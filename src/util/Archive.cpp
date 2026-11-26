// =============================================================================
//  Archive.cpp — cf. Archive.hpp.
//
//  Formats : [B] PKWARE APPNOTE.TXT 6.3.x (§ 4.3.7 en-tête local, § 4.3.12 répertoire
//  central, § 4.3.16 fin de répertoire) ; [B] RFC 1952 (gzip) ; [B] RFC 1951 (deflate,
//  décodé par stb_image) ; [B] 7-Zip 24.09 DOC/7zFormat.txt (signature, décodé par le
//  SDK LZMA d'extern/lzma).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/Archive.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "util/Crc32.hpp"

// Décodeur 7z du SDK LZMA (C, domaine public), compilé dans neost_core.
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"

// stb_image en STATIQUE : seul son décodeur zlib sert ici, et l'unité d'implémentation
// du GUI (src/gui/StbImage.cpp) garde ses symboles publics sans conflit à l'édition
// de liens.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace neost::archive {
namespace {

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
bool fail(std::string* err, const char* why) { if (err) *err = why; return false; }

}  // namespace

bool isZip(const std::vector<uint8_t>& d) {
    return d.size() >= 22 && d[0] == 'P' && d[1] == 'K' && ((d[2] == 3 && d[3] == 4) || (d[2] == 5 && d[3] == 6));
}

bool isGzip(const std::vector<uint8_t>& d) { return d.size() >= 18 && d[0] == 0x1F && d[1] == 0x8B && d[2] == 8; }

bool is7z(const std::vector<uint8_t>& d) {
    // [B] 7zFormat.txt « SignatureHeader » : '7' 'z' BC AF 27 1C, puis la version.
    static const uint8_t sig[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    return d.size() >= 32 && std::memcmp(d.data(), sig, 6) == 0;
}

bool inflateRaw(const uint8_t* src, std::size_t n, std::vector<uint8_t>& out, std::size_t expected) {
    out.clear();
    if (n > 0x7FFFFFFFu) return false;
    int len = 0;
    char* p = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(src), int(n), &len);
    if (!p) return false;
    out.assign(reinterpret_cast<uint8_t*>(p), reinterpret_cast<uint8_t*>(p) + len);
    std::free(p);
    return expected == 0 || out.size() == expected;
}

bool listZip(const std::vector<uint8_t>& d, std::vector<ZipMember>& out, std::string* err) {
    out.clear();
    if (d.size() < 22) return fail(err, "not a zip file");
    // Fin de répertoire central : signature PK\5\6, cherchée depuis la fin (le
    // commentaire d'archive fait au plus 65 535 octets).
    std::size_t eocd = std::string::npos;
    const std::size_t lo = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
    for (std::size_t i = d.size() - 22 + 1; i-- > lo;)
        if (d[i] == 'P' && d[i + 1] == 'K' && d[i + 2] == 5 && d[i + 3] == 6) { eocd = i; break; }
    if (eocd == std::string::npos) return fail(err, "zip directory not found");
    const uint8_t* e = d.data() + eocd;
    if (le16(e + 4) != 0 || le16(e + 6) != 0) return fail(err, "multi-volume zip");
    const uint16_t count = le16(e + 10);
    const uint32_t cdSize = le32(e + 12), cdOff = le32(e + 16);
    if (cdOff == 0xFFFFFFFFu || count == 0xFFFF) return fail(err, "zip64 archive");
    if (std::size_t(cdOff) + cdSize > d.size()) return fail(err, "truncated zip directory");
    std::size_t p = cdOff;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > d.size() || le32(d.data() + p) != 0x02014B50u) return fail(err, "corrupt zip directory");
        const uint8_t* c = d.data() + p;
        ZipMember m;
        m.method = le16(c + 10);
        m.crc = le32(c + 16);
        m.compressedSize = le32(c + 20);
        m.size = le32(c + 24);
        const uint16_t nameLen = le16(c + 28), extraLen = le16(c + 30), commentLen = le16(c + 32);
        m.localHeader = le32(c + 42);
        if (p + 46 + nameLen > d.size()) return fail(err, "corrupt zip directory");
        m.name.assign(reinterpret_cast<const char*>(c + 46), nameLen);
        for (char& ch : m.name) if (ch == '\\') ch = '/';
        out.push_back(m);
        p += 46u + nameLen + extraLen + commentLen;
    }
    return true;
}

