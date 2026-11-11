// =============================================================================
//  CompDisks.cpp — Lecture du catalogue CompDisks et correspondance des images.
//  Voir CompDisks.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/CompDisks.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace neost {
namespace {

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Nom de fichier sans dossier ni extension, en minuscules.
std::string stemOf(const std::string& path) {
    std::size_t a = path.find_last_of("/\\");
    std::string base = (a == std::string::npos) ? path : path.substr(a + 1);
    const std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base.resize(dot);
    return lowerAscii(base);
}

// Lecteur JSON limité : objet { nom: { clé: "chaîne", … }, … }. Les échappements
// \" \\ \/ \b \f \n \r \t \uXXXX sont rendus en UTF-8 (paires de substitution comprises).
class Reader {
public:
    explicit Reader(const std::string& s) : s_(s) {}
    bool fail() const { return fail_; }

    void ws() { while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_; }
    bool eat(char c) { ws(); if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; } return false; }
    bool peek(char c) { ws(); return i_ < s_.size() && s_[i_] == c; }

    std::string str() {
        std::string out;
        if (!eat('"')) { fail_ = true; return out; }
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) { fail_ = true; return out; }
            c = s_[i_++];
            switch (c) {
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        const unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: out += c; break;   // \" \\ \/
            }
        }
        if (!eat('"')) fail_ = true;
        return out;
    }

    // Saute une valeur quelconque (nombre, booléen, tableau, objet) : le catalogue
    // n'en a pas, mais une évolution du format ne doit pas tout rejeter.
    void skipValue() {
        ws();
        if (i_ >= s_.size()) { fail_ = true; return; }
        const char c = s_[i_];
        if (c == '"') { str(); return; }
        if (c == '{' || c == '[') {
            const char close = (c == '{') ? '}' : ']';
            ++i_;
            if (eat(close)) return;
            do {
                if (c == '{') { str(); if (!eat(':')) { fail_ = true; return; } }
                skipValue();
            } while (!fail_ && eat(','));
            if (!eat(close)) fail_ = true;
            return;
        }
        while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']') ++i_;
    }

private:
    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            if (i_ >= s_.size()) { fail_ = true; return 0; }
            const char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
            else { fail_ = true; return 0; }
        }
        return v;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F));
               o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    const std::string& s_;
    std::size_t i_ = 0;
    bool fail_ = false;
};

}  // namespace

void splitDiskName(const std::string& stem, std::string& letters, int& number) {
    letters.clear();
    number = -1;
    // Dernier groupe de chiffres : « pp_001 » → 1, « fuzcd077 » → 77, « disk 12b » → 12.
    std::size_t end = stem.size();
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(stem[end - 1]))) --end;
    std::size_t beg = end;
    while (beg > 0 && std::isdigit(static_cast<unsigned char>(stem[beg - 1]))) --beg;
    if (beg < end && end - beg <= 6) number = std::stoi(stem.substr(beg, end - beg));
    // Lettres AVANT le numéro, sans séparateurs ni mots « disk »/« menu ».
    std::string raw;
    for (std::size_t k = 0; k < beg; ++k) {
        const unsigned char c = static_cast<unsigned char>(stem[k]);
        if (std::isalpha(c)) raw += static_cast<char>(std::tolower(c));
    }
    for (const char* w : {"disk", "menu"}) {
        const std::size_t p = raw.size() >= 4 ? raw.rfind(w) : std::string::npos;
        if (p != std::string::npos && p + 4 == raw.size() && p > 0) raw.resize(p);
    }
    letters = raw;
}

bool CompDiskCatalog::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadFromText(ss.str());
}

bool CompDiskCatalog::loadFromText(const std::string& json) {
    entries_.clear(); byStem_.clear(); fileKeys_.clear(); teamKeys_.clear(); byNumber_.clear();
    Reader r(json);
    if (!r.eat('{')) return false;
    if (r.eat('}')) return true;
    do {
        const std::string name = r.str();
        if (r.fail() || !r.eat(':')) return false;
        if (!r.peek('{')) { r.skipValue(); continue; }
        r.eat('{');
        CompDiskInfo info;
        info.name = name;
        std::string file;
        if (!r.eat('}')) {
            do {
                const std::string key = r.str();
                if (r.fail() || !r.eat(':')) return false;
                if (!r.peek('"')) { r.skipValue(); continue; }
                const std::string val = r.str();
                if (key == "Games") info.games = val;
                else if (key == "Team") info.team = val;
                else if (key == "File") file = val;
            } while (!r.fail() && r.eat(','));
            if (r.fail() || !r.eat('}')) return false;
        }
        const std::size_t idx = entries_.size();
        entries_.push_back(info);
        Key fk, tk;
        const std::string st = stemOf(file);
        splitDiskName(st, fk.letters, fk.number);
        // Équipe + numéro du NOM (« Fuzion Disk 77 ») : rattrape les fichiers nommés
        // d'après l'équipe plutôt que d'après le fichier d'origine.
        std::string nm; int nn;
        splitDiskName(lowerAscii(name), nm, nn);
        splitDiskName(lowerAscii(info.team), tk.letters, tk.number);
        tk.number = nn;
        fileKeys_.push_back(fk);
        teamKeys_.push_back(tk);
        if (!st.empty()) byStem_.emplace(st, idx);          // premier gardé en cas de doublon
        if (fk.number >= 0) byNumber_[fk.number].push_back(idx);
        if (tk.number >= 0 && tk.number != fk.number) byNumber_[tk.number].push_back(idx);
    } while (!r.fail() && r.eat(','));
    return !r.fail() && r.eat('}');
}

const CompDiskInfo* CompDiskCatalog::find(const std::string& imagePath) const {
    const std::string st = stemOf(imagePath);
    if (auto it = byStem_.find(st); it != byStem_.end()) return &entries_[it->second];
    std::string letters; int number;
    splitDiskName(st, letters, number);
    if (number < 0 || letters.size() < 2) return nullptr;
    const auto it = byNumber_.find(number);
    if (it == byNumber_.end()) return nullptr;
    auto compatible = [&](const Key& k) {
        if (k.number != number || k.letters.size() < 2) return false;
        const std::string& a = k.letters;
        // Un préfixe de l'autre (« fuz » / « fuzcd », « automation » / « auto »), 3 lettres au moins
        // pour le plus court : « pp » seul resterait trop ambigu.
        const std::size_t n = std::min(a.size(), letters.size());
        return n >= 3 && a.compare(0, n, letters, 0, n) == 0;
    };
    const CompDiskInfo* found = nullptr;
    std::size_t foundIdx = 0;
    for (const std::size_t idx : it->second) {
        if (!compatible(fileKeys_[idx]) && !compatible(teamKeys_[idx])) continue;
        if (found && idx != foundIdx) return nullptr;      // ambigu : rien plutôt que faux
        found = &entries_[idx];
        foundIdx = idx;
    }
    return found;
}

}  // namespace neost
