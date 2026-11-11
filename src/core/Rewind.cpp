// =============================================================================
//  Rewind.cpp — Anneau d'états du retour arrière. Voir Rewind.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Rewind.hpp"

#include <algorithm>
#include <set>

#include "core/Machine.hpp"

void Rewind::setEnabled(bool on) {
    enabled_ = on;
    if (!on) clear();
}

void Rewind::clear() {
    ring_.clear();
    since_ = 0;
}

const char* Rewind::blocker(const Machine& m) {
    if (m.gemdos.active())   return "GEMDOS hard disk mounted";
    if (m.fdc.acsiActive())  return "ACSI hard disk mounted";
    if (m.ultraSatanEnabled()) return "UltraSatan enabled";
    if (m.isp1160.enabled())
        for (unsigned port = 0; port < 2; ++port)
            if (m.isp1160.device(port).kind() == UsbDevice::Kind::Storage)
                return "USB storage attached";
    return nullptr;
}

std::size_t Rewind::bytes() const {
    std::size_t total = 0;
    std::set<const Fdc::MediaCopy*> seen;      // une copie partagée ne compte qu'une fois
    for (const Snap& s : ring_) {
        total += s.state.size();
        for (const auto& disk : s.disk)
            if (disk && seen.insert(disk.get()).second) total += Fdc::mediaCopyBytes(*disk);
    }
    return total;
}

void Rewind::trim() {
    while (ring_.size() > 1 && bytes() > budget_) ring_.pop_front();
}

void Rewind::onFrame(Machine& m) {
    if (!enabled_) return;
    if (since_ < interval_) ++since_;
    if (since_ < interval_) return;
    if (blocker(m)) { clear(); return; }
    if (m.fdc.stxCommandActive()) return;      // réessai à la trame suivante

    Snap snap;
    if (!m.saveState(snap.state)) return;
    snap.stxWrites = m.fdc.stxWrites();
    for (int drive = 0; drive < 2; ++drive) {
        const auto* previous = ring_.empty() ? nullptr : &ring_.back().disk[drive];
        if (previous && *previous
            && Fdc::mediaCopyGeneration(**previous) == m.fdc.mediaGeneration(drive))
            snap.disk[drive] = *previous;
        else
            snap.disk[drive] = m.fdc.copyMedia(drive);
    }
    ring_.push_back(std::move(snap));
    ++taken_;
    since_ = 0;
    trim();
}

bool Rewind::stepBack(Machine& m, std::string* why) {
    auto fail = [&](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    if (!enabled_) return fail("Rewind is off");
    if (const char* reason = blocker(m)) return fail(reason);

    // Une écriture STX n'est pas réversible : ce qui la précède n'est plus accessible.
    const uint64_t stx = m.fdc.stxWrites();
    ring_.erase(std::remove_if(ring_.begin(), ring_.end(),
                               [&](const Snap& s) { return s.stxWrites != stx; }),
                ring_.end());
    if (ring_.empty()) return fail("Nothing to rewind");

    // L'instantané tout juste pris (ou tout juste restauré) ne ramènerait nulle part :
    // on recule au précédent. Sans cela, un second appui resterait sur place.
    // Le retrait n'a lieu qu'UNE FOIS la cible acceptée (plus bas) : retiré d'emblée,
    // chaque appui sur un état refusé rognait l'anneau d'un instantané.
    const bool skipLast = since_ < interval_ / 2 && ring_.size() > 1;
    const Snap& target = skipLast ? ring_[ring_.size() - 2] : ring_.back();
    // L'en-tête AVANT le média : restoreMedia réécrit l'image et son fichier hôte, et un
    // état refusé ensuite (clé, carte réseau ou cartouche changée depuis la capture)
    // laissait une disquette du passé sous un TOS au présent — FAT en cache périmée.
    // L'anneau est GARDÉ : rebrancher la clé ou la cartouche rend ces instantanés
    // de nouveau valides.
    if (!m.stateHeaderOk(target.state.data(), target.state.size()))
        return fail("State refused");
    if (skipLast) ring_.pop_back();      // `target` (l'avant-dernier) reste valide
    // Le média d'abord : la remontée arme des transitions que loadState écrase ensuite.
    for (int drive = 0; drive < 2; ++drive)
        if (target.disk[drive] && !m.fdc.restoreMedia(drive, *target.disk[drive]))
            return fail("Floppy image is gone");
    if (!m.loadState(target.state.data(), target.state.size()))
        return fail("State refused");
    since_ = 0;
    return true;
}