bool extractZip(const std::vector<uint8_t>& d, const ZipMember& m, std::vector<uint8_t>& out, std::string* err) {
    out.clear();
    const std::size_t h = m.localHeader;
    if (h + 30 > d.size() || le32(d.data() + h) != 0x04034B50u) return fail(err, "corrupt zip member header");
    const uint8_t* l = d.data() + h;
    if (le16(l + 6) & 1) return fail(err, "encrypted zip member");
    const std::size_t data = h + 30 + le16(l + 26) + le16(l + 28);
    if (data + m.compressedSize > d.size()) return fail(err, "truncated zip member");
    if (m.method == 0) {
        if (m.compressedSize != m.size) return fail(err, "corrupt stored zip member");
        out.assign(d.begin() + std::ptrdiff_t(data), d.begin() + std::ptrdiff_t(data + m.size));
    } else if (m.method == 8) {
        if (!inflateRaw(d.data() + data, m.compressedSize, out, m.size)) return fail(err, "bad deflate data");
    } else {
        return fail(err, "unsupported zip compression method");
    }
    if (neost::crc32(out.data(), out.size()) != m.crc) return fail(err, "zip member CRC mismatch");
    return true;
}

bool gunzip(const std::vector<uint8_t>& d, std::vector<uint8_t>& out, std::string* name, std::string* err) {
    out.clear();
    if (name) name->clear();
    if (!isGzip(d)) return fail(err, "not a gzip file");
    // [B] RFC 1952 § 2.3 : FTEXT 1, FHCRC 2, FEXTRA 4, FNAME 8, FCOMMENT 16.
    const uint8_t flags = d[3];
    std::size_t p = 10;
    if (flags & 4) { if (p + 2 > d.size()) return fail(err, "truncated gzip"); p += 2u + le16(d.data() + p); }
    auto zstring = [&](std::string* into) {
        const std::size_t s = p;
        while (p < d.size() && d[p]) ++p;
        if (into) into->assign(reinterpret_cast<const char*>(d.data() + s), p - s);
        ++p;
    };
    if (flags & 8) zstring(name);
    if (flags & 16) zstring(nullptr);
    if (flags & 2) p += 2;
    if (p + 8 > d.size()) return fail(err, "truncated gzip");
    const uint32_t crc = le32(d.data() + d.size() - 8), isize = le32(d.data() + d.size() - 4);
    if (!inflateRaw(d.data() + p, d.size() - 8 - p, out, 0)) return fail(err, "bad deflate data");
    if (uint32_t(out.size()) != isize) return fail(err, "gzip size mismatch");
    if (neost::crc32(out.data(), out.size()) != crc) return fail(err, "gzip CRC mismatch");
    return true;
}

