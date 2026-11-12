// NeoST — NAT intégré (issu de POM2)
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatAllowlist — see the header.

#include "NatAllowlist.h"

#include "Logger.h"

#include <algorithm>

namespace nat {
namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }

std::string normalised(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), lower);
    while (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

/// Dotted IPv4, four decimal fields of 0..255, in host order.
bool dottedIpv4(const std::string& s, uint32_t& out)
{
    uint32_t address = 0;
    std::size_t i = 0;
    for (int field = 0; field < 4; ++field) {
        if (field && (i >= s.size() || s[i++] != '.')) return false;
        unsigned n = 0, digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 4)
            n = n * 10 + unsigned(s[i++] - '0'), ++digits;
        if (!digits || digits > 3 || n > 255) return false;
        address = (address << 8) | n;
    }
    if (i != s.size()) return false;
    out = address;
    return true;
}

bool validDomain(const std::string& d)
{
    if (d.empty() || d.size() > 253) return false;
    std::size_t label = 0;
    for (char c : d) {
        if (c == '.') {
            if (!label) return false;
            label = 0;
            continue;
        }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok || ++label > 63) return false;
    }
    return label != 0;
}

/// One owner name of a resource record, labels or a compression pointer
/// ([B] RFC 1035 § 4.1.4). Only skipped: the answer was already matched to
/// the question by the DNS proxy.
bool skipName(const detail::Bytes& m, std::size_t& i)
{
    for (int labels = 0; labels < 128; ++labels) {
        if (i >= m.size()) return false;
        const uint8_t n = m[i];
        if ((n & 0xC0) == 0xC0) { i += 2; return i <= m.size(); }
        if (n & 0xC0) return false;
        ++i;
        if (n == 0) return true;
        i += n;
    }
    return false;
}

}  // namespace

bool parseAllowedDomains(const std::string& text, std::vector<std::string>& out)
{
    std::vector<std::string> parsed;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return true;
        std::string d = normalised(word);
        word.clear();
        if (d.rfind("*.", 0) == 0) d.erase(0, 2);
        else if (!d.empty() && d[0] == '.') d.erase(0, 1);
        if (!validDomain(d) || parsed.size() >= 64) return false;
        if (std::find(parsed.begin(), parsed.end(), d) == parsed.end()) parsed.push_back(d);
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!flush()) return false;
        } else {
            word.push_back(c);
        }
    }
    if (!flush()) return false;
    out = std::move(parsed);
    return true;
}

bool domainAllowed(const std::vector<std::string>& list, const std::string& name)
{
    const std::string n = normalised(name);
    if (n.empty()) return false;
    for (const std::string& d : list) {
        if (n == d) return true;
        if (n.size() > d.size() && n[n.size() - d.size() - 1] == '.' &&
            n.compare(n.size() - d.size(), d.size(), d) == 0)
            return true;
    }
    return false;
}

namespace detail {

DomainGate::DomainGate(std::vector<std::string> domains) : domains_(std::move(domains))
{
    for (const std::string& d : domains_) {
        uint32_t ip;
        if (dottedIpv4(d, ip)) literal4_.insert(ip);
    }
}

bool DomainGate::admitName(const std::string& name)
{
    if (!active() || allowsName(name)) return true;
    ++refusedNames_;
    if (logClosed_ || loggedNames_.count(name)) return false;
    if (loggedNames_.size() >= kMaxNamesLogged) {
        logClosed_ = true;
        log().warn("Net", "built-in NAT: further names outside the domain allowlist are "
                          "refused without a log line");
        return false;
    }
    loggedNames_.insert(name);
    log().warn("Net", "built-in NAT: DNS query for \"" + name +
                          "\" refused — not in the domain allowlist (net_allow_domains)");
    return false;
}

void DomainGate::learn4(uint32_t ip)
{
    if (!active() || !known4_.insert(ip).second) return;
    order4_.push_back(ip);
    if (order4_.size() > kMaxLearned) { known4_.erase(order4_.front()); order4_.pop_front(); }
}

void DomainGate::learn6(const Ipv6& ip)
{
    if (!active() || !known6_.insert(ip).second) return;
    order6_.push_back(ip);
    if (order6_.size() > kMaxLearned) { known6_.erase(order6_.front()); order6_.pop_front(); }
}

void DomainGate::learnWire(const Bytes& m)
{
    if (!active() || m.size() < 12 || (m[3] & 0x0F) != 0) return;   // NOERROR only
    const unsigned questions = rd16(m.data() + 4), answers = rd16(m.data() + 6);
    std::size_t i = 12;
    for (unsigned q = 0; q < questions; ++q) {
        if (!skipName(m, i) || i + 4 > m.size()) return;
        i += 4;
    }
    for (unsigned a = 0; a < answers; ++a) {
        if (!skipName(m, i) || i + 10 > m.size()) return;
        const uint16_t type = rd16(m.data() + i), klass = rd16(m.data() + i + 2);
        const uint16_t length = rd16(m.data() + i + 8);
        i += 10;
        if (i + length > m.size()) return;
        if (klass == 1 && type == 1 && length == 4) learn4(rd32(m.data() + i));
        if (klass == 1 && type == 28 && length == 16) {
            Ipv6 ip{};
            std::copy_n(m.data() + i, 16, ip.begin());
            learn6(ip);
        }
        i += length;
    }
}

bool DomainGate::reaches(const Endpoint& host) const
{
    if (!active()) return true;
    if (!host.isIpv6) return literal4_.count(host.ip) || known4_.count(host.ip);
    const Ipv6& a = host.ipv6;
    const bool mapped = std::all_of(a.begin(), a.begin() + 10, [](uint8_t x) { return x == 0; }) &&
                        a[10] == 0xff && a[11] == 0xff;
    if (mapped) {
        const uint32_t ip = rd32(a.data() + 12);
        return literal4_.count(ip) || known4_.count(ip);
    }
    return known6_.count(a) != 0;
}

void DomainGate::forget()
{
    known4_.clear(); order4_.clear();
    known6_.clear(); order6_.clear();
}

}  // namespace detail
}  // namespace nat
