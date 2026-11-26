// =============================================================================
//  ArchiveDisks.cpp — cf. ArchiveDisks.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/ArchiveDisks.hpp"

#include "util/Archive.hpp"
#include "util/RomProbe.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace fs = std::filesystem;

namespace neost::archive {

namespace {

std::string lowerOf(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extOf(const std::string& path) { return lowerOf(fs::path(path).extension().string()); }

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return bool(f) || f.eof();
}

// Écrit `data` en `path` s'il n'existe pas. Passe par un fichier « .part » renommé :
// une extraction interrompue ne laisse pas une image tronquée que la règle « jamais
// réécrit » rendrait définitive.
bool writeIfAbsent(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    if (fs::exists(path, ec)) return true;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// Membres parasites des archives faites sur Mac : ni des disquettes ni à extraire.
bool macJunk(const std::string& rel) {
    return rel.rfind("__MACOSX/", 0) == 0 || fs::path(rel).filename().string().rfind("._", 0) == 0;
}

}  // namespace

bool isArchiveName(const std::string& path) {
    const std::string e = extOf(path);
    return e == ".zip" || e == ".7z" || e == ".gz";
}

bool isFloppyArchiveName(const std::string& path) {
    const std::string e = extOf(path);
    if (e == ".zip" || e == ".7z") return true;
    return e == ".gz" && romprobe::isFloppyImageName(fs::path(path).stem().string());
}

std::string unpackDirFor(const std::string& archive, const std::string& cacheRoot) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(archive, ec);
    if (ec) return {};
    std::string name;
    for (char c : fs::path(archive).stem().string()) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Nom de dossier sûr sur les trois hôtes ; l'UTF-8 (≥ $80) passe tel quel.
        name += (std::isalnum(u) || u >= 0x80 || std::string(" -_()[].,'&+").find(c) != std::string::npos) ? c : '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();   // Windows
    if (name.empty()) name = "archive";
    return (fs::path(cacheRoot) / (name + "-" + std::to_string(size))).string();
}

bool unpackFloppies(const std::string& archive, const std::string& dir,
                    std::vector<std::string>& disks, std::string* err) {
    disks.clear();
    auto fail = [&](const std::string& why) { if (err) *err = why; return false; };
    std::vector<uint8_t> raw;
    if (!readAll(archive, raw)) return fail("cannot read the archive");
    const fs::path root(dir);
    std::string e;

    if (isGzip(raw)) {
        std::vector<uint8_t> out;
        std::string name;
        if (!gunzip(raw, out, &name, &e)) return fail(e);
        name = safeMemberPath(fs::path(name).filename().string());
        if (name.empty()) name = fs::path(archive).stem().string();      // « jeu.st.gz » → « jeu.st »
        if (!romprobe::isFloppyImageName(name)) return fail("not a floppy image: " + name);
        const fs::path dst = root / name;
        if (!writeIfAbsent(dst, out)) return fail("cannot write " + dst.string());
        disks.push_back(dst.string());
        return true;
    }

    Reader reader;
    if (!reader.open(raw, &e)) return fail(e);
    struct Found { std::string key, rel; std::size_t index; };
    std::vector<Found> found;
    for (std::size_t i = 0; i < reader.members().size(); ++i) {
        const Member& m = reader.members()[i];
        if (m.directory) continue;
        const std::string rel = safeMemberPath(m.name);
        if (rel.empty() || macJunk(rel) || !romprobe::isFloppyImageName(rel)) continue;
        found.push_back({lowerOf(rel), rel, i});
    }
    if (found.empty()) return fail("no floppy image in the archive");
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.key < b.key; });
    std::string lastErr;
    for (const Found& f : found) {
        const fs::path dst = root / f.rel;
        std::error_code ec;
        if (!fs::exists(dst, ec)) {
            std::vector<uint8_t> out;
            std::string e2;
            if (!reader.extract(f.index, out, &e2)) { lastErr = f.rel + ": " + e2; continue; }
            if (!writeIfAbsent(dst, out)) { lastErr = "cannot write " + dst.string(); continue; }
        }
        disks.push_back(dst.string());
    }
    if (disks.empty()) return fail(lastErr.empty() ? std::string("no floppy could be unpacked") : lastErr);
    return true;
}

std::vector<std::string> unpackedFloppies(const std::string& dir) {
    std::vector<std::pair<std::string, std::string>> found;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || !romprobe::isFloppyImageName(it->path().string())) continue;
        found.push_back({lowerOf(it->path().lexically_relative(dir).generic_string()), it->path().string()});
    }
    // Même ordre qu'unpackFloppies : chemin relatif en minuscules (« Disk 1 of 2 » d'abord).
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

}  // namespace neost::archive