// =============================================================================
//  Reader : zip ou 7z
// =============================================================================
namespace {

// Flux d'entrée du SDK sur un tampon en mémoire (ISeekInStream).
struct MemInStream {
    ISeekInStream vt;
    const uint8_t* data;
    std::size_t size, pos;
};

SRes memRead(ISeekInStreamPtr pp, void* buf, size_t* size) {
    auto* p = reinterpret_cast<MemInStream*>(const_cast<ISeekInStream*>(pp));
    const std::size_t n = std::min(*size, p->size - p->pos);
    std::memcpy(buf, p->data + p->pos, n);
    p->pos += n;
    *size = n;
    return SZ_OK;
}

SRes memSeek(ISeekInStreamPtr pp, Int64* pos, ESzSeek origin) {
    auto* p = reinterpret_cast<MemInStream*>(const_cast<ISeekInStream*>(pp));
    Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? Int64(p->pos) : Int64(p->size);
    const Int64 to = base + *pos;
    if (to < 0 || uint64_t(to) > p->size) return SZ_ERROR_READ;
    p->pos = std::size_t(to);
    *pos = to;
    return SZ_OK;
}

const ISzAlloc kAlloc = {SzAlloc, SzFree};

const char* szError(SRes r) {
    switch (r) {
        case SZ_ERROR_UNSUPPORTED: return "unsupported 7z method (encrypted?)";
        case SZ_ERROR_CRC:         return "7z CRC mismatch";
        case SZ_ERROR_MEM:         return "out of memory";
        case SZ_ERROR_INPUT_EOF:   return "truncated 7z archive";
        case SZ_ERROR_NO_ARCHIVE:  return "not a 7z archive";
        default:                   return "corrupt 7z archive";
    }
}

std::string utf16ToUtf8(const std::vector<UInt16>& w) {
    std::string out;
    for (std::size_t i = 0; i < w.size() && w[i]; ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (w[++i] - 0xDC00);
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

}  // namespace

struct Reader::SevenZip {
    MemInStream mem{};
    CLookToRead2 look{};
    std::vector<Byte> lookBuf = std::vector<Byte>(1u << 16);
    CSzArEx db{};
    // Cache du dernier bloc solide décodé (contrat de SzArEx_Extract).
    UInt32 blockIndex = 0xFFFFFFFFu;
    Byte* outBuffer = nullptr;
    size_t outBufferSize = 0;

    SevenZip() { SzArEx_Init(&db); }
    ~SevenZip() {
        ISzAlloc_Free(&kAlloc, outBuffer);
        SzArEx_Free(&db, &kAlloc);
    }
};

Reader::Reader() = default;
Reader::~Reader() = default;

bool Reader::open(const std::vector<uint8_t>& d, std::string* err) {
    data_ = &d;
    members_.clear();
    zip_.clear();
    sz_.reset();
    if (isZip(d)) {
        if (!listZip(d, zip_, err)) return false;
        for (const auto& z : zip_) members_.push_back({z.name, z.size, z.directory()});
        return true;
    }
    if (!is7z(d)) return fail(err, "not a zip or 7z archive");
    static std::once_flag crcTable;
    std::call_once(crcTable, [] { CrcGenerateTable(); });
    auto s = std::make_unique<SevenZip>();
    s->mem.vt.Read = memRead;
    s->mem.vt.Seek = memSeek;
    s->mem.data = d.data();
    s->mem.size = d.size();
    LookToRead2_CreateVTable(&s->look, False);
    s->look.buf = s->lookBuf.data();
    s->look.bufSize = s->lookBuf.size();
    s->look.realStream = &s->mem.vt;
    LookToRead2_INIT(&s->look)
    const SRes r = SzArEx_Open(&s->db, &s->look.vt, &kAlloc, &kAlloc);
    if (r != SZ_OK) return fail(err, szError(r));
    uint64_t total = 0;
    std::vector<UInt16> name;
    for (UInt32 i = 0; i < s->db.NumFiles; ++i) {
        name.assign(SzArEx_GetFileNameUtf16(&s->db, i, nullptr), 0);
        SzArEx_GetFileNameUtf16(&s->db, i, name.data());
        Member m;
        m.name = utf16ToUtf8(name);
        for (char& ch : m.name) if (ch == '\\') ch = '/';
        m.directory = SzArEx_IsDir(&s->db, i);
        m.size = m.directory ? 0 : SzArEx_GetFileSize(&s->db, i);
        if (m.directory && (m.name.empty() || m.name.back() != '/')) m.name += '/';
        total += m.size;
        members_.push_back(std::move(m));
    }
    // Un bloc solide se décode d'un coup en mémoire : borner le tout borne chaque bloc.
    if (total > kMaxUnpacked) { members_.clear(); return fail(err, "7z archive too large"); }
    sz_ = std::move(s);
    return true;
}

bool Reader::extract(std::size_t i, std::vector<uint8_t>& out, std::string* err) {
    out.clear();
    if (!data_ || i >= members_.size()) return fail(err, "no such archive member");
    if (members_[i].directory) return true;
    if (!sz_) return extractZip(*data_, zip_[i], out, err);
    SevenZip& s = *sz_;
    size_t offset = 0, size = 0;
    const SRes r = SzArEx_Extract(&s.db, &s.look.vt, UInt32(i), &s.blockIndex, &s.outBuffer, &s.outBufferSize,
                                  &offset, &size, &kAlloc, &kAlloc);
    if (r != SZ_OK) return fail(err, szError(r));
    out.assign(s.outBuffer + offset, s.outBuffer + offset + size);
    return true;
}

std::string safeMemberPath(const std::string& name) {
    std::string out, part;
    auto flush = [&]() -> bool {
        if (part.empty() || part == ".") { part.clear(); return true; }
        if (part == "..") return false;
        if (!out.empty()) out += '/';
        out += part;
        part.clear();
        return true;
    };
    if (name.empty() || name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':')) return {};
    for (char c : name) {
        if (c == '/' || c == '\\') { if (!flush()) return {}; }
        else if (static_cast<unsigned char>(c) < 0x20) return {};
        else part += c;
    }
    if (!flush()) return {};
    return out;
}

}  // namespace neost::archive
