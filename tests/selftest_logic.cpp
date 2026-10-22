// =============================================================================
//  selftest_logic.cpp — auto-test de la LOGIQUE PURE (pas d'émulation).
//
//  Ce que le headless ne peut pas couvrir : les fonctions sans machine ni ROM —
//  chemins hôte, parseurs, formats. Elles se testent en microsecondes, et jusqu'ici
//  elles ne se testaient nulle part : l'issue #37 (lecteur GEMDOS mort sur tous les
//  paquets Windows) tenait en UNE fonction de résolution de chemin que personne ne
//  pouvait exercer, faute d'endroit où poser dix lignes de test.
//
//  Volontairement SANS framework (le dépôt n'en a aucun, et n'en a pas besoin) :
//  un CHECK, un compteur, un code de sortie. Même forme de rapport que les
//  auto-tests du headless : « N OK, M FAIL ».
//
//  Lancer :  ./build/neost-selftest        (câblé au palier fast de run_all.py)
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/AppConfig.hpp"
#include "util/HostPath.hpp"
#include "util/JoyScript.hpp"
#include "util/ConfigPath.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include "io/CartridgeKey.hpp"
#include "io/PortDevices.hpp"
#include "io/DongleTable.hpp"
#include "io/Mfp.hpp"
#include "core/YM2149.hpp"
#include "core/StateArchive.hpp"
#include "audio/MidiEndpoint.hpp"
#include "audio/MidiInHost.hpp"
#include "audio/GmSynth.hpp"
#include "io/MidiAcia.hpp"
#include "io/Ikbd.hpp"
#include "io/Rtc.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Blitter.hpp"
#include "core/DmaSound.hpp"
#include "io/Fdc.hpp"
#include "core/Pacing.hpp"

#include <cmath>
#include "util/MouseScale.hpp"
#include "core/MachineType.hpp"
#include <filesystem>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

using namespace neost;
using hostpath::Style;

static int g_ok = 0, g_fail = 0;

bool testSccBrg(); // banc SCC + ordonnanceur dans selftest_scc.cpp
bool testSccSerial(); // cadence des caractères SCC, registres TDR/TSR et réception
int testMfpTx(); // pipeline USART MFP
int testMfpClock(); // diviseurs USART /1 et /16
int testMidiBreak(); // suspension et reprise TX MIDI
bool testSccRts(); // sortie RTS différée jusqu’au dernier stop
bool testSccSync(); // filtre WR3 Sync Character Load Inhibit
int testIkbdMouseMonitor(); // MouseAction et cadence commune après $17
int testMfpRxFormat(); // mots courts RX et format en vol
int testMidiTxFormat(); // formats et diviseurs TX ACIA
bool testSccTxFormat(); // WR5 cinq bits ou moins et format du TSR
bool testSccTxEnable(); // fin du caractère actif après désactivation TX
int testMfpRxEnable(); // arrêt RX, statut et registre UDR persistant
int testIkbdPause(); // PAUSE termine le caractère SCI engagé
int testMfpUsartControl(); // droits des bits de contrôle et de statut USART
bool testSccBreak(); // suspension TSR pendant SEND BREAK
int testIkbdOutputCapacity(); // capacité firmware indépendante du RDR ACIA
bool testSccClock(); // rapport rationnel des horloges directes SCC
int testMfpTurnaround(); // END au repos et auto-turnaround USART
bool testSccBrgClock(); // cadence rationnelle des caractères issus du BRG
int testMfpUnderrun(); // underrun mémorisé et front TXERR
int testFdcForceInterrupt(); // IRQ forcée et phase INDEX du média
bool testSccZeroCount(); // compteur BRG avant les diviseurs de sortie
bool testSccRxParity(); // parité reçue visible en bouclage avec mots courts
int testRtcBank(); // registres de banque indépendants du calendrier
int testRtcTimer(); // retenue du diviseur pendant arrêt TIMER EN
int testRtcLongElapsed(); // rattrapage calendaire sans limite de durée
int testRtcElapsedDifferential(); // comparaison avec les pas unitaires
int testRtcLongPhase(); // phase et retenue après arrêt prolongé
bool testSccChannelReset(); // reset IP/IUS isolé au canal ciblé
bool testSccMieIus(); // MIE masque sans effacer les interruptions en service
bool testSccAckPriority(); // priorité des IUS aussi pendant INTACK
bool testSccRxStop(); // RX un stop indépendamment du nombre de stops TX
bool testSccEchoTxClock(); // horloge TX indépendante de l’écho RX
bool testSccEchoBrgConfig(); // TC BRG sans effet sur écho RX à horloge directe
bool testSccEchoBrgControl(); // contrôle BRG indépendant de RXdirect, reprise écho invalidé
int testBusMmioMirror(); // écriture blitter atomique aussi aux miroirs24bits
int testBlitterResetRetention(); // RAM halftone et pipeline source conservés au reset chaud
bool testSccExtendedRead(); // images WR via WR7' D6 sur Z85C30
int testAcsiWriteDestination(); // cible et secteur du WRITE différé conservés
int testAcsiLargeBounds(); // borne READ/WRITE non signée sur image sparse de plus de 1 Tio
bool testSccRxErrorIrq(); // pas d’IP RX sans donnée après vidage FIFO All/Special
int testMfpGpipComposite(); // XSINT qualifié après composition complète du GPIP7
int testAcsiCapacityLimit(); // refus des images hors capacité des compteurs ACSI
bool testSccRxErrorMode(); // verrou erreur conservé entre First/Special et SpecialOnly
int testStxDeletedTrack(); // marque F8 conservée dans les pistes STX réécrites
int testStxSectorRewrite(); // WRITE SECTOR normal remplace la marque F8 par FB
int testStxDataCrcWrite(); // WRITE SECTOR STX sur CRC données + durée secteur HD
int testStxSectorDam(); // WRITE SECTOR A8/A9 et marque persistante FB/F8
int testStxRawTrackOverlay(); // fusion locale des overlays dans une image de piste décodée
int testStxRawTrackWrap(); // champs STX traversant l’index, fusion circulaire bornée
int testStxReadAddressCrc(); // CRC ID signalé après les six octets READ ADDRESS
int testIpfImage();         // IPF forgée : blocs, CRC32, flux de cellules, lecture WD1772 → STX, montage
int testIkbdAciaInit(); // sorties firmware rejetées avant initialisation du diviseur
int testIkbdAciaReset(); // registres ACIA conservés au reset du SCI clavier
int testIkbdSciReset(); // fragments CPU→SCI, réancrage et réception bit à bit
bool testSccFirstErrorReset(); // IP du premier caractère retiré aussi par Error Reset
int testDmaSoundMicrowireByte(); // fusion octet depuis DATA visible après décalage
int testDmaSoundMicrowireTiming(); // grille série et accès MMIO après fronts échus
int testRtcWriteOnly(); // TEST/RESET en écriture seule
int testMidiIrqTiming(); // ligne IRQ MIDI datée au transfert série
int testIkbdIrqTiming(); // ligne IRQ clavier datée aux fronts série
int testGemdosSnapshot(); // reprise transactionnelle des fichiers hôtes GEMDOS
int testDesktopMouse(); // pointeur absolu GEM, géométrie et pilote EmuTOS
int testMfpVectorRegister(); // bits réservés VR nuls, S et base préservés
int testFdcTrackIdLocation(); // IDs de piste ST compatibles avec tête et face réelles
int testMfpInternalLoopback(); // TSR H/L sélectionne le bouclage matériel interne
int testFdcSectorSizeSwitch(); // secteur STX long vers lecteur ST sélectionné en vol
int testMfpRxOverrun(); // erreur différée UDR puis acquittement RSR
int testMfpRxIrqTiming(); // RXFULL datée au stop même après service tardif
bool testSccExtStatus(); // verrous RR0 indépendants du masque maître WR1
int testMidiTxControl(); // phase du diviseur conservée à la sortie BREAK
int testMidiTxReclock(); // diviseur modifié pendant le caractère MIDI
int testIkbdTxControl(); // phase de reprise BREAK CPU vers IKBD
bool testSccBreakStatus(); // fronts BREAK conservés pendant un verrou externe
int testMidiTxPhase(); // départs MIDI sur les fronts du diviseur
int testIkbdTxPhase(); // phase des départs CPU vers IKBD usuels
bool testSccStatusModes(); // qualification Sync/Hunt et BREAK selon WR4/WR11
int testMfpBreak(); // BREAK ne commande que le mode USART asynchrone
bool testSccAutoEcho(); // sortie TSR déconnectée quand Auto Echo a priorité
int testStxWrittenTrack(); // une piste STX effacée ne ressuscite pas ses anciens secteurs
int testMfpAsyncBreak(); // cadence BREAK, fin TE et reprise asynchrones
bool testSccEchoReceive(); // écho hôte des formats RX connus
int testAcsiTransfer(); // statut SCSI d'une erreur de plage DMA
bool testSccLocalLoopback(); // multiplexage RxD et sortie TxD en boucle locale
int testMfpGpipLines(); // fronts BUSY/XSINT et qualification GPIO
int testFdcMediaChange(); // première et dernière actions de changement de média
bool testSccEomReset(); // commande WR0 de remise à zéro du latch EOM
int testMachineTimerBAer(); // recalage immédiat Timer B après changement AER3
int testMfpMmioDecode(); // pairs fautifs, mots partiels et impairs void MFP
bool testPsgOddWrite(); // ombres impaires PSG selon largeur du Bus
int testDmaSoundMmioState(); // octet FF8920 conservé dans les sauvegardes
bool testPsgReset(); // image des registres et silence après reset PSG
int testBlitterRmw(); // atomicité TAS devant une échéance blitter
int testMachineFdcMediaVbl(); // Mediach suit les vrais VBL malgré la fréquence vidéo
int testGemdosDriveMapping(); // GEMDOS après les partitions ACSI, routage des lecteurs
int testAcsiPartitions(); // comptage des racines DOS/Atari et des cibles ACSI
int testGemdosDirectoryEntries(); // points en sous-dossier selon masque et attributs
int testGemdosDtaReuse(); // recherches DTA indépendantes, réutilisation et erreurs
int testMfpUnderrunClear(); // délai matériel avant effacement UE
int testFdcLiveMedia(); // sélection live et validation CHS du nouveau média
int testMidiRxPhase(); // horloge MIDI RX continue
bool testSccAutoEnables(); // qualification CTS/DCD des canaux SCC
int testMachineMfpTx(); // émission via Bus/Machine
int testMidiRx(); // RDR MIDI et acquittement de l’overrun
int testMfpRxTiming(); // format et phase des caractères USART
bool testSccAsync(); // lecture RR8 dans les formats courts SCC
int testMidiSerial(); // pipeline MIDI /16 8N1
int testIkbdAciaFormat(); // break ACIA clavier
int testMachineMidi(); // callbacks MIDI à travers Bus et Machine
int testMfpSerial(); // réception USART et débordement
bool testSccRxFifo(); // FIFO matérielle de réception trois octets
int testIkbdCpuSerial(); // transit CPU→IKBD et registres TDR/TSR
int testAcsiIrqTiming(); // banc IRQ DMA ACSI dans fdc_acsi_logic.cpp
int testMachineScc(); // câblage BRG après changement de modèle
int testFdcRegisters(); // retenues d'adresse DMA et reset FIFO
int testMmuMapping(); // trou MMU STF et distinction reset chaud/froid
int testFdcSelection(); // sélection PSG et index vivant avant accès WD1772
int testMmioFidelity(); // lectures octet blitter et registre de densité disque
int testIkbdResponses(); // délais firmware des réponses IKBD
int testFdcTiming(); // attentes des registres FDC et ancrage anti-dérive
int testIkbdMonitor(); // taux d'échantillonnage joystick $17
int testIkbdMonitorMachine(); // câblage du timer dans la machine complète
int testDmaSoundMono(); // précision du repli mono et drainage après STOP
int testShifterStateGeometry(); // géométrie du save-state liée à la résolution (idx[660])
int testIkbdAutoSend(); // autosend indépendant de la fréquence vidéo

static void checkStr(const char* what, const std::string& got, const std::string& want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = \"%s\"\n%54s attendu \"%s\"\n",
                what, got.c_str(), "", want.c_str());
}

static void checkHex(const char* what, unsigned got, unsigned want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = $%04X (attendu $%04X)\n", what, got, want);
}

static void checkInt(const char* what, long got, long want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = %ld (attendu %ld)\n", what, got, want);
}

static void checkBool(const char* what, bool got, bool want) {
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = %s (attendu %s)\n",
                what, got ? "true" : "false", want ? "true" : "false");
}

// -----------------------------------------------------------------------------
//  hostpath — sémantique WINDOWS, exercée depuis n'importe quelle plateforme.
//  C'est tout l'intérêt du paramètre Style : ces cas-là n'étaient testables sur
//  aucune machine de développement, donc ne l'étaient pas.
// -----------------------------------------------------------------------------
static void testWindowsPaths() {
    const Style W = Style::Windows;
    std::printf("hostpath (style Windows)\n");

    checkBool("isAbsolute(C:\\Temp\\atari)",  hostpath::isAbsolute("C:\\Temp\\atari", W), true);
    checkBool("isAbsolute(C:/Temp/atari)",    hostpath::isAbsolute("C:/Temp/atari", W),   true);
    checkBool("isAbsolute(c:/x) minuscule",   hostpath::isAbsolute("c:/x", W),            true);
    checkBool("isAbsolute(\\\\srv\\share) UNC", hostpath::isAbsolute("\\\\srv\\share", W), true);
    checkBool("isAbsolute(//srv/share) UNC",  hostpath::isAbsolute("//srv/share", W),     true);
    checkBool("isAbsolute(hd) relatif",       hostpath::isAbsolute("hd", W),              false);
    checkBool("isAbsolute(..\\hd) relatif",   hostpath::isAbsolute("..\\hd", W),          false);
    checkBool("isAbsolute(\"\") vide",        hostpath::isAbsolute("", W),                false);

    checkStr("normalizeSeparators(a\\b\\c)", hostpath::normalizeSeparators("a\\b\\c", W), "a/b/c");

    // LE cas de l'issue #37 : le dossier déposé est absolu, il ne doit RIEN
    // recevoir du répertoire courant.
    checkStr("lexicalAbsolute(C:\\Temp\\atari)",
             hostpath::lexicalAbsolute("C:\\Temp\\atari", "C:\\Temp\\NeoST-0.5.2", W),
             "C:/Temp/atari");
    checkStr("lexicalAbsolute(UNC)",
             hostpath::lexicalAbsolute("\\\\srv\\part\\atari", "C:\\NeoST", W),
             "//srv/part/atari");
    checkStr("lexicalAbsolute(relatif → cwd)",
             hostpath::lexicalAbsolute("hd", "C:\\Temp\\NeoST", W), "C:/Temp/NeoST/hd");
    checkStr("lexicalAbsolute(..\\hd)",
             hostpath::lexicalAbsolute("..\\hd", "C:\\Temp\\NeoST", W), "C:/Temp/hd");
    checkStr("lexicalAbsolute(D:\\Jeux\\ST\\..\\HD)",
             hostpath::lexicalAbsolute("D:\\Jeux\\ST\\..\\HD", "C:\\NeoST", W), "D:/Jeux/HD");
    checkStr("lexicalAbsolute(.\\hd)",
             hostpath::lexicalAbsolute(".\\hd", "C:\\NeoST", W), "C:/NeoST/hd");

    // Remontée AU-DESSUS de la racine (chasse aux bugs du 2026-09-01). Le corps
    // d'Hatari est purement Unix : quand rfind ne trouvait plus de séparateur il
    // retombait sur « / », ce qui sous Windows désigne la racine du lecteur COURANT
    // du processus — donc un AUTRE lecteur. Les cas ci-dessus ne franchissaient
    // jamais la racine, d'où le trou.
    checkStr("lexicalAbsolute(C:\\..\\X) reste sur C:",
             hostpath::lexicalAbsolute("C:\\..\\X", "C:\\NeoST", W), "C:/X");
    checkStr("lexicalAbsolute(C:\\a\\..\\..\\X) reste sur C:",
             hostpath::lexicalAbsolute("C:\\a\\..\\..\\X", "C:\\NeoST", W), "C:/X");
    checkStr("lexicalAbsolute(UNC ..\\..) garde le partage",
             hostpath::lexicalAbsolute("\\\\srv\\part\\a\\..\\..\\X", "C:\\NeoST", W),
             "//srv/part/X");
    // Posix : le clamp EXISTANT ne bouge pas — c'est la racine « / », et y rester
    // est correct. Ces deux cas prouvent que le correctif n'a rien déplacé.
    checkStr("lexicalAbsolute(/../x) reste à la racine",
             hostpath::lexicalAbsolute("/../x", "/home/u", Style::Posix), "/x");
    checkStr("lexicalAbsolute(/a/../../x) reste à la racine",
             hostpath::lexicalAbsolute("/a/../../x", "/home/u", Style::Posix), "/x");

    // Racine de lecteur : « C:/ » rabattu en « C: » désignerait le dossier COURANT
    // du lecteur C, pas sa racine — piège classique de l'API Windows.
    checkStr("stripTrailingSep(C:/)",      hostpath::stripTrailingSep("C:/", W),      "C:/");
    checkStr("stripTrailingSep(C:/Temp/)", hostpath::stripTrailingSep("C:/Temp/", W), "C:/Temp");

    checkStr("join(base, relatif)", hostpath::join("C:/NeoST", "disks/a.st", W),
             "C:/NeoST/disks/a.st");
    checkStr("join(base/, relatif)", hostpath::join("C:/NeoST/", "disks/a.st", W),
             "C:/NeoST/disks/a.st");
    // Joindre une base à un chemin DÉJÀ absolu, c'est précisément le bug #37.
    checkStr("join(base, absolu) → absolu", hostpath::join("C:/NeoST", "C:\\Temp\\x", W),
             "C:/Temp/x");
}

// -----------------------------------------------------------------------------
//  hostpath — sémantique POSIX. Vérifie surtout qu'on n'a RIEN cassé en rendant
//  le module conscient de Windows : « C:/x » y est un chemin relatif ordinaire,
//  et '\' un caractère de nom parfaitement légal.
// -----------------------------------------------------------------------------
static void testPosixPaths() {
    const Style P = Style::Posix;
    std::printf("hostpath (style POSIX)\n");

    checkBool("isAbsolute(/usr/share)",        hostpath::isAbsolute("/usr/share", P), true);
    checkBool("isAbsolute(gemdos) relatif",    hostpath::isAbsolute("gemdos", P),     false);
    checkBool("isAbsolute(C:/x) = RELATIF",    hostpath::isAbsolute("C:/x", P),       false);
    checkBool("isAbsolute(\\\\srv) = RELATIF", hostpath::isAbsolute("\\\\srv", P),    false);

    // '\' est un caractère de nom légal sous Unix : le convertir corromprait le nom.
    checkStr("normalizeSeparators(a\\b) intact", hostpath::normalizeSeparators("a\\b", P), "a\\b");

    checkStr("lexicalAbsolute(absolu)",
             hostpath::lexicalAbsolute("/srv/gemdos", "/home/u/neost", P), "/srv/gemdos");
    checkStr("lexicalAbsolute(relatif → cwd)",
             hostpath::lexicalAbsolute("gemdos", "/home/u/neost", P), "/home/u/neost/gemdos");
    checkStr("lexicalAbsolute(../gemdos)",
             hostpath::lexicalAbsolute("../gemdos", "/home/u/neost", P), "/home/u/gemdos");
    checkStr("lexicalAbsolute(./gemdos)",
             hostpath::lexicalAbsolute("./gemdos", "/home/u/neost", P), "/home/u/neost/gemdos");
    checkStr("lexicalAbsolute(a/b/../c)",
             hostpath::lexicalAbsolute("/a/b/../c", "/cwd", P), "/a/c");

    checkStr("stripTrailingSep(/tmp/)", hostpath::stripTrailingSep("/tmp/", P), "/tmp");
    checkStr("stripTrailingSep(/)",     hostpath::stripTrailingSep("/", P),     "/");

    checkStr("join(base, relatif)",  hostpath::join("/opt/neost", "roms/a.img", P),
             "/opt/neost/roms/a.img");
    checkStr("join(base, absolu)",   hostpath::join("/opt/neost", "/srv/a.img", P), "/srv/a.img");
    checkStr("join(\"\", relatif)",  hostpath::join("", "roms/a.img", P), "roms/a.img");
}

// -----------------------------------------------------------------------------
//  Le module doit se comporter comme la plateforme QUAND ON NE PRÉCISE RIEN.
// -----------------------------------------------------------------------------
static void testNativeDefaults() {
    std::printf("hostpath (défauts natifs)\n");
#if defined(_WIN32)
    checkBool("natif = Windows", hostpath::kNative == Style::Windows, true);
    checkBool("isAbsolute(C:\\x) par défaut", hostpath::isAbsolute("C:\\x"), true);
#else
    checkBool("natif = POSIX", hostpath::kNative == Style::Posix, true);
    checkBool("isAbsolute(/x) par défaut",  hostpath::isAbsolute("/x"),   true);
    checkBool("isAbsolute(C:/x) par défaut", hostpath::isAbsolute("C:/x"), false);
#endif
    // currentDir() doit rendre un chemin absolu et normalisé, sinon tout chemin
    // relatif préfixé du cwd ressort en séparateurs mélangés (bug latent Windows).
    const std::string cwd = hostpath::currentDir();
    checkBool("currentDir() non vide", !cwd.empty(), true);
    checkBool("currentDir() absolu",   hostpath::isAbsolute(cwd), true);
    checkBool("currentDir() sans '\\'", cwd.find('\\') == std::string::npos, true);
    checkStr("makeAbsolute(chemin absolu) inchangé", hostpath::makeAbsolute(cwd), cwd);
}

// -----------------------------------------------------------------------------
//  Recomposition Unicode NFD → NFC des noms de fichiers (cible macOS).
//
//  macOS rend les noms de fichiers DÉCOMPOSÉS : « café » y est « cafe » + U+0301.
//  Sans recomposition, un fichier accentué du lecteur GEMDOS est INTROUVABLE depuis
//  le TOS. Le test est ici — et non dans un auto-test qui aurait besoin d'un vrai
//  volume macOS — parce que la fonction est PURE : on lui donne la forme décomposée
//  en dur, elle doit rendre la précomposée, sur n'importe quelle plateforme. C'est la
//  même discipline que `Style` pour les chemins Windows (cf. l'en-tête de HostPath.hpp) :
//  un défaut spécifique à une plateforme n'est gardé que s'il est exerçable ailleurs.
// -----------------------------------------------------------------------------
static void testPrecomposeUtf8() {
    std::printf("Unicode NFD → NFC (noms de fichiers macOS)\n");
    using neost::hostpath::precomposeUtf8;
    // « café » : la forme NFD fait 6 octets (e + U+0301 = 0xCC 0x81), la NFC 5
    // (é = U+00E9 = 0xC3 0xA9). C'est LE cas qui motive le port.
    checkStr("cafe+U+0301 → café", precomposeUtf8("cafe\xCC\x81"), "caf\xC3\xA9");
    checkStr("la chaîne raccourcit", std::to_string(precomposeUtf8("cafe\xCC\x81").size()), "5");
    // Déjà précomposé (Linux) : STRICTEMENT inchangé — le port doit être nul ailleurs.
    checkStr("café déjà NFC inchangé", precomposeUtf8("caf\xC3\xA9"), "caf\xC3\xA9");
    checkStr("ASCII pur inchangé",     precomposeUtf8("README.TXT"),    "README.TXT");
    checkStr("vide",                   precomposeUtf8(""),              "");
    // Majuscules, tréma, tilde, cédille, rond : les autres familles de la table.
    checkStr("A+U+0308 → Ä", precomposeUtf8("A\xCC\x88"), "\xC3\x84");
    checkStr("n+U+0303 → ñ", precomposeUtf8("n\xCC\x83"), "\xC3\xB1");
    checkStr("c+U+0327 → ç", precomposeUtf8("c\xCC\xA7"), "\xC3\xA7");
    checkStr("a+U+030A → å", precomposeUtf8("a\xCC\x8A"), "\xC3\xA5");
    // Plusieurs accents dans le même nom, et un accent en fin de chaîne (le cas où
    // l'accès à s[i+2] déborderait si la borne était mal écrite).
    checkStr("deux accents", precomposeUtf8("e\xCC\x81te\xCC\x81"), "\xC3\xA9t\xC3\xA9");
    checkStr("accent final", precomposeUtf8("cle\xCC\x81"), "cl\xC3\xA9");
    // Combinaison ABSENTE de la table (b + accent aigu n'existe pas en Latin-1) :
    // laissée telle quelle. Recomposer au-delà du jeu Atari n'aurait aucun sens.
    checkStr("hors table inchangé", precomposeUtf8("b\xCC\x81"), "b\xCC\x81");
    // Séquence tronquée : un octet de tête de marque combinante SANS son second
    // octet ne doit ni déborder ni être consommé.
    checkStr("marque tronquée", precomposeUtf8("e\xCC"), "e\xCC");
}

// -----------------------------------------------------------------------------
//  neost.cfg — le parseur et l'écrivain doivent se répondre CLÉ POUR CLÉ. Un
//  réglage écrit mais jamais relu (ou l'inverse) est muet : il ne casse rien au
//  démarrage, il rend juste le réglage inopérant à la session suivante. C'est
//  exactement le genre de défaut qu'aucun test d'émulation ne peut voir.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  Sensibilité de la souris émulée : la règle d'accumulation.
//
//  C'est LE point qui peut rendre le réglage inutilisable. Sous 1,0, la plupart des
//  deltas mis à l'échelle valent moins d'un pas entier ; les tronquer sans reporter
//  le reste ferait perdre TOUS les petits mouvements — la souris ST ne bougerait plus
//  du tout sur un déplacement lent, c'est-à-dire exactement au moment où le réglage
//  sert. Le test vérifie donc surtout la CONSERVATION : sur une longue série, le total
//  émis doit suivre le total attendu à moins d'un pas près.
// -----------------------------------------------------------------------------
static void testMouseScale() {
    std::printf("Souris émulée — mise à l'échelle du delta hôte\n");
    using neost::mousescale::step;

    // 1,0 = comportement d'AVANT le réglage : un pas hôte, un pas ST.
    { double a = 0; checkBool("1.0x : 10 px → 10", step(10, 1.0f, a) == 10, true); }

    // 0,25 : un delta de 1 px ne franchit un pas qu'au 4ᵉ appel — mais il le franchit.
    {
        double a = 0; int emis = 0, first = -1;
        for (int i = 0; i < 8; ++i) { const int d = step(1, 0.25f, a); emis += d; if (d && first < 0) first = i; }
        checkBool("0.25x : le 1er pas tombe au 4e appel", first == 3, true);
        checkBool("0.25x : 8 px hôte → 2 pas ST",         emis == 2,  true);
    }

    // CONSERVATION sur une longue série : rien ne se perd en route.
    {
        double a = 0; int emis = 0;
        for (int i = 0; i < 1000; ++i) emis += step(3, 0.3f, a);
        checkBool("0.3x : 3000 px → ~900 pas (±1)", std::abs(emis - 900) <= 1, true);
    }

    // Le signe est préservé, et la troncature va VERS ZÉRO des deux côtés.
    {
        double a = 0; int emis = 0;
        for (int i = 0; i < 10; ++i) emis += step(-2, 0.5f, a);
        checkBool("deltas négatifs : -20 px → -10 pas", emis == -10, true);
    }

    // Accélération : au-dessus de 1, un seul pixel doit déjà donner plusieurs pas.
    { double a = 0; checkBool("3.0x : 1 px → 3", step(1, 3.0f, a) == 3, true); }

    // Bornage DÉFENSIF de la vitesse — la même règle que la relecture du neost.cfg,
    // mais appliquée ici aussi : le curseur n'est pas le seul chemin vers ce champ.
    { double a = 0; checkBool("vitesse 0 bornée (souris jamais figée)", step(100, 0.0f, a) > 0, true); }
    { double a = 0; checkBool("vitesse énorme bornée", step(100, 1e9f, a) <= 100 * 4, true); }
    // NaN : `int(NaN)` est INDÉFINI et figerait la souris pour la session entière.
    { double a = 0; const int d = step(10, std::nanf(""), a);
      checkBool("vitesse NaN → défaut, pas de NaN propagé", d == 10 && std::isfinite(a), true); }
    { double a = std::nan(""); const int d = step(10, 1.0f, a);
      checkBool("reste NaN assaini", d == 10 && std::isfinite(a), true); }
}

// -----------------------------------------------------------------------------
//  « Machine gelée » : nommer le matériel absent plutôt que de laisser deviner.
//
//  Un jeu STE-only lancé sur un profil ST lit un registre qui n'existe pas, prend une
//  bus error, et le gestionnaire du TOS 1.x recharge un A7 corrompu → double faute →
//  CPU halté. Le comportement est FIDÈLE (Hatari halte à l'identique) — mais le bandeau
//  disait seulement « check the machine profile ». Rapport du 2026-09-02 sur Stardust :
//  l'utilisateur a légitimement conclu à un bug de NeoST. Le bandeau nomme désormais la
//  puce et le profil à choisir ; ce test garde la table de correspondance.
// -----------------------------------------------------------------------------
static void testMissingHwHint() {
    std::printf("Machine gelée — diagnostic du profil manquant\n");
    MissingHw m{};
    // Le cas rapporté : $FF8900 = son DMA STE, lu sur un profil ST.
    checkBool("$FF8900 sur ST → manquant", mmioNeedsBetterMachine(0xFF8900, MachineType::St, m), true);
    checkStr ("… nommé « STE DMA sound »", m.chip ? m.chip : "", "STE DMA sound");
    checkStr ("… profil conseillé",        m.needs ? m.needs : "", "ste");
    // L'adresse arrive parfois en 32 bits non tronqués : le 68000 n'a que 24 bits.
    checkBool("$00FF8900 (24 bits) idem", mmioNeedsBetterMachine(0x00FF8900, MachineType::St, m), true);
    // Sur STE le même registre EXISTE → aucun diagnostic.
    checkBool("$FF8900 sur STE → rien",     mmioNeedsBetterMachine(0xFF8900, MachineType::Ste, m),     false);
    checkBool("$FF8900 sur MegaSTE → rien", mmioNeedsBetterMachine(0xFF8900, MachineType::MegaSte, m), false);
    // Joypads STE, Microwire : même famille.
    checkBool("$FF9200 sur ST → manquant",  mmioNeedsBetterMachine(0xFF9200, MachineType::St, m), true);
    checkBool("$FF8922 sur ST → manquant",  mmioNeedsBetterMachine(0xFF8922, MachineType::St, m), true);
    // Blitter : absent du ST NU seulement — présent sur Mega ST comme sur STE.
    checkBool("blitter $FF8A00 sur ST → manquant", mmioNeedsBetterMachine(0xFF8A00, MachineType::St, m), true);
    checkBool("blitter sur MegaST → rien", mmioNeedsBetterMachine(0xFF8A00, MachineType::MegaSt, m), false);
    checkBool("blitter sur STE → rien",    mmioNeedsBetterMachine(0xFF8A00, MachineType::Ste, m),    false);
    // SCU / SCC : Mega STE seulement.
    checkBool("SCU $FF8E00 sur STE → manquant",   mmioNeedsBetterMachine(0xFF8E00, MachineType::Ste, m),     true);
    checkBool("SCU sur MegaSTE → rien",           mmioNeedsBetterMachine(0xFF8E00, MachineType::MegaSte, m), false);
    // Une adresse RAM ordinaire ne doit JAMAIS produire de conseil : le halt a alors
    // une autre cause, et pointer le profil enverrait l'utilisateur sur une fausse piste.
    checkBool("RAM $001000 → aucun conseil",  mmioNeedsBetterMachine(0x001000, MachineType::St, m), false);
    checkBool("vidéo $FF8240 → aucun conseil", mmioNeedsBetterMachine(0xFF8240, MachineType::St, m), false);
}

static void testConfigParser() {
    using namespace neost::appconfig;
    std::printf("neost.cfg (analyse / écriture)\n");

    // --- Listes d'appareils MIDI : le piège des PROFILS ------------------------
    // parseConfigLine est partagé avec les profils nommés, qui s'appliquent PAR-DESSUS
    // la config courante. Une clé scalaire remplace ; la première version de ces listes
    // utilisait des clés RÉPÉTABLES et faisait donc un push_back — charger un profil
    // DUPLIQUAIT chaque appareil, et deux lignes de même nom partagent le même
    // ImGui::PushID, donc leurs cases se pilotaient l'une l'autre. Bug rapporté par
    // l'utilisateur (« Circuit Tracks est en double »), corrigé en revenant à
    // « une clé, une ligne, une affectation ».
    {
        Config c;
        // Nom TORDU à dessein : les trois caractères de l'encodage. Un nom d'appareil
        // peut contenir n'importe quoi — c'est l'objection qui avait fait écarter un
        // séparateur au départ, et que l'échappement lève.
        const std::string tordu = "Ac;me | Piano \\ 2";
        c.midiOutDevices = {{tordu, 0x000F, "111"}, {"Circuit Tracks MIDI", 0x0200, "-901138834"}};
        c.midiInDevices  = {{tordu, 3, "222"}, {"Circuit Tracks MIDI", 0, ""}};

        std::ostringstream os;
        writeConfigKeys(os, c, true);
        const std::string texte = os.str();

        auto rejouer = [&](Config& dst) {
            std::istringstream is(texte);
            std::string ligne;
            while (std::getline(is, ligne)) parseConfigLine(dst, ligne);
        };

        Config relu;
        rejouer(relu);
        checkBool("cfg MIDI : 2 sorties relues", relu.midiOutDevices.size() == 2, true);
        checkBool("cfg MIDI : nom à séparateurs préservé",
                  !relu.midiOutDevices.empty() && relu.midiOutDevices[0].name == tordu, true);
        checkBool("cfg MIDI : masque de canaux préservé",
                  relu.midiOutDevices.size() == 2 && relu.midiOutDevices[1].channels == 0x0200, true);
        checkBool("cfg MIDI : canal forcé préservé",
                  !relu.midiInDevices.empty() && relu.midiInDevices[0].channel == 3, true);
        // L'identifiant DOIT survivre : c'est lui qui distingue deux appareils du même
        // modèle d'un lancement à l'autre. Un négatif est un uid CoreMIDI valide.
        checkBool("cfg MIDI : identifiant unique préservé",
                  relu.midiOutDevices.size() == 2 && relu.midiOutDevices[1].uid == "-901138834", true);
        checkBool("cfg MIDI : identifiant vide préservé vide",
                  relu.midiInDevices.size() == 2 && relu.midiInDevices[1].uid.empty(), true);

        // LE test du bug : rejouer les MÊMES lignes par-dessus une config DÉJÀ remplie,
        // exactement ce que fait loadProfileInto. Les listes doivent être REMPLACÉES.
        rejouer(relu);
        checkBool("cfg MIDI : un profil REMPLACE, il n'ajoute pas (sorties)",
                  relu.midiOutDevices.size() == 2, true);
        checkBool("cfg MIDI : un profil REMPLACE, il n'ajoute pas (entrées)",
                  relu.midiInDevices.size() == 2, true);

        // Un profil qui ne déclare AUCUN appareil doit pouvoir vider le studio : la
        // ligne est donc écrite même vide (une clé absente laisserait l'ancien en place).
        Config vide;
        std::ostringstream ov;
        writeConfigKeys(ov, vide, true);
        checkBool("cfg MIDI : la liste vide est écrite (un profil peut effacer)",
                  ov.str().find("midi_out_devices=") != std::string::npos, true);
        std::istringstream iv(ov.str());
        std::string l2;
        while (std::getline(iv, l2)) parseConfigLine(relu, l2);
        checkBool("cfg MIDI : et elle efface bien", relu.midiOutDevices.empty(), true);

        // FORMAT HÉRITÉ (clés répétables du 2026-08-29 au matin) : encore LU pour qu'un
        // neost.cfg d'avant ne perde pas son studio en silence. Ce chemin n'était couvert
        // NULLE PART — et c'est justement celui qu'on ne remarque pas quand il casse,
        // puisque son seul symptôme est une liste d'appareils vide au démarrage suivant.
        // Il porte aussi l'invariant « uid vide » : l'ancien format n'en a pas, et c'est
        // App::midiLearnUids() qui le renseignera à la première ouverture réussie.
        {
            Config anc;
            for (const char* l : { "midi_out_device=Circuit Tracks",
                                   "midi_out_channels=1,2,10",
                                   "midi_in_device=Keystation 49",
                                   "midi_in_channel=3" })
                parseConfigLine(anc, l);
            checkBool("cfg MIDI hérité : la sortie est relue",
                      anc.midiOutDevices.size() == 1 &&
                      anc.midiOutDevices[0].name == "Circuit Tracks", true);
            checkBool("cfg MIDI hérité : son masque de canaux suit",
                      anc.midiOutDevices.size() == 1 &&
                      anc.midiOutDevices[0].channels == 0x0203, true);
            checkBool("cfg MIDI hérité : l'entrée est relue, avec son canal",
                      anc.midiInDevices.size() == 1 &&
                      anc.midiInDevices[0].name == "Keystation 49" &&
                      anc.midiInDevices[0].channel == 3, true);
            checkBool("cfg MIDI hérité : uid VIDE des deux côtés (l'ancien format n'en a pas)",
                      anc.midiOutDevices[0].uid.empty() && anc.midiInDevices[0].uid.empty(), true);
        }
    }

    // CRLF : un fichier passé par Windows, un éditeur ou un partage réseau. Le '\r'
    // collé faisait tomber CHAQUE clé sur son défaut, en silence (machine ST demandée,
    // STE démarrée) — et saveConfig réécrivait ensuite les '\r', rendant la panne
    // définitive. Le garde-fou est dans parseConfigLine ; il n'était testé nulle part.
    {
        Config c;
        parseConfigLine(c, "machine=megaste\r");
        parseConfigLine(c, "mem=4m\r");
        parseConfigLine(c, "rom=roms/tos206fr.img\r");
        checkStr("CRLF : machine",  c.machine, "megaste");
        checkStr("CRLF : mem",      c.mem,     "4m");
        checkStr("CRLF : rom",      c.rom,     "roms/tos206fr.img");
    }
    // Espaces de fin : même traitement que le '\r'.
    {
        Config c;
        parseConfigLine(c, "machine=ste   ");
        checkStr("espaces de fin", c.machine, "ste");
    }
    // Le rognage est exposé (trimConfigLine) parce que le HEADLESS lit le même
    // fichier avec son propre lecteur (--from-cfg). Tant que la règle y était
    // recopiée, elle a divergé : le headless ne retirait que le '\r', donc un
    // « machine=st » suivi d'une espace repartait en silence sur la machine par
    // défaut (STE) — le défaut même que le rognage GUI corrige. Une définition,
    // deux appelants ; ces cas verrouillent la définition.
    {
        auto trimmed = [](std::string s) { trimConfigLine(s); return s; };
        checkStr("trim : CRLF",            trimmed("machine=st\r"),     "machine=st");
        checkStr("trim : espaces",         trimmed("machine=st   "),    "machine=st");
        checkStr("trim : tabulations",     trimmed("machine=st\t\t"),   "machine=st");
        checkStr("trim : mélange CR+blancs", trimmed("mem=4m \t\r"),    "mem=4m");
        checkStr("trim : rien à retirer",  trimmed("mem=4m"),           "mem=4m");
        checkStr("trim : ligne vide",      trimmed(""),                 "");
        checkStr("trim : que des blancs",  trimmed("  \t\r"),           "");
        // Un blanc INTERNE appartient à la valeur (un chemin peut en contenir).
        checkStr("trim : blanc interne gardé",
                 trimmed("disk=disks/my game.st "), "disk=disks/my game.st");
    }
    // Valeurs hostiles bornées : une deadzone > 0.95 rendait le menu de la borne
    // incontrôlable, un volume hors [0,1] saturait la sortie.
    {
        Config c;
        parseConfigLine(c, "joydeadzone=9.5");
        checkBool("deadzone aberrante ramenée au défaut", c.joydeadzone == 0.30f, true);
        parseConfigLine(c, "joydeadzone=nan");
        checkBool("deadzone NaN ramenée au défaut",       c.joydeadzone == 0.30f, true);
        parseConfigLine(c, "volume=42");
        checkBool("volume borné à 1.0",                   c.volume == 1.0f, true);
        parseConfigLine(c, "volume=-3");
        checkBool("volume borné à 0.0",                   c.volume == 0.0f, true);
        // ⚠ NaN : toute comparaison est fausse, donc « if (v<0) … if (v>1) … » le
        // laissait passer INTACT. Un gain NaN fige les filtres du YM (hpfX1_/hpfY0_)
        // → plus aucun son jusqu'au reset, même après avoir remis le curseur.
        parseConfigLine(c, "volume=nan");
        checkBool("volume NaN ramené au défaut",          c.volume == 1.0f, true);
        // Faders du mixeur (page Sound, curseur 0..200 %) : mêmes garde-fous.
        parseConfigLine(c, "mix_ym=nan");
        checkBool("mix_ym NaN ramené au défaut",          c.mixYm == 1.0f, true);
        parseConfigLine(c, "mix_dac=nan");
        checkBool("mix_dac NaN ramené au défaut",         c.mixDac == 1.0f, true);
        parseConfigLine(c, "mix_dma=1e30");
        checkBool("mix_dma aberrant borné à 2.0",         c.mixDma == 2.0f, true);
        parseConfigLine(c, "mix_drive=-1");
        checkBool("mix_drive négatif borné à 0.0",        c.mixDrive == 0.0f, true);
        parseConfigLine(c, "mix_mt32=1.5");
        checkBool("mix_mt32 valide conservé",             c.mixMt32 == 1.5f, true);
    }
    // Une clé inconnue (fichier d'une version future) ne doit RIEN faire.
    {
        Config c;
        parseConfigLine(c, "cleQuiNExistePas=1");
        parseConfigLine(c, "");
        checkStr("clé inconnue ignorée", c.machine, Config().machine);
    }
    // ALLER-RETOUR : tout ce que writeConfigKeys écrit doit se relire à l'identique.
    // C'est le contrat qui lie les deux fonctions, et le seul moyen d'attraper une
    // clé ajoutée d'un seul côté.
    {
        Config a;
        a.rom = "roms/tos206fr.img"; a.disk = "disks/x.st"; a.diskb = "disks/y.st";
        a.cart = "carts/z.img";      a.gemdos = "/srv/gemdos"; a.acsi = "hd/c.img";
        a.machine = "megaste";       a.mem = "4m";  a.mono = true;  a.fpu = true;
        a.modem = true;              a.ethernec = true;
        a.netusbee = true;           a.slirp = true;
        a.joyport = 0;               a.joymap = "GUID1:0,GUID2:x";
        a.joydeadzone = 0.42f;       a.fastfdc = true;
        a.volume = 0.5f;             a.audioLatencyMs = 40;  a.driveSound = false;
        a.autoZoom = false;          a.crt = true;
        a.crtParams.scanlines = 0.75f;

        std::ostringstream out;
        writeConfigKeys(out, a, /*full=*/true);
        Config b;
        std::istringstream in(out.str());
        std::string line;
        while (std::getline(in, line)) parseConfigLine(b, line);

        checkStr("aller-retour rom",      b.rom,     a.rom);
        checkStr("aller-retour gemdos",   b.gemdos,  a.gemdos);
        checkStr("aller-retour machine",  b.machine, a.machine);
        checkStr("aller-retour mem",      b.mem,     a.mem);
        checkStr("aller-retour joymap",   b.joymap,  a.joymap);
        checkBool("aller-retour mono",     b.mono     == a.mono,     true);
        checkBool("aller-retour fpu",      b.fpu      == a.fpu,      true);
        checkBool("aller-retour modem",    b.modem    == a.modem,    true);
        checkBool("aller-retour ethernec", b.ethernec == a.ethernec, true);
        checkBool("aller-retour netusbee", b.netusbee == a.netusbee, true);
        checkBool("aller-retour slirp",    b.slirp    == a.slirp,    true);
        checkBool("aller-retour joyport",  b.joyport  == a.joyport,  true);
        checkBool("aller-retour deadzone", b.joydeadzone == a.joydeadzone, true);
        checkBool("aller-retour fastfdc",  b.fastfdc  == a.fastfdc,  true);
        checkBool("aller-retour volume",   b.volume   == a.volume,   true);
        checkBool("aller-retour latence",  b.audioLatencyMs == a.audioLatencyMs, true);
        checkBool("aller-retour drivesound", b.driveSound == a.driveSound, true);
        checkBool("aller-retour autozoom", b.autoZoom == a.autoZoom, true);
        checkBool("aller-retour crt",      b.crt      == a.crt,      true);
        checkBool("aller-retour crt_scanlines", b.crtParams.scanlines == a.crtParams.scanlines, true);
    }
    // Un PROFIL (full=false) ne doit PAS emporter l'horloge ni la disposition de
    // l'interface : charger un profil ne déplace pas les fenêtres de l'utilisateur.
    {
        Config a; a.rtc = "1,2,3,4,5,6,26"; a.rtcSaved = 12345; a.uiVersion = 7;
        a.romDirs.push_back("/srv/roms");
        std::ostringstream out;
        writeConfigKeys(out, a, /*full=*/false);
        const std::string s = out.str();
        checkBool("profil sans rtc=",          s.find("rtc=") == std::string::npos, true);
        checkBool("profil sans uiVersion=",    s.find("uiVersion=") == std::string::npos, true);
        checkBool("profil sans kiosk_romdir=", s.find("kiosk_romdir=") == std::string::npos, true);
        checkBool("profil avec machine=",      s.find("machine=") != std::string::npos, true);
    }
    // Nom de fichier de profil : il devient un chemin, donc aucun séparateur ni
    // « .. » ne doit survivre (un profil nommé « ../../etc/x » écrirait hors du
    // dossier des profils).
    {
        checkStr("profil : séparateurs retirés",   profileFileName("a/b\\c"), "abc");
        checkStr("profil : caractères Windows interdits retirés",
                 profileFileName("a:b*c?d\"e<f>g|h"), "abcdefgh");
        checkStr("profil : « ../../x » ne remonte nulle part",
                 profileFileName("../../x"), "x");
        checkStr("profil : points et espaces de tête/queue rognés",
                 profileFileName("  .mon profil.  "), "mon profil");
        checkBool("profil : nom borné à 64 caractères",
                  profileFileName(std::string(200, 'a')).size() == 64, true);
    }
    // L'horloge sérialisée doit se relire (sinon la date repart à zéro à chaque boot).
    {
        Rtc::DateTime dt{};
        checkBool("rtc= bien formée",   parseRtcConfig("1,2,3,4,5,6,26", dt), true);
        checkBool("rtc= valeurs lues",  dt.sec == 1 && dt.min == 2 && dt.hour == 3
                                     && dt.wday == 4 && dt.day == 5 && dt.month == 6
                                     && dt.year == 26, true);
        checkBool("rtc= tronquée refusée", parseRtcConfig("1,2,3", dt), false);
    }
    // --- Sensibilité de la souris émulée --------------------------------------
    // Réglage AJOUTÉ le 2026-09-02 pour les souris hôtes à haute résolution (le ST
    // compte les crans d'une souris ~200 dpi ; une souris à 3200 dpi envoie 16 fois
    // plus de pas et le pointeur GEM devient inutilisable). Trois choses à garder :
    // qu'il fasse l'aller-retour dans neost.cfg, qu'il soit BORNÉ à la relecture, et
    // que NaN ne passe pas — `int(NaN)` est indéfini, et un delta NaN figerait la
    // souris ST définitivement (même leçon que mixGain, cf. AppConfig.cpp).
    {
        Config c; c.mouseSpeed = 0.35f; c.desktopMouse = true;
        std::ostringstream os; writeConfigKeys(os, c, true);
        Config relu; std::istringstream is(os.str()); std::string l;
        while (std::getline(is, l)) parseConfigLine(relu, l);
        checkBool("mousespeed : aller-retour", std::fabs(relu.mouseSpeed - 0.35f) < 1e-4f, true);
        checkBool("desktop_mouse : aller-retour", relu.desktopMouse, true);

        Config d;
        parseConfigLine(d, "mousespeed=nan");
        checkBool("mousespeed=nan → défaut 1.0", d.mouseSpeed == 1.0f, true);
        parseConfigLine(d, "mousespeed=0");
        checkBool("mousespeed=0 borné (souris jamais figée)", d.mouseSpeed >= 0.05f, true);
        parseConfigLine(d, "mousespeed=-3");
        checkBool("mousespeed négatif borné", d.mouseSpeed >= 0.05f, true);
        parseConfigLine(d, "mousespeed=999");
        checkBool("mousespeed énorme borné", d.mouseSpeed <= 4.0f, true);
        // Défaut du champ : 1,0 = comportement d'AVANT ce réglage, à l'identique.
        Config neuf;
        checkBool("défaut = 1.0 (comportement d'origine)", neuf.mouseSpeed == 1.0f, true);
    }
}

// -----------------------------------------------------------------------------
//  CartridgeKey — machines d'état des clés Steinberg (logique pure, pas de bus).
//  Les valeurs épinglées ont été produites par cette implémentation (aucune séquence
//  de référence n'est publiée) : elles gardent la TRANSCRIPTION des équations à
//  l'identique, elles ne prouvent pas la clé. Les propriétés, elles, sont connues.
// -----------------------------------------------------------------------------
// Libellé RAM de la barre d'état : calculé depuis les octets. La chaîne de
// ternaires qu'il remplace affichait « 4 MB » pour 2176k/2560k (et pour toute
// valeur qu'elle ne connaissait pas).
static void testRamDisplay() {
    std::printf("Libellé RAM de la barre d'état\n");
    checkStr("256k",  ramDisplayName(parseRamBytes("256k")),  "256 KB");
    checkStr("512k",  ramDisplayName(parseRamBytes("512k")),  "512 KB");
    checkStr("1m",    ramDisplayName(parseRamBytes("1m")),    "1 MB");
    checkStr("2m",    ramDisplayName(parseRamBytes("2m")),    "2 MB");
    checkStr("2176k", ramDisplayName(parseRamBytes("2176k")), "2176 KB");
    checkStr("2560k", ramDisplayName(parseRamBytes("2560k")), "2.5 MB");
    checkStr("4m",    ramDisplayName(parseRamBytes("4m")),    "4 MB");
    // Alias « 2.5m » ramené à l'identifiant du bouton radio de la page Memory.
    checkStr("alias 2.5m canonique", ramLabel(parseRamBytes("2.5m")), "2560k");
}

static void testCartridgeKey() {
    std::printf("CartridgeKey (clés Steinberg /ROM3)\n");
    {   // Clé noire : registres à 0 au reset, le motif %11011000 sur A8..A1 ramène à 0.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Cubase2);
        checkBool("cubase2 : sortie au reset = $00 (octet fort)", k.cartRead(0xFB0000, true) == 0x00, true);
        checkBool("cubase2 : octet faible = $FF", k.cartRead(0xFB0001, false) == 0xFF, true);
        k.udsCycle(0x1000);   // fetch quelconque, A8..A1 = 0 : aucun terme vrai → $FF
        checkBool("cubase2 : un cycle /UDS hors clé cadence la PAL", k.state() == 0xFF, true);
        for (uint32_t a = 1; a < 40; ++a) k.udsCycle(0x1000 + a * 2);
        k.udsCycle(0xFB01B0);   // A8..A1 = %11011000 → tous les termes vrais → 0
        checkBool("cubase2 : motif de reset logiciel → état 0", k.state() == 0, true);
        // Épinglage : 8 lectures « défi » successives A8..A1 = i*37, même défi deux fois.
        std::string got;
        for (int i = 0; i < 8; ++i) {
            const uint32_t a = 0xFB0000 | ((uint32_t(i * 37) & 0xFF) << 1);
            char b[8]; std::snprintf(b, sizeof b, "%02X", k.cartRead(a, true)); got += b;
            k.udsCycle(a);
        }
        checkStr("cubase2 : séquence épinglée", got, "00FF00FF00000000");
    }
    {   // Clé rouge : 1 bit (D8) ; D9-D15 à 1 ; horloge sur chaque accès $FBxxxx seulement.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Cubase3);
        checkBool("cubase3 : sortie au reset = $FE (D8=0, D9-15=1)", k.cartRead(0xFB0000, true) == 0xFE, true);
        const uint16_t s0 = k.state();
        k.udsCycle(0x1234); k.udsCycle(0xFB0100);
        checkBool("cubase3 : /UDS seul ne cadence pas", k.state() == s0, true);
        std::string got;
        for (int i = 0; i < 48; ++i) {
            const uint32_t a = 0xFB0000 | (((i * 7) & 1) ? 0x100u : 0u);   // A8 pseudo-aléatoire
            got += (k.cartRead(a, true) & 1) ? '1' : '0';
        }
        checkStr("cubase3 : 48 bits épinglés", got, "000011100010001000100010001000100010001000100010");
    }
    {   // Clé Notator : armement par /ROM4 à $FA00EA (STER), données par /ROM3 ensuite.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
        checkBool("notator : reset → 0, désarmée, crochet /UDS demandé", k.state() == 0 && !k.armed() && k.wantsUds(), true);
        k.udsCycle(0x1002);    // désarmée : UDS cadence les données (A1=1 → D14 ← 1)
        checkBool("notator : désarmée, UDS cadence (A1 → D14)", k.state() == 0x40, true);
        k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);   // accès d'armement : STER → 0 puis FEEDB1
        checkBool("notator : $FA00EA arme et remet à 0", k.armed() && k.state() == 0, true);
        k.udsCycle(0x1002); k.udsCycle(0x1008);
        checkBool("notator : armée, UDS sans effet", k.state() == 0, true);
        const uint8_t v = k.cartRead(0xFB0002, true);       // /ROM3 descend : horloge AVANT lecture
        checkBool("notator : lecture $FB0002 = état APRÈS horloge (D14)", v == 0x40 && k.state() == 0x40, true);
        checkBool("notator : octet faible $FF", k.cartRead(0xFB0003, false) == 0xFF, true);
        k.rom4Listen(0xFA0000, true); k.udsCycle(0xFA0000);   // tout autre accès /ROM4 désarme
        checkBool("notator : accès /ROM4 hors STER désarme", !k.armed(), true);
        k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);
        std::string got;
        for (int i = 0; i < 12; ++i) {
            const uint32_t a = 0xFB0000 | ((uint32_t(i * 53) & 0xFF) << 1);
            char b[8]; std::snprintf(b, sizeof b, "%02X", k.cartRead(a, true)); got += b;
        }
        checkStr("notator : séquence épinglée", got, "FF00FD0000400100FF000040");
        k.cartRead(0xFB00EA, true);
        checkBool("notator : $FB00EA (STER) remet à 0", k.state() == 0, true);
        k.cartRead(0xFB0002, true);                          // D14 ← 1 (A1)
        k.cartRead(0xFB0014, true);                          // A4·A2 : reset asynchrone de D9 ; A1=0
        checkBool("notator : reset asynchrone D9 sous A4·A2", (k.state() & 0x02) == 0, true);
    }
    {   // Oracle de rejeu : une trace écrite par setLog se rejoue sans écart ; une
        // trace altérée signale le premier écart (c'est ce qu'une capture matérielle
        // divergente produira).
        const char* path = "neost_selftest_key.trace";
        {
            CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
            FILE* f = std::fopen(path, "w"); k.setLog(f);
            k.udsCycle(0x1002);
            k.rom4Listen(0xFA00EA, true); k.udsCycle(0xFA00EA);
            for (int i = 0; i < 12; ++i) { const uint32_t a = 0xFB0000 | ((uint32_t(i * 53) & 0xFF) << 1); k.cartRead(a, true); k.udsCycle(a); }
            k.setLog(nullptr); std::fclose(f);
        }
        CartridgeKey k; k.setModel(CartridgeKey::Model::Notator);
        char err[128];
        checkBool("replay : trace propre → 0 écart", k.replay(path, err, sizeof err) == 0, true);
        if (FILE* f = std::fopen(path, "a")) { std::fputs("R3 00 12\n", f); std::fclose(f); }
        const int n = k.replay(path, err, sizeof err);
        checkBool("replay : ligne altérée → 1 écart localisé", n == 1 && std::strstr(err, "expected 12") != nullptr, true);
        checkBool("replay : fichier absent → -1", k.replay("/nonexistent/x.trace", err, sizeof err) == -1, true);
        std::remove(path);
    }
    {   // Auto : premier accès avec A7..A1 = 0 → rouge ; ≠ 0 → noire.
        CartridgeKey k; k.setModel(CartridgeKey::Model::Auto);
        checkBool("auto : crochet /UDS demandé tant que non tranché", k.wantsUds(), true);
        k.cartRead(0xFB0100, true);
        checkBool("auto : A7..A1 = 0 → clé rouge", k.cartRead(0xFB0000, true) == 0xFE && !k.wantsUds(), true);
        CartridgeKey k2; k2.setModel(CartridgeKey::Model::Auto);
        k2.cartRead(0xFB00B4, true);
        checkBool("auto : A7..A1 ≠ 0 → clé noire", k2.wantsUds(), true);
    }
}

// -----------------------------------------------------------------------------
//  PortDevices — un périphérique par port (protocoles Steem SSE / WinUAE).
// -----------------------------------------------------------------------------
static void testPortDevices() {
    std::printf("PortDevices (clés joystick / série, DAC, boutons)\n");
    using PD = PortDevices;
    checkBool("ids : aller-retour sur tous les périphériques et ports", [] {
        for (int i = 0; i < int(PD::Device::Count); ++i)
            if (PD::fromId(PD::id(PD::Device(i))) != PD::Device(i)) return false;
        for (int i = 0; i < int(PD::Port::Count); ++i) { bool ok; if (PD::portFromId(PD::portId(PD::Port(i)), &ok) != PD::Port(i) || !ok) return false; }
        bool ok = true; PD::portFromId("bogus", &ok);
        return PD::fromId("bogus") == PD::Device::None && PD::fromId(nullptr) == PD::Device::None && !ok;
    }(), true);
    {   // Connecteurs : une clé joystick entre dans les deux ports DE-9, pas ailleurs.
        PD d;
        checkBool("fits : leaderboard → joy0/joy1 oui, rs232 non",
                  d.plug(PD::Port::Joy0, PD::Device::LeaderBoard) && d.plug(PD::Port::Joy1, PD::Device::LeaderBoard)
                  && !d.plug(PD::Port::Rs232, PD::Device::LeaderBoard) && !d.plug(PD::Port::Joy0, PD::Device::Bat2), true);
        checkBool("defaultPort : leaderboard → joy1, cricket → joy0, prosound → printer",
                  PD::defaultPort(PD::Device::LeaderBoard) == PD::Port::Joy1 && PD::defaultPort(PD::Device::Cricket) == PD::Port::Joy0
                  && PD::defaultPort(PD::Device::ProSound) == PD::Port::Printer, true);
        d.unplugAll(); checkBool("unplugAll", !d.any(), true);
    }
    {   // Leader Board dans le port 1 : haut+bas, port 0 intact ; dans le port 0 : l'inverse.
        PD d; d.plug(PD::Port::Joy1, PD::Device::LeaderBoard);
        uint8_t j0 = 0x80, j1 = 0x04; d.onJoystick(j0, j1);
        checkBool("leaderboard (joy1) : joy1 |= haut+bas, joy0 intact", j0 == 0x80 && j1 == 0x07, true);
        PD w; w.plug(PD::Port::Joy0, PD::Device::LeaderBoard);
        j0 = 0; j1 = 0; w.onJoystick(j0, j1);
        checkBool("leaderboard (mauvais port) : joy0 seulement", j0 == 0x03 && j1 == 0, true);
    }
    {   // Cricket + Leader Board coexistent ; oscillateur indépendant par port.
        PD d; d.plug(PD::Port::Joy0, PD::Device::Cricket); d.plug(PD::Port::Joy1, PD::Device::Rugby);
        uint8_t j0 = 0, j1 = 0; d.onJoystick(j0, j1); const uint8_t a0 = j0, a1 = j1;
        j0 = j1 = 0; d.onJoystick(j0, j1);
        checkBool("cricket/rugby : oscillent entre $C et $D sur leur port", a0 + j0 == 0xC + 0xD && a1 + j1 == 0xC + 0xD, true);
    }
    {   // B.A.T. II : CTS (bit2) forcé à 0, le reste intact.
        PD d; d.plug(PD::Port::Rs232, PD::Device::Bat2);
        uint8_t v = 0xFF; d.gpipRead(v, 0);
        checkBool("bat2 : GPIP bit2 (CTS) = 0", v == 0xFB && d.hasSerial(), true);
    }
    {   // Music Master : DCD (bit1) suit DTR avec 200 cycles de retard.
        Mfp mfp; PD d; d.plug(PD::Port::Rs232, PD::Device::MusicMaster);
        d.onPortA(0x10, 1000, mfp);
        uint8_t v = 0xFD; d.gpipRead(v, 1100); const bool early = (v & 0x02) == 0;
        v = 0xFD; d.gpipRead(v, 1300);         const bool late  = (v & 0x02) != 0;
        d.onPortA(0x00, 2000, mfp);
        v = 0xFF; d.gpipRead(v, 2100); const bool early2 = (v & 0x02) != 0;
        v = 0xFF; d.gpipRead(v, 2300); const bool late2  = (v & 0x02) == 0;
        checkBool("musicmaster : DTR → DCD retardé de 200 cycles", early && late && early2 && late2, true);
    }
    {   // Jeanne d'Arc : DCD assertée quand (RTS|DTR) décroît sans s'annuler.
        Mfp mfp; PD d; d.plug(PD::Port::Rs232, PD::Device::JeanneDArc);
        d.onPortA(0x18, 0, mfp); const bool a = (mfp.read8(0xFFFA01) & 0x02) != 0;
        d.onPortA(0x08, 0, mfp); const bool b = (mfp.read8(0xFFFA01) & 0x02) == 0;
        d.onPortA(0x00, 0, mfp); const bool c = (mfp.read8(0xFFFA01) & 0x02) != 0;
        checkBool("jeannedarc : DCD = !(new && new < old)", a && b && c, true);
    }
    {   // Multiface : bouton → GPIP7 à 0 jusqu'à la VBL, quel que soit le moniteur.
        Mfp mfp; mfp.setColorMonitor(true);
        PD d; d.plug(PD::Port::CartButton, PD::Device::Multiface);
        const bool before = (mfp.read8(0xFFFA01) & 0x80) != 0;
        d.pressButton(mfp);
        const bool during = (mfp.read8(0xFFFA01) & 0x80) == 0 && d.buttonPressed();
        d.onVbl(mfp);
        const bool after = (mfp.read8(0xFFFA01) & 0x80) != 0 && !d.buttonPressed();
        checkBool("multiface : GPIP7 bas pendant l'appui, relâché à la VBL", before && during && after, true);
        PD n; n.plug(PD::Port::Rs232, PD::Device::Bat2); n.pressButton(mfp);
        checkBool("bouton : sans effet sans bouton branché", !n.buttonPressed(), true);
        checkBool("reset : relâche le bouton, garde le périphérique", [&] { d.pressButton(mfp); d.reset(); return !d.buttonPressed() && d.hasButton(); }(), true);
    }
}

// -----------------------------------------------------------------------------
//  DongleTable — disks/dongles.txt (motif → branchement).
// -----------------------------------------------------------------------------
static void testDongleTable() {
    std::printf("DongleTable (disks/dongles.txt)\n");
    int bad = 0;
    const auto rules = neost::parseDongleTable(neost::defaultDongleTable(), &bad);
    checkBool("table livrée : toutes les lignes valides", bad == 0 && rules.size() >= 15, true);
    const auto hits = neost::matchDongleRules(rules, "disks/st/Leader Board (1986)(Access)[cr].st");
    checkBool("leader board → joy1:leaderboard", hits.size() == 1 && !hits[0].cart
              && hits[0].port == PortDevices::Port::Joy1 && hits[0].dev == PortDevices::Device::LeaderBoard, true);
    const auto n = neost::matchDongleRules(rules, "/x/NOTATOR_SL_3.21.msa");
    checkBool("NOTATOR (majuscules) → cart:notator", n.size() == 1 && n[0].cart && n[0].key == CartridgeKey::Model::Notator, true);
    checkBool("sans correspondance → vide", neost::matchDongleRules(rules, "disks/diskA.st").empty(), true);
    const auto r2 = neost::parseDongleTable("# c\n  foo = joy0:leaderboard # ok\nbar = rs232:leaderboard\nbaz\nqux = cart:nope\n", &bad);
    checkBool("analyse : commentaires, espaces, lignes invalides comptées", r2.size() == 1 && bad == 3 && r2[0].pattern == "foo", true);
}

// -----------------------------------------------------------------------------
//  YM2149 — file d'écritures horodatées : le DOMAINE des registres empilés par
//  write8 et la garde qui le vérifie au chargement doivent coïncider. Ni machine
//  ni ROM : un YM2149 nu et une StateArchive suffisent.
// -----------------------------------------------------------------------------
static void testYmEventDomain() {
    std::printf("YM2149 (domaine des écritures horodatées)\n");
    // Aller-retour d'un événement portant `reg`, comme le fait Machine::saveState.
    auto roundTrip = [](uint8_t reg) {
        YM2149 psg;
        psg.reset();
        psg.setCycleClock([] { return int64_t(0); });   // active le modèle « push »
        if (reg == 15) psg.setPortBDac(true);           // DAC Pro Sound branché
        psg.write8(0xFF8800, reg);
        psg.write8(0xFF8802, 0x40);
        std::vector<uint8_t> buf;
        { StateArchive s = StateArchive::saver(buf); psg.serialize(s); }
        YM2149 back;
        StateArchive l = StateArchive::loader(buf.data(), buf.size());
        back.serialize(l);
        return l.ok();
    };
    checkBool("R13 (enveloppe) : etat accepte", roundTrip(13), true);
    // ⚠ Régression : le DAC Pro Sound a élargi le push de write8 à R15, mais la
    // garde de relecture exigeait encore reg < 14 — un save-state LÉGITIME pris avec
    // une écriture R15 en file était refusé au chargement (« invariant rejected »).
    checkBool("R15 sous DAC Pro Sound : etat accepte", roundTrip(15), true);
}

// -----------------------------------------------------------------------------
//  ACIA MIDI 6850 — TDRE (« émetteur prêt ») doit tomber à CHAQUE écriture de
//  données, que l'IRQ d'émission soit armée ou non : c'est le seul frein d'un
//  pilote qui SCRUTE le statut. Ni machine ni ROM : un Mfp, un Scheduler, une ACIA.
// -----------------------------------------------------------------------------
static void testMidiTdre() {
    std::printf("ACIA MIDI (TDRE, frein de l'émetteur)\n");
    Mfp mfp; Scheduler sched; MidiAcia midi(mfp);
    midi.setScheduler(&sched);
    constexpr uint32_t kCtrl = 0xFFFC04, kData = 0xFFFC06;
    auto tdre = [&] { return (midi.read8(kCtrl) >> 1) & 1; };

    // `cr` = valeur du registre de contrôle (bits 5-6 = 01 → IRQ d'émission armée).
    auto ecritUnOctet = [&](uint8_t cr) {
        midi.reset();
        midi.write8(kCtrl, 0x03);            // master reset
        midi.write8(kCtrl, cr);
        midi.write8(kData, 0x90);
        return tdre();
    };
    checkBool("TIE armé : TDRE tombe",   ecritUnOctet(0x35) == 0, true);
    // ⚠ Régression : TDRE n'était modélisé QUE sous TIE. Un pilote scrutant le
    // statut (le cas de Notator, cf. Hatari midi.c:243) le voyait éternellement à
    // 1 et émettait sans jamais attendre — 1000 octets collés au même cycle.
    checkBool("TIE coupé : TDRE tombe aussi", ecritUnOctet(0x15) == 0, true);
    checkBool("RIE seul : TDRE tombe aussi",  ecritUnOctet(0x95) == 0, true);

    // Le master reset, LUI, remet bien l'émetteur au repos.
    midi.reset(); midi.write8(kCtrl, 0x15); midi.write8(kData, 0x90);
    checkBool("émetteur occupé après écriture", tdre() == 0, true);
    midi.write8(kCtrl, 0x03);
    checkBool("master reset : émetteur au repos", tdre() == 1, true);

    // Un pilote qui scrute n'enchaîne pas les octets sans attendre.
    midi.reset(); midi.write8(kCtrl, 0x03); midi.write8(kCtrl, 0x15);
    int envoyes = 0;
    for (int i = 0; i < 50 && tdre(); ++i) { midi.write8(kData, uint8_t(i)); ++envoyes; }
    checkBool("scrutation : 1 seul octet avant l'attente", envoyes == 1, true);

    // Débordement du récepteur : le 6850 perd le NOUVEL octet et garde l'ancien
    // (acia.c, état STOP_BIT). Jeter le plus ancien faisait disparaître le STATUS
    // d'un message MIDI et laissait des octets de données orphelins.
    midi.setLoopback(true);
    midi.reset(); midi.write8(kCtrl, 0x03); midi.write8(kCtrl, 0x15);
    // Octets déjà reçus : le transport TX est gardé par midi_serial_logic.
    midi.receiveExternal(0x90); midi.receiveExternal(0x3C); midi.receiveExternal(0x40);
    const uint8_t o1 = midi.read8(kData), o2 = midi.read8(kData);
    checkBool("débordement : le status $90 survit", o1 == 0x90, true);
    checkBool("débordement : relire RDR garde $90", o2 == 0x90, true);
}

// -----------------------------------------------------------------------------
//  APPAREILS HOMONYMES — désigner sans ambiguïté
//
//  Deux machines du MÊME MODÈLE branchées ensemble (le cas d'un studio : deux
//  claviers identiques) portent EXACTEMENT le même nom d'affichage. Les désigner par
//  le nom seul, c'est ouvrir deux fois le même appareil et laisser l'autre muet.
//  L'index n'est pas la solution non plus : il se renumérote au débranchement d'un
//  voisin, et la config se mettrait à piloter la mauvaise machine.
//
//  On ne peut PAS éprouver ça sur le matériel du développeur (un seul appareil
//  branché) : c'est exactement pourquoi l'appariement est une fonction PURE.
// -----------------------------------------------------------------------------
static void testMidiHomonymes() {
    std::printf("Appareils MIDI homonymes (appariement nom + identifiant)\n");
    using neost::midi::Endpoint;
    using neost::midi::Wanted;
    using neost::midi::matchEndpoints;
    using neost::midi::displayLabel;

    // Deux claviers identiques + un troisième appareil.
    const std::vector<Endpoint> have = {
        {"Piano 88", "111"}, {"Piano 88", "222"}, {"Circuit Tracks MIDI", "333"}};

    // (1) Deux entrées de config par le NOM SEUL (config d'avant les identifiants) :
    //     elles doivent tomber sur DEUX points distincts, pas deux fois le premier.
    {
        const auto pick = matchEndpoints({{"Piano 88", ""}, {"Piano 88", ""}}, have);
        checkBool("homonymes : deux entrées, deux points distincts",
                  pick.size() == 2 && pick[0] == 0 && pick[1] == 1, true);
    }

    // (2) L'IDENTIFIANT prime, et il prime AVANT que le nom ne serve : sans cette
    //     priorité, l'entrée par nom raflerait le point que l'autre réclamait par son
    //     identifiant. Ici le second veut explicitement « 111 » — il doit l'obtenir.
    {
        const auto pick = matchEndpoints({{"Piano 88", ""}, {"Piano 88", "111"}}, have);
        checkBool("homonymes : l'identifiant passe avant le nom",
                  pick.size() == 2 && pick[1] == 0 && pick[0] == 1, true);
    }

    // (3) LE bénéfice de l'identifiant : l'ordre d'énumération change au rebranchement
    //     (débrancher un voisin renumérote tout). Le nom seul suivrait la position ;
    //     l'identifiant, lui, retrouve la bonne machine.
    {
        const std::vector<Endpoint> reordonne = {
            {"Circuit Tracks MIDI", "333"}, {"Piano 88", "222"}, {"Piano 88", "111"}};
        const auto pick = matchEndpoints({{"Piano 88", "111"}}, reordonne);
        checkBool("homonymes : l'identifiant survit à la renumérotation",
                  pick.size() == 1 && pick[0] == 2, true);
    }

    // (4) Appareil ABSENT : -1, et surtout il ne vole pas le point d'un autre.
    {
        const auto pick = matchEndpoints({{"Fantôme", "999"}, {"Piano 88", ""}}, have);
        checkBool("homonymes : l'absent rend -1", pick.size() == 2 && pick[0] == -1, true);
        checkBool("homonymes : et ne vole rien au suivant", pick.size() == 2 && pick[1] == 0, true);
    }

    // (5) Étiquettes : le suffixe n'apparaît QUE s'il y a ambiguïté — sinon on
    //     alourdirait toutes les listes pour rien.
    checkBool("homonymes : étiquette suffixée #1", displayLabel(have, 0) == "Piano 88 #1", true);
    checkBool("homonymes : étiquette suffixée #2", displayLabel(have, 1) == "Piano 88 #2", true);
    checkBool("homonymes : nom unique laissé nu",
              displayLabel(have, 2) == "Circuit Tracks MIDI", true);

    // (6) APPRENTISSAGE des identifiants — le scénario même de la fonctionnalité :
    //     deux homonymes branchés, config d'avant (uid vides). Chaque ligne doit
    //     apprendre l'identifiant de SON point, jamais deux fois le même. Le bug
    //     chassé le 2026-08-30 : les deux lignes apprenaient « 111 », et au
    //     débranchement du premier clavier le masque de canaux de la seconde
    //     pilotait le mauvais appareil (le repli par nom masquait l'erreur tant
    //     que les deux restaient branchés).
    {
        using neost::midi::learnUids;
        // openEndpoints() rend les points OUVERTS dans l'ordre de la config :
        // l'entrée 0 a été appariée au point « 111 », la 1 au point « 222 ».
        const std::vector<Endpoint> open = {{"Piano 88", "111"}, {"Piano 88", "222"}};
        std::vector<Wanted> cfgw = {{"Piano 88", ""}, {"Piano 88", ""}};
        checkBool("apprentissage : il a bien appris", learnUids(cfgw, open), true);
        checkBool("apprentissage : la 1re ligne prend le 1er point", cfgw[0].uid == "111", true);
        checkBool("apprentissage : la 2e ligne prend le 2e — jamais deux fois le même",
                  cfgw[1].uid == "222", true);
    }
    // (6b) Un uid DÉJÀ connu est réservé : la ligne vide apprend l'AUTRE point,
    //      même si le point connu vient en premier dans la liste ouverte.
    {
        using neost::midi::learnUids;
        const std::vector<Endpoint> open = {{"Piano 88", "111"}, {"Piano 88", "222"}};
        std::vector<Wanted> cfgw = {{"Piano 88", "111"}, {"Piano 88", ""}};
        learnUids(cfgw, open);
        checkBool("apprentissage : l'uid déjà connu est réservé", cfgw[1].uid == "222", true);
    }
    // (6c) UN seul homonyme branché pour DEUX lignes vides : la seconde reste vide —
    //      on ne sait pas lequel des deux claviers c'est, et apprendre faux est pire
    //      que ne rien apprendre.
    {
        using neost::midi::learnUids;
        const std::vector<Endpoint> open = {{"Piano 88", "111"}};
        std::vector<Wanted> cfgw = {{"Piano 88", ""}, {"Piano 88", ""}};
        learnUids(cfgw, open);
        checkBool("apprentissage : un seul point, une seule ligne servie",
                  cfgw[0].uid == "111" && cfgw[1].uid.empty(), true);
    }

    // (7) GARDE de la reconnexion à 1 Hz : countMatchable dit ce qu'une re-tentative
    //     OUVRIRAIT. Le bug chassé le 2026-08-30 : la garde comparait au CONFIGURÉ,
    //     donc un appareil durablement absent déclenchait la re-tentative à chaque
    //     seconde — or re-tenter ferme tout d'abord, ce qui paniquait (All Notes
    //     Off) le synthé encore branché une fois par seconde.
    {
        using neost::midi::countMatchable;
        // Config : deux appareils. Branché : UN seul. Ouvert : ce seul-là (1).
        const std::vector<Wanted> cfgw = {{"Piano 88", "111"}, {"Fantôme", "999"}};
        const std::vector<Endpoint> plugged = {{"Piano 88", "111"}};
        checkBool("garde 1 Hz : l'absent durable ne re-déclenche pas (1 ouvrable = 1 ouvert)",
                  countMatchable(cfgw, plugged) == 1, true);
        // Le Fantôme revient : 2 ouvrables ≠ 1 ouvert → la re-tentative se justifie.
        const std::vector<Endpoint> back = {{"Piano 88", "111"}, {"Fantôme", "999"}};
        checkBool("garde 1 Hz : le retour de l'appareil re-déclenche (2 ouvrables)",
                  countMatchable(cfgw, back) == 2, true);
    }

    // (8) LONGUEUR D'UN MESSAGE COURT — la table de vérité de winmm.
    //     MM_MIM_DATA livre un message empaqueté dans un mot de 32 bits SANS dire
    //     combien d'octets comptent. Pousser les trois aveuglément ajoute une donnée
    //     fantôme derrière tout message à un seul paramètre, et le running status
    //     l'avale en silence : la note suivante part sur la mauvaise hauteur, sans
    //     rien dans les journaux. Éprouvé ici, donc sur macOS et Linux aussi, où
    //     aucun clavier n'est branché en CI.
    {
        using neost::midi::shortMessageLength;
        checkBool("winmm : Note On = 3 octets",         shortMessageLength(0x90) == 3, true);
        checkBool("winmm : Note Off = 3 octets",        shortMessageLength(0x85) == 3, true);
        checkBool("winmm : Control Change = 3 octets",  shortMessageLength(0xB0) == 3, true);
        checkBool("winmm : Pitch Bend = 3 octets",      shortMessageLength(0xEF) == 3, true);
        checkBool("winmm : Program Change = 2 octets",  shortMessageLength(0xC3) == 2, true);
        checkBool("winmm : Channel Pressure = 2 octets", shortMessageLength(0xD0) == 2, true);
        checkBool("winmm : Song Position = 3 octets",   shortMessageLength(0xF2) == 3, true);
        checkBool("winmm : Song Select = 2 octets",     shortMessageLength(0xF3) == 2, true);
        checkBool("winmm : Tune Request = 1 octet",     shortMessageLength(0xF6) == 1, true);
        // Temps réel : un octet, et c'est ce qui porte l'horloge d'un séquenceur.
        checkBool("winmm : horloge $F8 = 1 octet",      shortMessageLength(0xF8) == 1, true);
        checkBool("winmm : Start $FA = 1 octet",        shortMessageLength(0xFA) == 1, true);
        checkBool("winmm : Active Sensing $FE = 1 octet", shortMessageLength(0xFE) == 1, true);
        // Une DONNÉE n'est pas un message : 0 dit « ne rien pousser ». Sans ce cas,
        // un mot mal formé ferait entrer un octet orphelin dans le flux fusionné.
        checkBool("winmm : une donnée n'est pas un message", shortMessageLength(0x40) == 0, true);
        checkBool("winmm : $00 n'est pas un message",   shortMessageLength(0x00) == 0, true);
    }
}

// -----------------------------------------------------------------------------
//  MIDI IN MATÉRIEL — l'ACIA tire les octets à 31250 bauds
//
//  Un appareil branché livre ses octets sur le thread de CoreMIDI/ALSA, quand ça lui
//  chante. La PREMIÈRE version les poussait dans l'ACIA une fois par trame, ce qui
//  plafonnait l'entrée à 2 octets/trame (le 6850 n'en tient pas plus) — mesuré
//  1,76 o/trame, soit ~143 o/s en mono contre 3125 o/s sur un vrai câble : un accord
//  de dix notes mettait 0,2 s à entrer. C'est désormais l'ACIA qui TIRE, sur son
//  horloge série (Scheduler::MIDI_RX, 2560 cycles = 10 bits à 31250 bauds).
//
//  On éprouve ici le chemin de production complet — MidiInHost + MidiAcia + Scheduler
//  — sans le moindre appareil branché.
// -----------------------------------------------------------------------------
static void testMidiInJitter() {
    std::printf("MIDI IN hôte (horloge série → ACIA)\n");
    constexpr uint32_t kCtrl = 0xFFFC04, kData = 0xFFFC06;
    constexpr int64_t kByte = 2560;          // 10 bits à 31250 bauds

    struct Rig {
        Mfp mfp; Scheduler sched; MidiAcia midi{mfp}; MidiInHost in;
        Rig() {
            midi.setScheduler(&sched);
            sched.setCallback(Scheduler::MIDI_RX, [this] { midi.onRxPace(); });
            midi.setRxSource([this](uint8_t& b) { return in.tryPop(b); });
        }
        bool rdrf() { return (midi.read8(kCtrl) & 0x01) != 0; }
    };

    // (1) CADENCE : un accord de 9 octets n'entre pas d'un bloc, il entre au débit du
    //     câble — ni plus vite (ce serait irréel), ni plus lentement (c'était le bug).
    {
        Rig r;
        const uint8_t accord[9] = {0x90, 0x3C, 0x40, 0x90, 0x40, 0x40, 0x90, 0x43, 0x40};
        r.in.pushForTest(0, accord, sizeof accord);
        // Le flux fusionné COMPACTE le running status : les deux notes suivantes
        // n'ont pas à répéter $90. Même musique, 7 octets au lieu de 9 — et c'est
        // ce que ferait un vrai boîtier de fusion.
        const std::vector<uint8_t> attendu = {0x90, 0x3C, 0x40, 0x40, 0x40, 0x43, 0x40};
        std::vector<uint8_t> recu;
        int64_t fin = -1;
        for (int i = 0; i < 200 && recu.size() < attendu.size(); ++i) {
            r.sched.runTo(r.sched.now() + kByte / 4);      // pas fin : 4 par octet
            while (r.rdrf()) {                              // le ST vide la puce
                recu.push_back(r.midi.read8(kData));
                if (recu.size() == attendu.size()) fin = r.sched.now();
            }
        }
        checkBool("accord : les 3 notes arrivent", recu == attendu, true);
        // 7 octets = 7 périodes série au plus tôt. La borne haute interdit le retour
        // du plafond par trame : à 2 octets/trame il aurait fallu ~4 trames, soit
        // plus de 400 000 cycles, contre ~18 000 ici.
        checkBool("accord : pas plus vite que le câble", fin >= 7 * kByte, true);
        checkBool("accord : pas plus lentement non plus", fin >= 0 && fin < 10 * kByte, true);
    }

    // (2) DÉBIT SOUTENU : 200 octets doivent entrer en 200 périodes série (3125 o/s),
    //     pas en 100 trames. C'est la propriété que l'ancienne version violait.
    {
        Rig r;
        // De VRAIS messages : les octets passent par un décodeur, qui jette à raison
        // les données orphelines. 66 note-on = 198 octets ; le running status du flux
        // fusionné les compacte à 2 octets après le premier, d'où 134 octets attendus.
        std::vector<uint8_t> flot;
        for (int i = 0; i < 66; ++i) {
            flot.push_back(0x90); flot.push_back(uint8_t(1 + i)); flot.push_back(0x40);
        }
        r.in.pushForTest(0, flot.data(), flot.size());
        const std::size_t attendu = 3 + 65 * 2;   // 1er message entier, puis running status
        std::size_t lus = 0;
        const int64_t budget = 150 * kByte;               // le flux + marge
        while (r.sched.now() < budget) {
            r.sched.runTo(r.sched.now() + kByte / 4);
            while (r.rdrf()) { r.midi.read8(kData); ++lus; }
        }
        checkBool("débit : le flux entier en ~autant de périodes série", lus == attendu, true);
        checkBool("débit : rien perdu côté hôte", r.in.dropped() == 0, true);
    }

    // (3) OVERRUN DU MATÉRIEL : si le ST ne lit pas, c'est le 6850 qui perd — et il
    //     perd le NOUVEL octet en gardant l'ancien (acia.c, état STOP_BIT). Jeter le
    //     plus ancien ferait disparaître le STATUS d'un message et laisserait des
    //     données orphelines. L'hôte, lui, ne retient plus rien pour l'éviter.
    {
        Rig r;
        // Un message complet, puis de quoi noyer la puce (running status : 2 octets
        // par note supplémentaire). Le flux commence par 90 3C : seul le RDR
        // contenant $90 peut survivre quand aucune lecture CPU ne le vide.
        std::vector<uint8_t> flot = {0x90, 0x3C, 0x40};
        for (int i = 0; i < 20; ++i) { flot.push_back(0x90); flot.push_back(uint8_t(0x40 + i)); flot.push_back(0x40); }
        r.in.pushForTest(0, flot.data(), flot.size());
        // ⚠ Par PETITS PAS : une source ne tire qu'UNE fois par appel à runTo (modèle
        // Hatari, cf. le masque `fired` du Scheduler). Un runTo d'un bloc de 20
        // périodes ne ferait donc entrer qu'un octet — ce serait mesurer le test, pas
        // la puce. L'émulateur avance lui aussi par quanta courts.
        for (int i = 0; i < 20 * 4; ++i) r.sched.runTo(r.sched.now() + kByte / 4);
        // Personne ne lit : un seul RDR, les caractères suivants sont perdus.
        const uint8_t o1 = r.midi.read8(kData), o2 = r.midi.read8(kData);
        checkBool("overrun 6850 : le premier octet survit", o1 == 0x90, true);
        checkBool("overrun 6850 : RDR persistant relu",    o2 == 0x90, true);
    }

    // (3b) SAVE-STATE CROISÉ : un état sauvé SANS appareil MIDI IN, rechargé (F7)
    //      pendant qu'un clavier est branché. Le Scheduler restaure ses échéances —
    //      celles de l'état sauvé, où MIDI_RX est éteint — mais la source hôte, elle,
    //      est toujours là : sans réarmement au chargement, plus rien ne tire et
    //      l'entrée MIDI meurt en silence (bug chassé le 2026-08-30).
    {
        // L'état « d'avant » : une machine sans appareil (source jamais branchée).
        std::vector<uint8_t> etat;
        {
            Mfp mfp0; Scheduler sched0; MidiAcia midi0{mfp0};
            midi0.setScheduler(&sched0);
            StateArchive sv = StateArchive::saver(etat);
            sched0.serialize(sv); midi0.serialize(sv);     // même ordre que Machine
        }
        // La machine « d'aujourd'hui » : un clavier branché, un octet en attente.
        Rig r;
        const uint8_t note[3] = {0x90, 0x3C, 0x40};
        r.in.pushForTest(0, note, 3);
        StateArchive ld = StateArchive::loader(etat.data(), etat.size());
        r.sched.serialize(ld); r.midi.serialize(ld);       // F7 : sched puis midi
        std::size_t lus = 0;
        for (int i = 0; i < 5 * 4; ++i) {
            r.sched.runTo(r.sched.now() + kByte / 4);
            while (r.rdrf()) { r.midi.read8(kData); ++lus; }
        }
        checkBool("save-state croisé : l'entrée MIDI survit au chargement", lus == 3, true);
    }

    // (4) TAMPON HÔTE PLEIN : au-delà de kMaxJitter, c'est le MESSAGE NEUF ENTIER
    //     qui tombe — jamais un fragment, qui laisserait des octets orphelins dans
    //     le flux. Le tampon absorbe une rafale livrée plus vite que 31250 bauds,
    //     il ne stocke pas indéfiniment.
    {
        MidiInHost in3;
        std::vector<uint8_t> trop = {0x90, 0x01, 0x40};      // marqueur du PLUS ANCIEN
        for (int i = 0; i < 2000; ++i) {
            trop.push_back(0x90); trop.push_back(0x02); trop.push_back(0x40);
        }
        in3.pushForTest(0, trop.data(), trop.size());
        checkBool("tampon plein : le trop-plein est compté", in3.dropped() > 0, true);
        uint8_t a = 0, b = 0;
        checkBool("tampon plein : c'est le NEUF qui tombe, pas l'ancien",
                  in3.tryPop(a) && in3.tryPop(b) && a == 0x90 && b == 0x01, true);
    }

    // (5) FUSION : deux claviers joués ENSEMBLE. Leurs octets arrivent entrelacés ;
    //     ce qui entre dans le ST doit être des messages INTACTS. C'est toute la
    //     raison d'être d'un boîtier de fusion — entrelacer des octets bruts
    //     donnerait « 90 90 3C 40 40 40 », du charabia.
    {
        MidiInHost in;
        const uint8_t a1[2] = {0x90, 0x3C};                  // clavier A : message ENTAMÉ
        const uint8_t b1[3] = {0x90, 0x40, 0x40};            // clavier B : message COMPLET
        const uint8_t a2[1] = {0x40};                        // clavier A : sa fin
        in.pushForTest(0, a1, 2);
        in.pushForTest(1, b1, 3);
        in.pushForTest(0, a2, 1);
        std::vector<uint8_t> flux; uint8_t b = 0;
        while (in.tryPop(b)) flux.push_back(b);
        // B sort en entier (il a fini le premier), puis A. Le statut de A est OMIS :
        // il est identique à celui déjà posé — running status, et c'est correct.
        const std::vector<uint8_t> attendu = {0x90, 0x40, 0x40, 0x3C, 0x40};
        checkBool("fusion : les messages restent intacts et ordonnés", flux == attendu, true);
    }

    // (6) CANALISATION : deux claviers émettent tous deux sur le canal 1. Sans
    //     réécriture du canal, un séquenceur ne peut PAS les séparer et tout finit
    //     sur la même piste. Forcés sur 1 et 2, ils deviennent enregistrables sur
    //     deux pistes simultanément.
    {
        MidiInHost in;
        const uint8_t note[3] = {0x90, 0x3C, 0x40};
        in.pushForTest(0, note, 3, 1);                       // clavier A → canal 1
        in.pushForTest(1, note, 3, 2);                       // clavier B → canal 2
        std::vector<uint8_t> flux; uint8_t b = 0;
        while (in.tryPop(b)) flux.push_back(b);
        const std::vector<uint8_t> attendu = {0x90, 0x3C, 0x40, 0x91, 0x3C, 0x40};
        checkBool("canalisation : deux sources, deux canaux distincts", flux == attendu, true);
    }

    // (7) TEMPS RÉEL : l'horloge MIDI ($F8) peut tomber N'IMPORTE OÙ, y compris
    //     entre deux messages en running status, et ne doit PAS casser ce running
    //     status — sinon la note suivante serait lue comme un message neuf.
    {
        MidiInHost in;
        const uint8_t s[7] = {0x90, 0x3C, 0x40, 0xF8, 0x3E, 0x40, 0xF8};
        in.pushForTest(0, s, 7);
        std::vector<uint8_t> flux; uint8_t b = 0;
        while (in.tryPop(b)) flux.push_back(b);
        const std::vector<uint8_t> attendu = {0x90, 0x3C, 0x40, 0xF8, 0x3E, 0x40, 0xF8};
        checkBool("temps réel : l'horloge passe sans casser le running status",
                  flux == attendu, true);
    }

    // (8) SYSEX À LA BORNE : la troncature vaut pour le MESSAGE ENTIER, $F0 et $F7
    //     compris — jamais kMaxSysex + 1. L'invariant n'est pas décoratif :
    //     l'encodeur ALSA de sortie est dimensionné à cette taille EXACTE
    //     (snd_midi_event_new(4096)), et un message d'un octet de plus y échouait à
    //     l'encodage, donc tombait en silence sous Linux (bug hunt du 2026-08-30 —
    //     le contenu était borné AVANT l'ajout du $F7 final : 4 097 octets).
    //     Testé sur le Parser nu : c'est lui qui porte l'invariant.
    {
        neost::midi::Parser prs;
        std::vector<uint8_t> msg;
        const auto emit = [&msg](const uint8_t* m, int len) { msg.assign(m, m + len); };
        prs.byte(0xF0, emit);
        for (int i = 0; i < 6000; ++i) prs.byte(uint8_t(i & 0x7F), emit);
        prs.byte(0xF7, emit);
        checkBool("sysex borné : le message tronqué ne dépasse JAMAIS kMaxSysex",
                  !msg.empty() && msg.size() <= neost::midi::Parser::kMaxSysex, true);
        checkBool("sysex borné : et il reste un SysEx bien formé ($F0 … $F7)",
                  !msg.empty() && msg.front() == 0xF0 && msg.back() == 0xF7, true);
        // Un SysEx COURT traverse intact — la borne ne doit toucher que l'excès.
        msg.clear();
        const uint8_t court[5] = {0xF0, 0x43, 0x12, 0x00, 0xF7};
        for (uint8_t b2 : court) prs.byte(b2, emit);
        checkBool("sysex borné : un dump court passe intact",
                  msg == std::vector<uint8_t>(court, court + 5), true);
    }
}

static void testIkbdTdre() {
    std::printf("ACIA clavier (TDRE, frein de l'émetteur)\n");
    Mfp mfp; Scheduler sched; Ikbd ikbd(mfp);
    ikbd.setScheduler(&sched);
    constexpr uint32_t kCtrl = 0xFFFC00, kData = 0xFFFC02;
    auto tdre = [&] { return (ikbd.read8(kCtrl) >> 1) & 1; };

    auto ecritUnOctet = [&](uint8_t cr) {
        ikbd.write8(kCtrl, 0x03);            // master reset
        ikbd.write8(kCtrl, cr);
        ikbd.write8(kData, 0x12);            // commande IKBD quelconque
        return tdre();
    };
    checkBool("TIE armé : TDRE tombe",        ecritUnOctet(0x20) == 0, true);
    // ⚠ Régression : TDRE n'était modélisé QUE sous TIE — la boucle « btst #1 »
    // d'Ikbdws passait sans attendre, tous les octets d'une commande au même cycle.
    checkBool("TIE coupé : TDRE tombe aussi", ecritUnOctet(0x96) == 0, true);
    ikbd.write8(kCtrl, 0x03);
    checkBool("master reset : émetteur au repos", tdre() == 1, true);
}

// -----------------------------------------------------------------------------
//  RTC RP5C15 — la seconde se compte en CYCLES. Elle doit valoir une seconde de la
//  base de temps de la MACHINE (trame × Hz), pas une constante : sinon l'horloge
//  dérive contre le reste, et un logiciel qui attend « une seconde » en comptant
//  ses trames retombe juste avant le tic. Ni machine ni ROM : une horloge factice.
// -----------------------------------------------------------------------------
static void testRtcSecond() {
    std::printf("RTC RP5C15 (une seconde = base de temps machine)\n");
    const int64_t kSec = 160256 * 50;        // PAL 50 Hz : cycles d'une trame × Hz
    int64_t clk = 0;
    Rtc rtc;
    rtc.setClock([&clk] { return clk; });
    rtc.setSecondCycles(kSec);
    Rtc::DateTime dt{};
    dt.sec = 59; dt.min = 59; dt.hour = 23; dt.wday = 6; dt.day = 31; dt.month = 12; dt.year = 99;
    rtc.setDateTime(dt);
    (void)rtc.getDateTime();                 // amorce la phase du diviseur sur clk = 0

    clk = kSec - 1;
    checkBool("juste avant la seconde : pas de tic", rtc.getDateTime().sec == 59, true);
    // ⚠ Régression : avec la constante figée d'avant (8021248 au lieu de 8012800),
    // ce tic tombait 8448 cycles TROP TARD — la cartouche Atari Field Service (test
    // L, Mega ST) attendait une seconde comptée en trames et lisait « C1 clock
    // increment error ».
    clk = kSec;
    const Rtc::DateTime a = rtc.getDateTime();
    checkBool("à la seconde pile : tic",          a.sec == 0 && a.min == 0 && a.hour == 0, true);
    checkBool("débordement calendaire complet",   a.day == 1 && a.month == 1 && a.year == 0, true);
    checkBool("jour de semaine avancé",           a.wday == 0, true);
    // Trois secondes machine = exactement trois tics (pas d'arrondi qui en perde un).
    clk = 4 * kSec;
    checkBool("3 s de plus = 3 tics", rtc.getDateTime().sec == 3, true);

    // TIMER EN = bit3 du registre MODE ($FFFC3B) : à 0 le COMPTEUR est arrêté.
    // Vérifié sur machine réelle émulée : TOS 1.02 et EmuTOS écrivent $9 puis $8 au
    // boot — ils ne basculent que le bit0 (banque) et laissent TIMER EN posé. Seule
    // la cartouche Atari Field Service le coupe, pour figer l'heure avant relecture.
    constexpr uint32_t kMode = 0xFFFC21 + 13 * 2;
    rtc.write8(kMode, 0x00);                       // compteur arrêté
    const int fige = rtc.getDateTime().sec;
    clk += 5 * kSec;
    checkBool("TIMER EN coupé : l'heure est figée", rtc.getDateTime().sec == fige, true);
    rtc.write8(kMode, 0x08);                       // compteur réarmé
    checkBool("TIMER EN réarmé : retenue délivrée", rtc.getDateTime().sec == fige + 1, true);
    clk += kSec;
    checkBool("TIMER EN réarmé : l'heure repart", rtc.getDateTime().sec == fige + 2, true);
}

// -----------------------------------------------------------------------------
//  A29 — « puce nue + Scheduler » étendu au BLITTER.
//
//  POURQUOI. Le blitter n'était couvert que par deux étalons PIXEL (blitter_timer,
//  blitter_hog). Quand l'un rougit, le verdict est « 3 400 px divergents à
//  (112,57) » et l'enquête commence. Une table de vérité, elle, dit « le masque de
//  fin ne s'applique plus au premier mot » ou « la tranche non-hog fait 63 accès au
//  lieu de 64 ». Étage manquant entre la logique pure et le pixel.
//
//  Ni machine ni ROM : un Bus (512 Ko de RAM), un Scheduler, un Blitter.
// -----------------------------------------------------------------------------
namespace {

constexpr uint32_t BLT = 0xFF8A00;      // base des registres blitter
constexpr uint32_t SRC = 0x020000;      // zones de travail en RAM
constexpr uint32_t DST = 0x030000;

struct BlitRig {
    Bus       bus{512u * 1024u};
    Scheduler sched;
    Blitter   blit{bus};

    BlitRig() {
        blit.reset();
        blit.setScheduler(&sched);
        // Ce que Machine fait pour de vrai (Machine.cpp) : sans ce câblage,
        // l'échéance Scheduler::BLITTER est POSÉE mais personne ne la sert — le
        // blit non-hog ne démarre jamais et le test mesurerait du vide.
        sched.setCallback(Scheduler::BLITTER, [this] { blit.onSlice(); });
    }

    void poke16(uint32_t addr, uint16_t v) { bus.write16(addr, v); }
    uint16_t peek16(uint32_t addr) { return bus.read16(addr); }

    // Programme un blit « une ligne, xCount mots » et le LANCE (l'écriture de
    // $FF8A3C est ce qui démarre le matériel, cf. Blitter::write16).
    void program(int xCount, int yCount, int hop, int lop,
                 uint16_t em1 = 0xFFFF, uint16_t em2 = 0xFFFF, uint16_t em3 = 0xFFFF,
                 uint8_t skew = 0, int srcInc = 2, int dstInc = 2) {
        blit.write16(BLT + 0x20, uint16_t(srcInc));      // src X inc
        blit.write16(BLT + 0x22, 2);                     // src Y inc
        blit.write32(BLT + 0x24, SRC);
        blit.write16(BLT + 0x28, em1);
        blit.write16(BLT + 0x2A, em2);
        blit.write16(BLT + 0x2C, em3);
        blit.write16(BLT + 0x2E, uint16_t(dstInc));      // dst X inc
        blit.write16(BLT + 0x30, 2);                     // dst Y inc
        blit.write32(BLT + 0x32, DST);
        blit.write16(BLT + 0x36, uint16_t(xCount));
        blit.write16(BLT + 0x38, uint16_t(yCount));
        blit.write8 (BLT + 0x3A, uint8_t(hop));
        blit.write8 (BLT + 0x3B, uint8_t(lop));
        blit.write16(BLT + 0x3D - 1, uint16_t((0xC0 << 8) | skew));   // BUSY+HOG puis skew
        sched.runTo(sched.nextDue());                 // PRE_START existe aussi en HOG
    }
};

}  // namespace

static void testBlitterTruthTable() {
    std::printf("Blitter (puce nue + Scheduler : table de vérité)\n");

    // --- 1. Copie simple : HOP=2 (source), LOP=3 (D=S), masques pleins ---------
    {
        BlitRig r;
        for (int i = 0; i < 4; ++i) r.poke16(SRC + i * 2, uint16_t(0x1000 + i));
        for (int i = 0; i < 5; ++i) r.poke16(DST + i * 2, 0xAAAA);   // le 5e est le témoin
        r.program(4, 1, /*hop*/2, /*lop*/3);
        checkBool("copie 4 mots : BUSY retombé (HOG)", r.blit.busy(), false);
        checkHex ("copie 4 mots : mot 0", r.peek16(DST + 0), 0x1000);
        checkHex ("copie 4 mots : mot 3", r.peek16(DST + 6), 0x1003);
        checkHex ("copie 4 mots : mot 4 INTACT", r.peek16(DST + 8), 0xAAAA);
    }

    // --- 2. Masques de fin : em1 au PREMIER mot, em3 au DERNIER ---------------
    // Le masque protège les bits de la destination qu'il laisse à 0 — c'est ce qui
    // permet de blitter un rectangle sans écraser les colonnes voisines.
    {
        BlitRig r;
        for (int i = 0; i < 3; ++i) { r.poke16(SRC + i * 2, 0xFFFF);
                                      r.poke16(DST + i * 2, 0x0000); }
        r.program(3, 1, 2, 3, /*em1*/0x00FF, /*em2*/0xFFFF, /*em3*/0xFF00);
        checkHex("masque em1 (1er mot)",     r.peek16(DST + 0), 0x00FF);
        checkHex("masque em2 (mot milieu)",  r.peek16(DST + 2), 0xFFFF);
        checkHex("masque em3 (dernier mot)", r.peek16(DST + 4), 0xFF00);
    }

    // --- 3. HOP et LOP : les quatre coins de la table -------------------------
    {
        BlitRig r;
        r.poke16(SRC, 0xF0F0);
        r.poke16(DST, 0xFF00);
        r.program(1, 1, /*hop*/0, /*lop*/3);            // HOP=0 : source = tout à 1
        checkHex("HOP=0 (uns) + LOP=3 (S)", r.peek16(DST), 0xFFFF);

        r.poke16(DST, 0xFF00);
        r.program(1, 1, 2, /*lop*/0);                   // LOP=0 : zéro
        checkHex("LOP=0 (zéro)", r.peek16(DST), 0x0000);

        r.poke16(DST, 0xFF00);
        r.program(1, 1, 2, /*lop*/15);                  // LOP=15 : uns
        checkHex("LOP=15 (uns)", r.peek16(DST), 0xFFFF);

        r.poke16(DST, 0xFF00);
        r.program(1, 1, 2, /*lop*/6);                   // LOP=6 : S XOR D
        checkHex("LOP=6 (S XOR D)", r.peek16(DST), 0x0FF0);

        r.poke16(DST, 0xFF00);
        r.program(1, 1, 2, /*lop*/1);                   // LOP=1 : S AND D
        checkHex("LOP=1 (S AND D)", r.peek16(DST), 0xF000);
    }

    // --- 4. Plusieurs lignes : les incréments Y sont appliqués ----------------
    {
        BlitRig r;
        for (int l = 0; l < 3; ++l) r.poke16(SRC + l * 2, uint16_t(0x2000 + l));
        r.program(1, 3, 2, 3);
        checkHex("3 lignes : ligne 0", r.peek16(DST + 0), 0x2000);
        checkHex("3 lignes : ligne 2", r.peek16(DST + 4), 0x2002);
        checkBool("3 lignes : BUSY retombé", r.blit.busy(), false);
    }

    // --- 5. La tranche NON-HOG fait exactement 64 accès bus -------------------
    // C'est LE chiffre que les étalons pixel ne savent pas dire. HOP=2/LOP=3 avec
    // masques pleins = 2 accès par mot (lecture source + écriture destination),
    // donc 32 mots par tranche. 40 mots demandés → la 1re tranche en fait 32, le
    // blitter reste BUSY, et la suivante finit le travail.
    {
        BlitRig r;
        for (int i = 0; i < 40; ++i) { r.poke16(SRC + i * 2, uint16_t(0x3000 + i));
                                       r.poke16(DST + i * 2, 0x0000); }
        r.blit.write16(BLT + 0x20, 2);  r.blit.write16(BLT + 0x22, 2);
        r.blit.write32(BLT + 0x24, SRC);
        r.blit.write16(BLT + 0x28, 0xFFFF); r.blit.write16(BLT + 0x2A, 0xFFFF);
        r.blit.write16(BLT + 0x2C, 0xFFFF);
        r.blit.write16(BLT + 0x2E, 2);  r.blit.write16(BLT + 0x30, 2);
        r.blit.write32(BLT + 0x32, DST);
        r.blit.write16(BLT + 0x36, 40); r.blit.write16(BLT + 0x38, 1);
        r.blit.write8 (BLT + 0x3A, 2);  r.blit.write8 (BLT + 0x3B, 3);
        r.blit.write16(BLT + 0x3C, 0x8000);        // BUSY sans HOG → tranches

        // Sans avancer l'ordonnanceur, rien n'a encore tourné : la 1re tranche est
        // DATÉE (phase PRE_START), elle n'est pas immédiate.
        checkBool("non-hog : BUSY armé, blit non fini", r.blit.busy(), true);
        checkHex ("non-hog : rien n'a encore été copié", r.peek16(DST), 0x0000);

        r.sched.runTo(64);                          // franchit l'échéance +8
        checkHex ("non-hog : mot 0 copié après la 1re tranche", r.peek16(DST), 0x3000);
        checkHex ("non-hog : mot 31 copié (32e mot)", r.peek16(DST + 31 * 2), 0x301F);
        checkHex ("non-hog : mot 32 PAS encore copié", r.peek16(DST + 32 * 2), 0x0000);
        checkBool("non-hog : BUSY toujours armé", r.blit.busy(), true);

        r.blit.onSlice();                           // tranche suivante
        checkHex ("non-hog : mot 39 copié après la 2e", r.peek16(DST + 39 * 2), 0x3027);
        checkBool("non-hog : BUSY retombé à la fin", r.blit.busy(), false);
    }
}

// -----------------------------------------------------------------------------
//  A29 — « puce nue + Scheduler » étendu au SON DMA (STE).
//
//  Les registres $FF8909/0B/0D (compteur d'adresse VIVANT) sont au cœur de la
//  divergence Hatari encore ouverte sur la quantification HBL du refill FIFO ; ils
//  n'étaient couverts par AUCUN test — un étalon pixel ne voit pas le son, et le
//  dump WAV ne dit pas OÙ le pointeur en était. Ni machine ni ROM : un Bus, un
//  Scheduler, un DmaSound.
// -----------------------------------------------------------------------------
namespace {

constexpr uint32_t SND = 0xFF8900;

struct SndRig {
    Bus       bus{512u * 1024u};
    Scheduler sched;
    DmaSound  snd{bus};

    SndRig() { snd.setScheduler(&sched); }

    void setStart(uint32_t a) {
        snd.write8(SND + 0x03, uint8_t(a >> 16));
        snd.write8(SND + 0x05, uint8_t(a >> 8));
        snd.write8(SND + 0x07, uint8_t(a));
    }
    void setEnd(uint32_t a) {
        snd.write8(SND + 0x0F, uint8_t(a >> 16));
        snd.write8(SND + 0x11, uint8_t(a >> 8));
        snd.write8(SND + 0x13, uint8_t(a));
    }
    uint32_t counter() {
        return (uint32_t(snd.read8(SND + 0x09)) << 16)
             | (uint32_t(snd.read8(SND + 0x0B)) << 8)
             |  uint32_t(snd.read8(SND + 0x0D));
    }
};

}  // namespace

static void testDmaSoundTruthTable() {
    std::printf("Son DMA STE (puce nue + Scheduler : table de vérité)\n");
    SndRig r;

    // --- 1. Masques d'adresse : 22 bits utiles, adresse PAIRE ----------------
    // L'octet haut est masqué à $3F (le compteur DMA n'a que 22 bits sur ST/STE
    // ≤ 4 Mo) et le bit0 de l'octet bas est ignoré (adresse paire). Une régression
    // ici pointerait le son hors RAM — sans qu'aucun pixel ne bouge.
    r.setStart(0xFFFFFF);
    checkHex("start : octet haut masqué à $3F", r.snd.read8(SND + 0x03), 0x3F);
    checkHex("start : octet bas forcé PAIR",    r.snd.read8(SND + 0x07), 0xFE);
    r.setEnd(0xFFFFFF);
    checkHex("end : octet haut masqué à $3F",   r.snd.read8(SND + 0x0F), 0x3F);
    checkHex("end : octet bas forcé PAIR",      r.snd.read8(SND + 0x13), 0xFE);

    // --- 2. $FF8900 est un registre MOT : l'octet PAIR relit 0, pas $FF ------
    // « move.w $FF8900,d0 » doit rendre $000x. Un $FF0x ferait lire au programme un
    // état de lecture qui n'existe pas.
    checkHex("$FF8900 (octet pair) relit 0", r.snd.read8(SND + 0x00), 0x00);

    // --- 3. Contrôle : quatre verrous ID0-3 relus, bits 4-7 à VSS ------------
    // Netlist 4081 (REGISTER.PIN) ; seuls play/repeat pilotent quelque chose.
    r.snd.write8(SND + 0x01, 0xFF);
    checkHex("contrôle : bits 0-3 tiennent", r.snd.read8(SND + 0x01), 0x0F);
    r.snd.write8(SND + 0x01, 0x00);
    checkHex("contrôle : arrêt", r.snd.read8(SND + 0x01), 0x00);

    // --- 4. Mode $FF8921 : masque $8F (mono + fréquence) --------------------
    r.snd.write8(SND + 0x21, 0xFF);
    checkHex("mode : masque $8F (mono + fréquence)", r.snd.read8(SND + 0x21), 0x8F);

    // --- 5. Le compteur VIVANT : à l'arrêt il montre l'adresse de DÉBUT ------
    r.setStart(0x020000);
    r.setEnd(0x020100);
    checkHex("compteur à l'arrêt = adresse de début (haut)", (r.counter() >> 16), 0x02);
    checkInt("compteur à l'arrêt = adresse de début",        long(r.counter()), 0x020000);

    // --- 6. Lecture : le pointeur AVANCE au fil des HBL, puis la trame finit --
    // C'est le comportement que le poll serré de $FF8909/0B/0D observe sur le vrai
    // matériel : le fetch DMA (8 octets par tic) est EN AVANCE sur la lecture DAC.
    for (int i = 0; i < 0x100; ++i) r.bus.write8(0x020000 + i, uint8_t(i));
    r.snd.write8(SND + 0x21, 0x83);                  // mono, 50 kHz
    r.snd.write8(SND + 0x01, 0x01);                  // PLAY
    checkBool("PLAY : la lecture est armée", (r.snd.read8(SND + 0x01) & 1) != 0, true);
    const uint32_t c0 = r.counter();
    for (int i = 0; i < 8; ++i) { r.sched.runTo(r.sched.now() + 512); r.snd.onHbl(); }
    checkBool("le compteur a AVANCÉ après 8 HBL", r.counter() > c0, true);
    checkBool("le compteur reste dans [start, end]",
              r.counter() >= 0x020000 && r.counter() <= 0x020100, true);

    // Sans repeat, la trame se termine et la lecture s'arrête d'elle-même.
    for (int i = 0; i < 400; ++i) { r.sched.runTo(r.sched.now() + 512); r.snd.onHbl(); }
    checkBool("fin de trame sans repeat : la lecture s'arrête",
              (r.snd.read8(SND + 0x01) & 1) == 0, true);
}

// -----------------------------------------------------------------------------
//  A29 — « puce nue + Scheduler » étendu au FDC / DMA disquette.
//
//  Le contrôleur DMA ($FF8600-$FF860F) n'était exercé QUE par des boots complets :
//  une régression sur le masque d'adresse ou sur le compteur de secteurs ne se
//  voyait qu'au bout d'un chargement raté, sans dire lequel des deux. Ni machine ni
//  ROM : un Bus, un YM2149, un Mfp, un Scheduler, un Fdc — et pas de disquette.
// -----------------------------------------------------------------------------
namespace {

constexpr uint32_t FDCR = 0xFF8600;

struct FdcRig {
    Bus       bus{512u * 1024u};
    YM2149    psg;
    Mfp       mfp;
    Scheduler sched;
    Fdc       fdc{bus, psg, mfp};

    FdcRig() {
        fdc.setScheduler(&sched);
        // Ce que Machine fait pour de vrai : sans ce câblage, la machine à états du
        // WD1772 est datée mais jamais servie — le RESTORE du reset resterait BUSY
        // pour toujours et le statut ne dirait rien.
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.reset(true);                                    // reset FROID
    }
    // Laisse la machine à états dérouler (le reset lance un RESTORE).
    void settle(int64_t cycles = 4'000'000) { sched.runTo(sched.now() + cycles); }

    void setMode(uint16_t m) {                       // $FF8606, mot big-endian
        fdc.write8(FDCR + 0x06, uint8_t(m >> 8));
        fdc.write8(FDCR + 0x07, uint8_t(m));
    }
    void setDmaAddr(uint32_t a) {
        fdc.write8(FDCR + 0x09, uint8_t(a >> 16));
        fdc.write8(FDCR + 0x0B, uint8_t(a >> 8));
        fdc.write8(FDCR + 0x0D, uint8_t(a));
    }
    uint32_t dmaAddr() {
        return (uint32_t(fdc.read8(FDCR + 0x09)) << 16)
             | (uint32_t(fdc.read8(FDCR + 0x0B)) << 8)
             |  uint32_t(fdc.read8(FDCR + 0x0D));
    }
};

}  // namespace

static void testFdcTruthTable() {
    std::printf("FDC / DMA disquette (puce nue + Scheduler : table de vérité)\n");
    FdcRig r;

    // --- 1. Adresse DMA : relisible, PAIRE, et bornée par la taille RAM ------
    // Le masque vient de FDC_WriteDMAAddress : bit0 toujours 0, et l'octet haut
    // limité à $3F sous 4 Mo — un diagnostic qui relit le compteur pour vérifier
    // le nombre d'octets transférés voit tout de suite une régression ici.
    r.setDmaAddr(0x123456);
    checkInt("adresse DMA relisible (paire)", long(r.dmaAddr()), 0x123456);
    r.setDmaAddr(0x123457);
    checkInt("adresse DMA : bit0 forcé à 0",  long(r.dmaAddr()), 0x123456);
    r.setDmaAddr(0xFFFFFE);
    checkInt("adresse DMA : octet haut masqué à $3F (512 Ko)",
             long(r.dmaAddr()), 0x3FFFFE);

    // --- 2. Compteur de secteurs : écrit via $FF8604 quand SCREG est armé ----
    // Et NON relisible par les bits rémanents de $FF8604 : Hatari sort avant de
    // mettre à jour ff8604recent_ (fdc.c:4695-4703). Deux comportements distincts
    // qu'un boot ne sépare pas.
    r.setMode(0x0090);                                // SCREG armé
    r.fdc.write8(FDCR + 0x04, 0x00);
    r.fdc.write8(FDCR + 0x05, 0x0A);                  // 10 secteurs
    checkHex("SCREG : $FF8604 ne devient PAS rémanent", r.fdc.read8(FDCR + 0x05), 0x00);

    // --- 3. Registre de piste, et la rémanence de $FF8604 dans le statut DMA -
    // Mode FDC_TR (A0) : l'écriture ne lance PAS de commande, elle pose la piste.
    // Et le mot de statut $FF8606 rejoue les bits 3-15 du dernier $FF8604 (vérifié
    // sur STF réel, cf. Hatari FDC_DmaStatus_ReadWord) — un détail que seul un
    // programme qui relit le statut voit, et qu'aucun boot ne distingue.
    r.setMode(0x0080 | 0x0002);                       // SCREG coupé, FDC_TR
    r.fdc.write8(FDCR + 0x04, 0x00);
    r.fdc.write8(FDCR + 0x05, 0x5A);
    checkHex("registre de piste relisible", r.fdc.read8(FDCR + 0x05), 0x5A);
    checkHex("statut DMA : bits 3-7 = dernier $FF8604",
             r.fdc.read8(FDCR + 0x07) & 0xF8, 0x5A & 0xF8);
    checkBool("statut DMA : bit1 = compteur de secteurs non nul",
              (r.fdc.read8(FDCR + 0x07) & 0x02) != 0, true);

    // --- 4. Densité Mega STE/TT ($FF860E) : mot relisible --------------------
    r.fdc.write8(FDCR + 0x0E, 0x00);
    r.fdc.write8(FDCR + 0x0F, 0x03);                  // HD
    checkHex("densité $FF860F relisible", r.fdc.read8(FDCR + 0x0F), 0x03);

    // --- 5. Le RESTORE du reset froid se termine, statut propre --------------
    r.settle();
    r.setMode(0x0080);                                // FDC_CS : registre de statut
    checkHex("après le reset froid : statut au repos", r.fdc.read8(FDCR + 0x05), 0x00);

    // --- 6. AUCUN lecteur sélectionné : les 3 entrées du WD1772 sont EFFACÉES -
    // Les bits 1-2 du port A du PSG sélectionnent les lecteurs et sont ACTIFS BAS :
    // les poser à 1 désélectionne tout. Le statut type I doit alors EFFACER TR00,
    // INDEX et WPRT (fdc.c : `updateStr(TR00|INDEX|WPRT, 0)`), pas les forcer — une
    // polarité qu'on inverse sans s'en apercevoir, et que seul un programme lisant
    // le statut sans disquette distingue.
    r.psg.write8(0xFF8800, 14);                       // sélection registre 14 (port A)
    r.psg.write8(0xFF8802, 0x07);                     // face 0 + les DEUX lecteurs OFF
    r.setMode(0x0080);
    r.fdc.write8(FDCR + 0x04, 0x00);
    r.fdc.write8(FDCR + 0x05, 0x00);                  // RESTORE : commande de TYPE I
    r.settle(32'000'000);                             // ~4 s de temps ST
    // ⚠ OBSERVÉ, non comparé à l'oracle : sans lecteur sélectionné ce RESTORE ne se
    // termine pas (BUSY reste posé même après 4 s). C'est plausible — le WD1772
    // attend un signal qui n'arrive jamais — mais ce n'est PAS vérifié contre
    // Hatari, donc ce n'est pas ce qu'on affirme ici. Le statut type I, lui, se lit
    // pendant la commande, et c'est sa POLARITÉ qui est le sujet.
    const uint8_t st = r.fdc.read8(FDCR + 0x05);
    checkBool("sans lecteur : TR00 effacé",  (st & 0x04) == 0, true);
    checkBool("sans lecteur : INDEX effacé", (st & 0x02) == 0, true);
    checkBool("sans lecteur : WPRT effacé",  (st & 0x40) == 0, true);

    // --- 7. Un registre hors carte relit $FF (pas 0) -------------------------
    checkHex("$FF8600 (hors carte) relit $FF", r.fdc.read8(FDCR + 0x00), 0xFF);

    // Floppy_FindDiskDetails est relu par Hatari après une écriture du boot :
    // une nouvelle géométrie valide doit changer les accès suivants sans remontage.
    // 720 Ko : 80×9×2 devient 160×9×1 (même nombre de secteurs, même densité).
    // On ne sonde que les pistes 0/1 : aucune tête n'a besoin d'atteindre 159.
    std::filesystem::path work;
    bool created = false;
    for (int n = 0; n < 1000 && !created; ++n) {
        work = std::filesystem::temp_directory_path()
             / ("neost-selftest-fdc-bpb-" + std::to_string(n));
        std::error_code ec;
        created = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    checkBool("BPB : répertoire temporaire exclusif", created, true);
    if (!created) return;
    struct RemoveDisk {
        std::filesystem::path path;
        ~RemoveDisk() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } removeDisk{work};
    const auto path = work / "disk.st";
    std::vector<uint8_t> image(80u * 9u * 2u * 512u, 0x39);
    image[0x13] = 0xA0; image[0x14] = 0x05;  // 1440 secteurs
    image[0x18] = 9; image[0x19] = 0;
    image[0x1A] = 2; image[0x1B] = 0;
    FILE* disk = std::fopen(path.string().c_str(), "wb");
    checkBool("BPB : création image temporaire", disk != nullptr, true);
    if (!disk) return;
    const bool saved = std::fwrite(image.data(), 1, image.size(), disk) == image.size();
    const bool closed = std::fclose(disk) == 0;
    checkBool("BPB : image temporaire complète", saved && closed, true);
    if (!saved || !closed) return;
    FdcRig g;
    g.fdc.setHostWriteBack(false);
    checkBool("BPB : montage image double face", g.fdc.loadImage(path.string()), true);
    if (!g.fdc.inserted()) return;
    auto reg = [&](uint16_t mode, uint8_t value) {
        g.setMode(mode);
        g.fdc.write8(FDCR + 4, 0);
        g.fdc.write8(FDCR + 5, value);
    };
    auto sector = [&](bool write, uint8_t side, uint8_t number,
                      uint8_t command = 0, uint8_t count = 1) {
        g.psg.write8(0xFF8800, 14);
        g.psg.write8(0xFF8802, uint8_t(0x05 & ~side));
        g.fdc.refreshDriveSide();
        g.setMode(write ? 0x0080 : 0x0180); // bascule WRBIT : reset FIFO/compteur
        g.setMode(write ? 0x0180 : 0x0080);
        g.setDmaAddr(0x2000);
        reg(write ? 0x0190 : 0x0090, count);
        reg(write ? 0x0182 : 0x0082, 0);  // TR
        reg(write ? 0x0184 : 0x0084, number); // SR
        reg(write ? 0x0180 : 0x0080, command ? command : (write ? 0xA8 : 0x88)); // pas de spin-up
        const int64_t deadline = g.sched.now() + 16'000'000;
        uint8_t status;
        unsigned events = 0;
        while ((status = g.fdc.read8(FDCR + 5)) & 1) {
            const int64_t due = g.sched.nextDue();
            if (due < 0 || due > deadline || ++events > 200000) break;
            g.sched.runTo(due);
        }
        checkBool("BPB : commande terminée", (status & 1) == 0, true);
        return status;
    };
    // Les accès passent par la vraie machine à états et la FIFO DMA.
    checkHex("BPB double face : lecture face 1", sector(false, 1, 1) & 0x10, 0);
    checkHex("BPB double face : données face 1", g.bus.ram[0x2000], 0x39);
    std::fill_n(g.bus.ram.begin() + 0x2000, 512, uint8_t(0xA5));
    checkHex("BPB double face : écriture face 1", sector(true, 1, 1) & 0x50, 0);
    checkHex("BPB double face : relecture face 1", sector(false, 1, 1) & 0x10, 0);
    checkHex("BPB double face : écriture relue", g.bus.ram[0x2000], 0xA5);
    auto writeBoot = [&](uint8_t sides, uint8_t spt = 9) {
        std::copy_n(image.begin(), 512, g.bus.ram.begin() + 0x2000);
        g.bus.ram[0x2018] = spt;
        g.bus.ram[0x201A] = sides;
        checkHex("BPB : écriture nouveau boot", sector(true, 0, 1) & 0x50, 0);
    };
    writeBoot(1);
    checkHex("BPB simple face : lecture face 1 RNF", sector(false, 1, 1) & 0x10, 0x10);
    std::fill_n(g.bus.ram.begin() + 0x2000, 512, uint8_t(0x5A));
    checkHex("BPB simple face : écriture face 1 RNF", sector(true, 1, 1) & 0x10, 0x10);
    checkHex("BPB simple face : lecture face 0", sector(false, 0, 2) & 0x10, 0);
    checkHex("BPB simple face : données intactes", g.bus.ram[0x2000], 0x39);
    // BPB invalide : le repli Hatari devine 80×9×2 depuis la taille.
    writeBoot(0);
    checkHex("BPB invalide : repli double face", sector(false, 1, 1) & 0x10, 0);
    checkHex("BPB invalide : écriture RNF sans effet", g.bus.ram[0x2000], 0xA5);

    // Le formateur WRITE TRACK valide les neuf secteurs sous l'ancienne
    // géométrie, puis publie le nouveau BPB. Le reste de la piste doit être écrit.
    std::vector<uint8_t> track(60, 0x4E);
    for (uint8_t sec = 1; sec <= 9; ++sec) {
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xA1, 0xA1, 0xA1, 0xFE, 0, 0, sec, 2, 0, 0});
        track.insert(track.end(), 22, 0x4E);
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xA1, 0xA1, 0xA1, 0xFB});
        const size_t data = track.size();
        track.insert(track.end(), 512, uint8_t(0x60 + sec));
        if (sec == 1) {
            std::copy_n(image.begin(), 512, track.begin() + data);
            track[data + 0x1A] = 1;
        }
        track.insert(track.end(), 2, 0); // CRC synthétisé à la relecture
        track.insert(track.end(), 40, 0x4E);
    }
    track.resize(6268, 0x4E);
    std::copy(track.begin(), track.end(), g.bus.ram.begin() + 0x2000);
    checkHex("BPB WRITE TRACK : formatage complet", sector(true, 0, 1, 0xF8, 13) & 0x54, 0);
    checkHex("BPB WRITE TRACK : nouvelle face absente", sector(false, 1, 1) & 0x10, 0x10);
    checkHex("BPB WRITE TRACK : secteur suivant lisible", sector(false, 0, 2) & 0x10, 0);
    checkHex("BPB WRITE TRACK : données secteur suivant", g.bus.ram[0x2000], 0x62);

    // Conteneur MSA 80×9×2 non compressé. Le BPB logique peut devenir
    // non représentable en MSA ; la persistance doit conserver TOUS les octets.
    std::vector<uint8_t> msa{0x0E, 0x0F, 0, 9, 0, 1, 0, 0, 0, 79};
    for (size_t off = 0; off < image.size(); off += 9 * 512) {
        msa.insert(msa.end(), {0x12, 0x00});
        msa.insert(msa.end(), image.begin() + off, image.begin() + off + 9 * 512);
    }
    const auto msaPath = work / "disk.msa";
    disk = std::fopen(msaPath.string().c_str(), "wb");
    checkBool("BPB MSA : création image", disk != nullptr, true);
    if (!disk) return;
    const bool msaSaved = std::fwrite(msa.data(), 1, msa.size(), disk) == msa.size();
    const bool msaClosed = std::fclose(disk) == 0;
    checkBool("BPB MSA : image complète", msaSaved && msaClosed, true);
    if (!msaSaved || !msaClosed) return;
    g.fdc.setHostWriteBack(true);
    checkBool("BPB MSA : montage", g.fdc.loadImage(msaPath.string()), true);
    writeBoot(1); // 160 pistes : encodeMsa refuse ce découpage, pas les données.
    checkBool("BPB MSA : remontage après boot SS", g.fdc.loadImage(msaPath.string()), true);
    checkHex("BPB MSA : boot SS persisté", sector(false, 0, 1) & 0x10, 0);
    checkHex("BPB MSA : face SS persistée", g.bus.ram[0x201A], 1);
    checkHex("BPB MSA : face 1 absente après remontage", sector(false, 1, 1) & 0x10, 0x10);
    std::fill_n(g.bus.ram.begin() + 0x2000, 512, uint8_t(0x5A));
    checkHex("BPB MSA : écriture après boot SS", sector(true, 0, 2) & 0x50, 0);
    checkBool("BPB MSA : remontage données", g.fdc.loadImage(msaPath.string()), true);
    checkHex("BPB MSA : lecture données persistées", sector(false, 0, 2) & 0x10, 0);
    checkHex("BPB MSA : données persistées", g.bus.ram[0x2000], 0x5A);
    writeBoot(2, 11); // 1440 secteurs n'est pas divisible par 11×2.
    checkBool("BPB MSA : remontage BPB spt 11", g.fdc.loadImage(msaPath.string()), true);
    checkHex("BPB MSA : lecture BPB spt 11", sector(false, 0, 1) & 0x10, 0);
    checkHex("BPB MSA : spt 11 persisté", g.bus.ram[0x2018], 11);
    checkHex("BPB MSA : données encore présentes", sector(false, 0, 2) & 0x10, 0);
    checkHex("BPB MSA : données préservées au repli", g.bus.ram[0x2000], 0x5A);
}

// -----------------------------------------------------------------------------
//  A28 — cadence des trames & servo audio (neost::pacing).
//
//  Ce code vivait en TROIS copies (GUI natif, web, Android) et n'était testé nulle
//  part : ni le headless ni les étalons pixel ne le traversent. Or c'est lui qui
//  décide combien d'échantillons sortent par trame — une erreur y donne un son
//  qui dérive ou qui hache, jamais un pixel de différence.
// -----------------------------------------------------------------------------
static void testPacing() {
    std::printf("Cadence & servo audio (neost::pacing)\n");
    using namespace neost::pacing;

    // --- 1. La cadence suit la GÉOMÉTRIE, pas un 20 ms figé ------------------
    // PAL 313×512 = 160 256 cyc ; NTSC 263×508 = 133 604 ; mono 501×224 = 112 224.
    checkBool("trame PAL  ≈ 19,98 ms", std::fabs(frameMillis(313 * 512) - 19.978) < 0.01, true);
    checkBool("trame NTSC ≈ 16,66 ms", std::fabs(frameMillis(263 * 508) - 16.656) < 0.01, true);
    checkBool("trame mono ≈ 13,99 ms", std::fabs(frameMillis(501 * 224) - 13.991) < 0.01, true);
    checkBool("frameNanos cohérent avec frameMillis",
              std::llabs(frameNanos(313 * 512) - 19978000) < 5000, true);

    // --- 2. Report fractionnaire : le débit MOYEN colle au temps émulé -------
    // 160 256 cyc à 48 kHz = 959,03… échantillons. Sans report, tronquer à 959 à
    // chaque trame perdrait ~1,7 échantillon par seconde — l'anneau se viderait,
    // le son hacherait, et personne ne saurait pourquoi.
    {
        AudioPacer p;
        long total = 0;
        const int kFrames = 1000;
        for (int i = 0; i < kFrames; ++i)
            total += p.samplesForFrame(313 * 512, 48000, 0, 0);   // servo neutre
        const double exact = double(kFrames) * double(313 * 512) * 48000.0 / kCpuHz;
        checkBool("report fractionnaire : 1 000 trames à ±1 échantillon",
                  std::fabs(double(total) - exact) <= 1.0, true);
        // Contre-épreuve : tronquer à chaque trame (ce que fait un code sans report)
        // perdrait ~989 échantillons sur 1 000 trames — 20 ms de son, à chaque
        // vingtaine de secondes. C'est cet écart-là que le report annule.
        const int truncated = int(double(313 * 512) * 48000.0 / kCpuHz);
        checkBool("sans report, la troncature perdrait > 500 échantillons/1 000 trames",
                  exact - double(kFrames * truncated) > 500.0, true);
    }

    // --- 3. Le servo : proportionnel, borné, et du BON SIGNE -----------------
    // File VIDE (queued 0) ⇒ il faut produire PLUS. File PLEINE ⇒ moins. Un signe
    // inversé emballe la boucle au lieu de la stabiliser.
    {
        AudioPacer p;
        const int base = p.samplesForFrame(313 * 512, 48000, 0, 0);
        AudioPacer q;
        checkInt("file vide : +8 échantillons (clamp)",
                 q.samplesForFrame(313 * 512, 48000, 4000, 0) - base, +8);
        AudioPacer r;
        checkInt("file pleine : −8 échantillons (clamp)",
                 r.samplesForFrame(313 * 512, 48000, 0, 4000) - base, -8);
        AudioPacer t;
        checkInt("écart de 256 trames : +1 échantillon",
                 t.samplesForFrame(313 * 512, 48000, 256, 0) - base, +1);
        AudioPacer u;
        checkInt("file à la cible : aucune correction",
                 u.samplesForFrame(313 * 512, 48000, 2000, 2000) - base, 0);
    }

    // --- 4. Rampe anti-clic : le volume ARRIVE à la cible, sans marche -------
    // Un saut instantané (mute en plein signal) posait une marche par bloc de
    // ~20 ms : clic audible, « zipper » en glissant le curseur.
    {
        AudioPacer p;
        float buf[8];
        for (int i = 0; i < 8; ++i) buf[i] = 1.0f;
        p.applyMasterVolume(buf, 4, 0.0f);                 // 1.0 → 0.0 sur 4 trames
        checkBool("rampe : 1er échantillon à 0,75",  std::fabs(buf[0] - 0.75f) < 1e-6f, true);
        checkBool("rampe : 2e échantillon à 0,50",   std::fabs(buf[2] - 0.50f) < 1e-6f, true);
        checkBool("rampe : dernier échantillon à 0", std::fabs(buf[6] - 0.00f) < 1e-6f, true);
        checkBool("rampe : L et R traités pareil",   buf[0] == buf[1], true);
        checkBool("rampe : le volume effectif ARRIVE à la cible",
                  p.volumeSmoothed() == 0.0f, true);
        // Volume déjà à 1 et lissé à 1 : aucun travail, aucune modification.
        AudioPacer q; float b2[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        q.applyMasterVolume(b2, 2, 1.0f);
        checkBool("volume neutre : le bloc n'est pas touché", b2[0] == 0.5f, true);
    }

    // --- 5. Garde-fou anti-saturation ---------------------------------------
    {
        float buf[4] = {2.0f, -2.0f, 0.5f, -0.5f};
        AudioPacer::clampStereo(buf, 2);
        checkBool("clamp : +2 → +1", buf[0] == 1.0f, true);
        checkBool("clamp : −2 → −1", buf[1] == -1.0f, true);
        checkBool("clamp : ce qui est dans les clous ne bouge pas", buf[2] == 0.5f, true);
    }

    // --- 6. Rattrapage de cadence : dû, pas dû, et le PLAFOND ----------------
    {
        FramePacer f;
        int calls = 0;
        auto oneFrame = [&] { ++calls; return int64_t(313 * 512); };   // 19,978 ms

        checkInt("1re trame : elle est due tout de suite", f.runDue(1000.0, oneFrame), 1);
        checkInt("même instant : plus rien n'est dû",      f.runDue(1000.0, oneFrame), 0);
        checkInt("après 20 ms : une trame",                f.runDue(1020.0, oneFrame), 1);
        checkInt("après 40 ms de plus : deux trames",      f.runDue(1060.0, oneFrame), 2);
        // Onglet en arrière-plan / appli réveillée : le retard est ABANDONNÉ, pas
        // traîné — sinon la boucle spirale et ne rend jamais la main.
        checkInt("long sommeil : plafonné à 4 trames",     f.runDue(11060.0, oneFrame), 4);
        // Le retard est ABANDONNÉ : l'échéance repart de l'instant courant. Sans
        // ça, les ~500 trames dues après 10 s de sommeil seraient rejouées 4 par
        // 4 pendant des secondes — machine qui s'emballe, son en accéléré.
        checkBool("le retard est abandonné (échéance recalée sur maintenant)",
                  std::fabs(f.nextDueMs() - 11060.0) < 1e-9, true);
        checkInt("l'appel suivant ne rattrape PAS la dette (1 trame, pas 4)",
                 f.runDue(11060.0, oneFrame), 1);
        checkInt("total d'appels cohérent", calls, 1 + 1 + 2 + 4 + 1);

        // resync explicite (menu borne ouvert = machine en pause).
        FramePacer g;
        g.resync(500.0);
        checkInt("après resync : la trame courante est due", g.runDue(500.0, oneFrame), 1);
    }
}


// -----------------------------------------------------------------------------
//  A31 — la table de plages MMIO du Bus est-elle COHÉRENTE ?
//
//  Le défaut qu'A31 corrige n'est pas une lenteur, c'est un ORDRE SÉMANTIQUE
//  IMPLICITE : deux chaînes de `if` où la position d'une branche décidait du
//  résultat sans que rien ne le dise. La table ne vaut mieux QUE SI ses plages
//  sont disjointes — sinon l'ordre des lignes redevient significatif, en silence.
//  Ce test le PROUVE, à chaque palier `fast`, sans machine ni ROM.
// -----------------------------------------------------------------------------
static void testMmioTable() {
    std::printf("Bus — table de plages MMIO (A31)\n");
    const char* a = nullptr; const char* b = nullptr;
    const bool ok = Bus::mmioTableDisjoint(&a, &b);
    if (!ok) std::printf("  (chevauchement : %s / %s)\n", a ? a : "?", b ? b : "?");
    checkBool("les plages MMIO sont DISJOINTES (l'ordre des lignes n'est pas signifiant)",
              ok, true);
    // Garde anti-vide : une table vidée par accident passerait « disjointe ».
    checkBool("la table décrit au moins 12 puces", Bus::mmioTableSize() >= 12, true);
}

// -----------------------------------------------------------------------------
//  A36 — OÙ vit neost.cfg ? La règle a quatre cas et deux plateformes ; elle se
//  teste ici SANS toucher au disque (sondes injectées, comme hostpath::Style).
//
//  Le défaut d'origine : `exeDir + "/../neost.cfg"`, point. Correct pour
//  `build/neost`, l'AppImage et le zip Windows — tous portables. Faux pour un
//  `/usr/bin/neost`, qui cherchait sa config dans `/usr/`, où l'utilisateur
//  n'écrit pas : l'écriture échouait en silence.
// -----------------------------------------------------------------------------
static void testConfigPath() {
    std::printf("neost.cfg — où vit la configuration (A36)\n");
    using namespace neost::cfgpath;
    using neost::hostpath::Style;

    // Fabrique une sonde : ensemble des fichiers existants, dossiers inscriptibles,
    // variables d'environnement.
    auto probe = [](std::set<std::string> files, std::set<std::string> writable,
                    std::map<std::string, std::string> env) {
        Probe p;
        p.exists      = [files](const std::string& f)    { return files.count(f) != 0; };
        p.dirWritable = [writable](const std::string& d)  { return writable.count(d) != 0; };
        p.env         = [env](const char* n) {
            auto it = env.find(n); return it == env.end() ? std::string() : it->second;
        };
        return p;
    };

    // --- 1. Installation PORTABLE : la config existante gagne toujours --------
    // Arbre de dev, AppImage, zip Windows, borne : rien ne doit changer, et surtout
    // une mise à jour ne doit pas faire « disparaître » les réglages.
    checkStr("portable : la config à côté du binaire gagne",
             resolve("/opt/neost/bin",
                     probe({"/opt/neost/bin/../neost.cfg"}, {}, {{"HOME", "/home/u"}}),
                     Style::Posix),
             "/opt/neost/bin/../neost.cfg");

    // Même quand une config utilisateur existe AUSSI : on ne déplace personne.
    checkStr("portable : elle gagne même si l'utilisateur en a une",
             resolve("/opt/neost/bin",
                     probe({"/opt/neost/bin/../neost.cfg",
                            "/home/u/.config/neost/neost.cfg"}, {}, {{"HOME", "/home/u"}}),
                     Style::Posix),
             "/opt/neost/bin/../neost.cfg");

    // --- 2. Installation SYSTÈME : la config utilisateur existante ------------
    checkStr("système : la config utilisateur existante est retenue",
             resolve("/usr/bin",
                     probe({"/home/u/.config/neost/neost.cfg"}, {}, {{"HOME", "/home/u"}}),
                     Style::Posix),
             "/home/u/.config/neost/neost.cfg");

    // --- 3. Rien n'existe : on choisit où ÉCRIRE -----------------------------
    // /usr n'est pas inscriptible → config utilisateur. C'EST le bug d'origine :
    // avant A36, on rendait "/usr/bin/../neost.cfg" et l'écriture échouait.
    checkStr("système, rien n'existe : on écrit chez l'utilisateur",
             resolve("/usr/bin", probe({}, {}, {{"HOME", "/home/u"}}), Style::Posix),
             "/home/u/.config/neost/neost.cfg");

    // Dossier du binaire inscriptible (portable neuf) → comportement historique.
    checkStr("portable neuf : on écrit à côté du binaire",
             resolve("/opt/neost/bin",
                     probe({}, {"/opt/neost/bin/.."}, {{"HOME", "/home/u"}}),
                     Style::Posix),
             "/opt/neost/bin/../neost.cfg");

    // --- 4. XDG_CONFIG_HOME, et la règle « absolu seulement » ----------------
    checkStr("XDG_CONFIG_HOME absolu est respecté",
             resolve("/usr/bin",
                     probe({}, {}, {{"HOME", "/home/u"}, {"XDG_CONFIG_HOME", "/xdg"}}),
                     Style::Posix),
             "/xdg/neost/neost.cfg");
    // La spec XDG dit d'IGNORER une valeur relative : sinon la config partirait
    // dans le répertoire courant du lancement, qui n'a rien à voir.
    checkStr("XDG_CONFIG_HOME RELATIF est ignoré (spec XDG)",
             resolve("/usr/bin",
                     probe({}, {}, {{"HOME", "/home/u"}, {"XDG_CONFIG_HOME", "rel/atif"}}),
                     Style::Posix),
             "/home/u/.config/neost/neost.cfg");

    // --- 5. Windows : %APPDATA% ---------------------------------------------
    // ⚠ Le séparateur rendu est « / » : c'est la convention INTERNE de hostpath
    // (cf. son SEP — « accepté par Win32 aussi »). Le vérifier ici plutôt que de
    // le supposer : ma première version de ce test attendait des « \ » et a
    // rougi — le code avait raison, l'attente était fausse.
    checkStr("Windows : %APPDATA%/neost (séparateur interne « / »)",
             resolve("C:\\Program Files\\NeoST\\bin",
                     probe({}, {}, {{"APPDATA", "C:\\Users\\u\\AppData\\Roaming"}}),
                     Style::Windows),
             "C:/Users/u/AppData/Roaming/neost/neost.cfg");

    // --- 6. Ni HOME ni XDG : on rend le chemin historique ---------------------
    // Cas d'un démon sans environnement. On ne devine pas : on rend le chemin
    // d'avant, et l'écriture échouera EN LE DISANT plutôt qu'en silence.
    checkStr("sans environnement : repli sur le chemin historique",
             resolve("/usr/bin", probe({}, {}, {}), Style::Posix),
             "/usr/bin/../neost.cfg");

    // --- 7. Les profils suivent la config, toujours --------------------------
    checkStr("profils : à côté de la config utilisateur",
             profilesDirFor("/home/u/.config/neost/neost.cfg", Style::Posix),
             "/home/u/.config/neost/profiles");
    checkStr("profils : à côté de la config portable",
             profilesDirFor("/opt/neost/bin/../neost.cfg", Style::Posix),
             "/opt/neost/bin/../profiles");
}

// -----------------------------------------------------------------------------
//  A12 — écrire la config dans un dossier qui N'EXISTE PAS ENCORE.
//
//  Le test ci-dessus est PUR : il prouve quel chemin la règle retient, et c'est
//  tout. Le défaut trouvé le 2026-09-01 vivait juste après — la règle rendait le
//  bon chemin (`~/.config/neost/neost.cfg`, le dossier du binaire étant en lecture
//  seule), mais RIEN ne créait ce dossier, donc l'écriture échouait. Conséquence
//  livrée : sur le `.dmg` macOS monté — et sur tout AppImage, dont le contenu est
//  monté en lecture seule lui aussi — le paquet ne conservait AUCUN réglage, chez
//  tout premier utilisateur.
//
//  ⚠ Ce test-ci touche le DISQUE, contrairement à son voisin. C'est délibéré et
//  c'est la leçon : la partie pure était testée, la partie qui écrit ne l'était
//  pas, et c'est exactement là que le défaut s'est logé. Il écrit sous le dossier
//  temporaire du système et nettoie derrière lui.
//
//  ⚠ Pourquoi la CI ne pouvait pas l'attraper autrement : les six smoke-tests de
//  paquet tournent sur un dossier EXTRAIT (`squashfs-root/usr`,
//  `dist/NeoST.app/Contents`, `_check/*/`), donc inscriptible. Le support de
//  livraison réel — image montée en lecture seule — n'est jamais exercé.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  Chasse aux bugs du 2026-09-01 — un `bool` MEMBRE d'un agrégat copié en bloc.
//
//  `StateArchive::operator()` normalise les booléens, mais UNIQUEMENT ceux qu'on lui
//  passe seuls : quand `T` est une struct, il ne voit que la struct et les octets du
//  fichier atterrissent tels quels dans ses membres. Or un `bool` dont l'octet vaut
//  autre chose que 0 ou 1 est un COMPORTEMENT INDÉFINI — le compilateur teste souvent
//  le bit 0 dans `if (b)` et l'octet entier dans `if (!b)`, si bien que les DEUX
//  branches peuvent être prises. C'est le cas de `Scc::Chn`, de `Fpu` et de
//  `StePads`, tous relus en bloc. `fixBools()` est le rattrapage ; ce test le garde.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//  Masque de canaux MIDI — l'aller-retour du masque VIDE (chasse round 2).
//
//  `formatChannelMask(0)` rendait « 1-16 » et `parseChannelMask("")` rendait 0xFFFF,
//  au nom d'un invariant écrit — « un masque vide serait une destination muette, ce
//  qu'on n'écrit jamais » — que le bouton `none` de la page MIDI a rendu faux. Couper
//  TOUS les canaux d'un appareil le rouvrait donc EN GRAND au redémarrage : le
//  réglage se retournait exactement à l'envers.
// -----------------------------------------------------------------------------
static void testChannelMask() {
    std::printf("MIDI — masque de canaux, aller-retour (chasse round 2)\n");
    using neost::appconfig::parseChannelMask;
    using neost::appconfig::formatChannelMask;

    // Le cas du défaut : vide → vide, et pas « tous ».
    checkStr("masque VIDE s'écrit « none »", formatChannelMask(0), "none");
    checkBool("« none » se relit VIDE", parseChannelMask("none") == 0, true);
    checkBool("aller-retour du masque vide", parseChannelMask(formatChannelMask(0)) == 0, true);

    // Une ligne ILLISIBLE reste « tous les canaux » : c'est l'intention d'origine,
    // et elle ne doit pas avoir changé.
    checkBool("ligne vide = tous les canaux", parseChannelMask("") == 0xFFFF, true);
    checkBool("ligne illisible = tous les canaux", parseChannelMask("bruit") == 0xFFFF, true);

    // Les cas ordinaires ne bougent pas.
    checkStr("tous les canaux", formatChannelMask(0xFFFF), "1-16");
    checkStr("un seul canal", formatChannelMask(uint16_t(1u << 1)), "2");
    checkBool("aller-retour 1,3,10-12",
              parseChannelMask("1,3,10-12") == parseChannelMask(formatChannelMask(parseChannelMask("1,3,10-12"))),
              true);
}

static void testFixBools() {
    std::printf("StateArchive — booléens relus EN BLOC (chasse 2026-09-01)\n");
    struct Agg { uint8_t a; bool flag; uint8_t b; };
    static_assert(std::is_trivially_copyable_v<Agg>, "l'agrégat doit être copiable en bloc");

    std::vector<uint8_t> buf;
    { Agg w{1, true, 2}; StateArchive s = StateArchive::saver(buf); s(w); }

    // On force l'octet du bool à 63 — exactement la valeur qu'un .state forgé avait
    // posée dans Machine::frameInProgress_ et qu'UBSan avait signalée.
    const std::size_t off = offsetof(Agg, flag);
    checkBool("l'octet du bool est bien dans le tampon", off < buf.size(), true);
    buf[off] = 63;

    Agg r{};
    StateArchive l = StateArchive::loader(buf.data(), buf.size());
    l(r);
    unsigned char brut;
    std::memcpy(&brut, &r.flag, 1);
    checkBool("sans rattrapage, l'octet reste 63 (c'est le défaut)", brut == 63, true);

    l.fixBools(r.flag);
    std::memcpy(&brut, &r.flag, 1);
    checkBool("fixBools normalise à 1", brut == 1, true);
    checkBool("et la valeur reste VRAIE", r.flag, true);

    // Un octet nul doit rester faux — le rattrapage ne doit pas tout allumer.
    Agg z{}; std::memcpy(&z.flag, "\0", 1);
    StateArchive l2 = StateArchive::loader(buf.data(), buf.size());
    l2(z); buf[off] = 0;
    StateArchive l3 = StateArchive::loader(buf.data(), buf.size());
    Agg f{}; l3(f); l3.fixBools(f.flag);
    checkBool("un octet nul reste FAUX", f.flag, false);
}

static void testConfigWriteCreatesDir() {
    std::printf("neost.cfg — l'écriture CRÉE son dossier (A12)\n");
    namespace fs = std::filesystem;

    const fs::path base = fs::temp_directory_path() / "neost-selftest-cfg";
    std::error_code ec;
    fs::remove_all(base, ec);                 // repartir propre, sans exiger l'absence
    const fs::path target = base / "neost" / "neost.cfg";

    // Le dossier n'existe pas : c'est TOUTE la situation du défaut.
    checkBool("le dossier de destination n'existe pas encore",
              fs::exists(base, ec), false);

    neost::appconfig::Config w{};
    std::string err;
    // cwdFallback=false À DESSEIN : le repli sur le répertoire courant masquerait
    // le défaut en écrivant ailleurs — et c'est lui qui rendait le message d'erreur
    // trompeur (il nommait « neost.cfg.tmp », pas le chemin voulu).
    const bool ok = neost::appconfig::writeConfigAtomic(
        target.string(), w, /*full=*/true, /*cwdFallback=*/false, err);
    if (!ok) std::printf("  (err = %s)\n", err.c_str());
    checkBool("écrire dans un dossier absent RÉUSSIT", ok, true);
    checkBool("le fichier est bien là", fs::exists(target, ec), true);
    checkBool("aucun temporaire laissé derrière",
              fs::exists(fs::path(target.string() + ".tmp"), ec), false);

    fs::remove_all(base, ec);
}

// -----------------------------------------------------------------------------
//  A33 — DEUX CPU vivants dans le même processus.
//
//  `Cpu68k` jetait sur une seconde instance :
//      throw std::logic_error("Cpu68k supports only one live instance")
//  C'était le plafond qui interdisait le test unitaire d'une Machine, l'A/B en un
//  processus et l'anneau MIDI à deux nœuds. Ce test-ci est la RAISON D'ÊTRE du
//  chantier : s'il ne tournait pas, A33 n'aurait rien changé d'observable.
//
//  ⚠ Ce qu'il NE prouve PAS : deux CPU tournant SIMULTANÉMENT dans deux threads.
//  Le modèle est « à tour de rôle » — g_cur désigne l'instance active, posée à
//  l'entrée de run()/reset(). C'est ce qu'A33 promettait, ni plus ni moins.
// -----------------------------------------------------------------------------
static void testTwoCpus() {
    std::printf("Cpu68k — deux instances vivantes (A33)\n");

    Bus busA(512u * 1024u), busB(512u * 1024u);
    // Deux CPU sur DEUX bus : la construction du second jetait, avant A33.
    Cpu68k cpuA(busA), cpuB(busB);
    busA.cpu = &cpuA;
    busB.cpu = &cpuB;
    checkBool("deux Cpu68k coexistent (plus de logic_error)", true, true);

    // Chacun voit SON bus. On pose un vecteur de reset différent de chaque côté :
    // si les deux CPU partageaient un état, le second écraserait le premier.
    auto poke32 = [](Bus& b, uint32_t a, uint32_t v) {
        b.write8(a, uint8_t(v >> 24)); b.write8(a + 1, uint8_t(v >> 16));
        b.write8(a + 2, uint8_t(v >> 8)); b.write8(a + 3, uint8_t(v));
    };
    constexpr uint32_t kPcA = 0x001000, kPcB = 0x002000;
    for (Bus* b : {&busA, &busB}) { poke32(*b, 0, 0x00040000); }   // SSP
    poke32(busA, 4, kPcA);
    poke32(busB, 4, kPcB);
    // Une instruction inoffensive à chaque PC : bra.s vers soi-même ($60FE).
    busA.write8(kPcA, 0x60); busA.write8(kPcA + 1, 0xFE);
    busB.write8(kPcB, 0x60); busB.write8(kPcB + 1, 0xFE);

    cpuA.reset();
    cpuB.reset();
    checkHex("CPU A a pris le vecteur de SON bus", cpuA.pc(), kPcA);
    checkHex("CPU B a pris le vecteur de SON bus", cpuB.pc(), kPcB);

    // Et ils avancent INDÉPENDAMMENT : on ne fait tourner que A.
    const int64_t a0 = cpuA.busClockNow(), b0 = cpuB.busClockNow();
    cpuA.run(64);
    checkBool("faire tourner A avance l'horloge de A", cpuA.busClockNow() > a0, true);
    checkBool("… et laisse celle de B intacte", cpuB.busClockNow() == b0, true);

    // Puis seulement B : l'activation bascule proprement dans les deux sens.
    cpuB.run(64);
    checkBool("faire tourner B avance ensuite l'horloge de B",
              cpuB.busClockNow() > b0, true);
    checkHex("A n'a pas bougé de PC pendant que B tournait", cpuA.pc(), kPcA);
}

// -----------------------------------------------------------------------------
//  A39 — IKBD : le PROTOCOLE, table de vérité (2026-08-29).
//
//  `io/Ikbd.cpp` fait 1 189 lignes et n'avait qu'un test : celui du TDRE de son
//  ACIA. Le protocole du 6301 lui-même — l'accumulation des commandes multi-octets,
//  la longueur attendue de chacune, la forme des paquets de réponse — n'était
//  couvert QUE par des jeux réels, c'est-à-dire par « ça marche ou ça ne marche
//  pas », sans rien entre les deux.
//
//  Or c'est une machine à états pure : des octets entrent, des octets sortent.
//  Exactement ce que le patron « puce nue + Scheduler » d'A29 sait pincer.
// -----------------------------------------------------------------------------
namespace {

constexpr uint32_t kIkbdCtrl = 0xFFFC00, kIkbdData = 0xFFFC02;

struct IkbdRig {
    Mfp       mfp;
    Scheduler sched;
    Ikbd      ikbd{mfp};

    IkbdRig() {
        ikbd.setScheduler(&sched);
        // Ce que Machine câble pour de vrai : sans ces trois rappels, les octets que
        // l'IKBD met en file ne sont JAMAIS livrés et le test lirait du vide.
        sched.setCallback(Scheduler::IKBD,    [this] { ikbd.onResetResponse(); });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        ikbd.write8(kIkbdCtrl, 0x03);          // master reset de l'ACIA
        ikbd.write8(kIkbdCtrl, 0x96);          // format 8N1, RX int armée
    }
    void send(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) { ikbd.write8(kIkbdData, b); run(11264); }
    }
    void run(int64_t cycles) {
        const int64_t end = sched.now() + cycles;
        // Comme la boucle Machine : servir chaque échéance. runTo ne rejoue
        // une source qu'une fois par lot ; un saut unique manquerait le stop TX.
        while (sched.nextDue() >= 0 && sched.nextDue() <= end)
            sched.runTo(sched.nextDue());
        sched.runTo(end);
    }
    // Draine ce que l'IKBD a mis en file : RDRF (bit0 du statut) = un octet prêt.
    std::vector<uint8_t> drain(int64_t cycles = 400000) {
        std::vector<uint8_t> out;
        const int64_t end = sched.now() + cycles;
        while (sched.now() < end) {
            if (ikbd.read8(kIkbdCtrl) & 0x01) out.push_back(ikbd.read8(kIkbdData));
            else sched.runTo(sched.now() + 512);
        }
        return out;
    }
};

}  // namespace

static void testIkbdProtocol() {
    std::printf("IKBD 6301 (protocole : longueurs, accumulation, paquets)\n");

    // --- 1. Interrogation joystick ($16) : $FD + 2 octets --------------------
    // La commande la plus simple qui RÉPOND : un octet en entrée, trois en sortie.
    {
        IkbdRig r;
        r.drain(200000);                        // vide la réponse de reset éventuelle
        r.send({0x16});
        const auto p = r.drain();
        checkInt("$16 (interrogate) : 3 octets de réponse", long(p.size()), 3);
        if (p.size() == 3) checkHex("$16 : en-tête $FD", p[0], 0xFD);
    }

    // --- 2. Une commande multi-octets n'agit qu'une fois COMPLÈTE ------------
    // $09 (AbsMouseMode) attend 5 octets. Envoyer l'opcode et deux paramètres ne
    // doit RIEN déclencher — et surtout, l'octet suivant reste un PARAMÈTRE, pas
    // une nouvelle commande. Une table de longueurs fausse désynchronise ici, et
    // le symptôme apparaît des milliers de cycles plus loin, dans un jeu.
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x09, 0x02, 0x80});             // 3 octets sur 5 : incomplet
        r.send({0x16});                         // 4e octet : PARAMÈTRE, pas interrogate
        const auto p = r.drain();
        checkInt("commande incomplète : aucune réponse", long(p.size()), 0);
    }

    // --- 3. … et elle agit dès qu'elle est complète --------------------------
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x09, 0x02, 0x80, 0x01, 0x90}); // AbsMouseMode complet (5 octets)
        r.send({0x0D});                         // ReadAbsMousePos → $F7 + 5 octets
        const auto p = r.drain();
        checkInt("$0D (pos. absolue) : 6 octets", long(p.size()), 6);
        if (p.size() == 6) checkHex("$0D : en-tête $F7", p[0], 0xF7);
    }

    // --- 4. Un opcode INCONNU est un NOP qui ne désynchronise pas ------------
    // Port de Hatari IKBD_RunKeyboardCommand : commande inconnue → tampon vidé.
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x55});                         // inconnu
        r.send({0x16});                         // doit être compris comme interrogate
        const auto p = r.drain();
        checkInt("opcode inconnu : NOP, la commande suivante passe", long(p.size()), 3);
    }

    // --- 5. PAUSE OUTPUT ($13) gèle la sortie ; TOUTE commande valide la lève -
    // Détail fidèle d'Hatari (« Any new valid command will unpause the output ») :
    // ce n'est PAS seulement $11 qui reprend. Un programme qui met en pause puis
    // envoie n'importe quelle commande doit revoir ses paquets.
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x13});                         // PAUSE OUTPUT
        r.ikbd.keyEvent(0x1E, true);            // une touche pressée
        checkInt("sous PAUSE : rien ne sort", long(r.drain(200000).size()), 0);
        r.send({0x08});                         // RelMouseMode : commande valide QUELCONQUE
        checkBool("toute commande valide lève la pause",
                  r.drain(200000).size() > 0, true);
    }

    // --- 6. La table des longueurs, opcode par opcode ------------------------
    // Recopiée de Hatari KeyboardCommands[] (ikbd.c:222-266). Une longueur fausse
    // ne se voit PAS à l'exécution normale : elle décale le flux de commandes du
    // jeu qui l'utilise, et de lui seul.
    {
        IkbdRig r;
        struct { uint8_t op; int len; } kTable[] = {
            {0x80,2},{0x07,2},{0x08,1},{0x09,5},{0x0A,3},{0x0B,3},{0x0C,3},{0x0D,1},
            {0x0E,6},{0x0F,1},{0x10,1},{0x11,1},{0x12,1},{0x13,1},{0x14,1},{0x15,1},
            {0x16,1},{0x17,2},{0x18,1},{0x19,7},{0x1A,1},{0x1B,7},{0x1C,1},{0x20,4},
            {0x21,3},{0x22,3},
            {0x87,1},{0x88,1},{0x89,1},{0x8A,1},{0x8B,1},{0x8C,1},{0x8F,1},{0x90,1},
            {0x92,1},{0x94,1},{0x95,1},{0x99,1},{0x9A,1},
        };
        int bad = 0;
        for (const auto& e : kTable)
            if (r.ikbd.commandLengthForTest(e.op) != e.len) {
                ++bad;
                std::printf("  FAIL longueur $%02X = %d (Hatari : %d)\n",
                            e.op, r.ikbd.commandLengthForTest(e.op), e.len);
            }
        checkInt("39 longueurs de commande == table Hatari", bad, 0);
        checkInt("opcode inconnu : longueur 0 (NOP)", r.ikbd.commandLengthForTest(0x55), 0);
    }

    // --- 7. $09 GARDE la position interne (ROM 6301 $FACB) -------------------
    // La doc du protocole annonce une remise à zéro (« Resets the ikbd maintained
    // X and Y coordinates »), mais la ROM ne copie que les bornes ($AA-$AD) ; Hatari
    // a raison. $0E pose la position et passe en mode absolu ($FB5F).
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x0E, 0x00, 0x00, 0xFA, 0x00, 0x96});   // position (250,150), mode absolu
        r.send({0x09, 0x00, 0xFF, 0x00, 0xFF});          // bornes 255 seulement
        r.send({0x0D});
        const auto p = r.drain();
        checkInt("$09 puis $0D : 6 octets", long(p.size()), 6);
        if (p.size() == 6) {
            checkHex("$09 : X conservé", (p[2] << 8) | p[3], 0xFA);
            checkHex("$09 : Y conservé", (p[4] << 8) | p[5], 0x96);
        }
    }

    // --- 8. $88 souris coupée : réponse COMPLÈTE de 8 octets ----------------
    // « Replies are padded to 8 bytes. » Hatari n'émet que $F6 quand la souris
    // est coupée ($12/$14/$17) : le flux se désynchronise. On rapporte le dernier
    // mode souris choisi.
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x14});                                  // joystick auto → souris coupée
        r.send({0x88});
        const auto p = r.drain();
        checkInt("$14 puis $88 : 8 octets", long(p.size()), 8);
        if (p.size() == 8) {
            checkHex("$88 : en-tête $F6", p[0], 0xF6);
            checkHex("$88 : dernier mode choisi (REL au boot)", p[1], 0x08);
        }
        r.send({0x09, 0x01, 0x3F, 0x00, 0xC7});          // ABS 319×199
        r.send({0x12});                                  // DISABLE MOUSE
        r.send({0x88});
        const auto q = r.drain();
        checkInt("ABS puis $12 puis $88 : 8 octets", long(q.size()), 8);
        if (q.size() == 8) {
            checkHex("$88 : mode ABS conservé", q[1], 0x09);
            checkHex("$88 : MaxX conservé", (q[2] << 8) | q[3], 0x013F);
        }
    }

    // --- 9. $9A : interrogation ($15) ≠ joysticks désactivés ($1A) ----------
    {
        IkbdRig r;
        r.drain(200000);
        r.send({0x15});
        r.send({0x9A});
        auto p = r.drain();
        checkInt("$15 puis $9A : 8 octets", long(p.size()), 8);
        if (p.size() == 8) checkHex("$15 : joysticks toujours disponibles", p[1], 0x00);
        r.send({0x1A});
        r.send({0x9A});
        p = r.drain();
        if (p.size() == 8) checkHex("$1A : joysticks désactivés", p[1], 0x1A);
        else checkInt("$1A puis $9A : 8 octets", long(p.size()), 8);
        r.send({0x15});
        r.send({0x9A});
        p = r.drain();
        if (p.size() == 8) checkHex("$15 après $1A : rallumés", p[1], 0x00);
        else checkInt("$15 puis $9A : 8 octets", long(p.size()), 8);
    }
}

// -----------------------------------------------------------------------------
//  Synthé GM intégré (GmSynth / TinySoundFont) — le pendant Linux/Windows du
//  DLSMusicDevice. Table de vérité du chemin complet : ouverture de la banque
//  livrée (roms/gm/TimGM6mb.sf2), octets ACIA → parseur → événements datés →
//  rendu découpé au cycle. « La note posée à mi-trame ne sonne QUE la 2ᵉ moitié »
//  est exactement le genre de dérive qu'aucun étalon pixel n'attraperait.
// -----------------------------------------------------------------------------
static void testGmSynth() {
    std::printf("Synthé GM intégré (TinySoundFont, roms/gm/)\n");
    checkBool("GmSynth::available()", GmSynth::available(), true);

    GmSynth gm;
    // run_all.py lance tout depuis la racine du dépôt : roms/gm est la banque LIVRÉE.
    if (!gm.open("roms/gm", 48000)) {
        ++g_fail;
        std::printf("  FAIL open(roms/gm) : %s\n", gm.lastError().c_str());
        return;
    }
    checkBool("une banque est nommée", !gm.soundFont().empty(), true);

    auto energy = [](const float* lr, int from, int to) {
        double e = 0; for (int i = 2 * from; i < 2 * to; ++i) e += double(lr[i]) * lr[i];
        return e;
    };
    // Une trame de 800 échantillons couvrant [0, 160000) cycles. Note posée au
    // cycle 80000 (pile à mi-trame) : la première moitié doit rester muette, la
    // seconde doit sonner — c'est la datation à l'échantillon qu'on éprouve.
    {
        std::vector<float> lr(1600, 0.0f);
        for (uint8_t b : {uint8_t(0x90), uint8_t(0x3C), uint8_t(0x64)}) gm.byteAt(b, 80000);
        gm.render(lr.data(), 800, 0, 160000);
        checkBool("1re moitié muette (note à mi-trame)", energy(lr.data(), 0, 390) == 0.0, true);
        checkBool("2e moitié sonne", energy(lr.data(), 410, 800) > 0.0, true);
    }
    // Percussions : canal 10 (0x99) → banque 128. Un .sf2 GM doit y répondre.
    {
        std::vector<float> lr(1600, 0.0f);
        for (uint8_t b : {uint8_t(0x99), uint8_t(0x26), uint8_t(0x64)}) gm.byteAt(b, 0);
        gm.render(lr.data(), 800, 0, 160000);
        checkBool("canal 10 percussif sonne", energy(lr.data(), 0, 800) > 0.0, true);
    }
    // Note-off par vélocité 0 (running status implicite du 0x90) : pas de crash,
    // et le silence revient une fois les queues de relâchement éteintes.
    {
        for (uint8_t b : {uint8_t(0x90), uint8_t(0x3C), uint8_t(0x00),
                          uint8_t(0x99), uint8_t(0x26), uint8_t(0x00)}) gm.byteAt(b, 0);
        std::vector<float> lr(9600, 0.0f);
        for (int i = 0; i < 40; ++i) {           // ~4 s : au-delà de tout release GM
            std::fill(lr.begin(), lr.end(), 0.0f);
            gm.render(lr.data(), 4800, 0, 960000);
        }
        checkBool("silence après note off + release", energy(lr.data(), 0, 4800) < 1e-9, true);
    }
    gm.close();
    checkBool("close() : plus ouvert", gm.isOpen(), false);
}

// -----------------------------------------------------------------------------
//  Scripts joystick (util/JoyScript.hpp) — la grammaire de --joy-script.
//  Elle décide de ce qu'un pilote EXTERNE peut demander à la machine : une
//  combinaison mal compilée, c'est un jeu qu'on ne sait pas jouer (le tir de
//  Rick Dangerous = feu+direction) ou, pire, une entrée silencieusement fausse
//  qui rendrait irreproductible un rejeu censé l'être. D'où le test à la valeur
//  près, et pas seulement au nombre de trames.
// -----------------------------------------------------------------------------
static void checkMasks(const char* what, const std::string& src,
                       const std::vector<uint8_t>& want) {
    std::vector<uint8_t> got;
    std::string err;
    if (!neost::joyscript::parse(src, got, err)) {
        ++g_fail;
        std::printf("  FAIL %-46s refusé : %s\n", what, err.c_str());
        return;
    }
    if (got == want) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s = [", what);
    for (const uint8_t b : got) std::printf("%02X ", b);
    std::printf("]\n%54s attendu [", "");
    for (const uint8_t b : want) std::printf("%02X ", b);
    std::printf("]\n");
}

static void checkRejected(const char* what, const std::string& src) {
    std::vector<uint8_t> got;
    std::string err;
    if (!neost::joyscript::parse(src, got, err)) { ++g_ok; return; }
    ++g_fail;
    std::printf("  FAIL %-46s ACCEPTÉ (%zu trames) — devait être refusé\n",
                what, got.size());
}

static void testJoyScript() {
    using namespace neost::joyscript;
    std::printf("scripts joystick (util/JoyScript.hpp)\n");

    // Rétro-compatibilité : les recettes existantes (Lethal Xcess « FFFF ») ne
    // doivent pas changer de sens en gagnant la nouvelle grammaire.
    checkMasks("\"FFFF\" (recette existante)", "FFFF", {0x80, 0x80, 0x80, 0x80});
    checkMasks("\"UDLRF.\" bits ST",           "UDLRF.", {0x01, 0x02, 0x04, 0x08, 0x80, 0x00});

    // Combinaisons : la raison d'être du lot. [DF] est bas+feu ($82), surtout
    // PAS la valeur hexa $DF — c'est le piège que le préfixe $ vient trancher.
    checkMasks("[UF] = feu + haut",            "[UF]", {0x81});
    checkMasks("[DF] = feu + bas (dynamite)",  "[DF]", {0x82});
    checkMasks("[UL] = diagonale",             "[UL]", {0x05});
    checkMasks("[$88] masque hexa brut",       "[$88]", {0x88});
    checkMasks("[0x0F] masque hexa 0x",        "[0x0F]", {0x0F});

    // Répétition : N est le TOTAL, pas un ajout.
    checkMasks("\"R*3\" répétition",           "R*3", {0x08, 0x08, 0x08});
    checkMasks("\"[UF]*2.\" sur groupe",       "[UF]*2.", {0x81, 0x81, 0x00});
    checkMasks("\"F*1\" répétition à 1",       "F*1", {0x80});
    checkMasks("\"F*0\" répétition à 0",       "F*0", {});

    // Confort des scripts en fichier : blancs ignorés, « # » commente la ligne.
    checkMasks("blancs et sauts de ligne",     " R\tR\n R ", {0x08, 0x08, 0x08});
    checkMasks("commentaire # jusqu'au \\n",   "# feu\nF F", {0x80, 0x80});
    checkMasks("script vide",                  "", {});

    // Refus : un script fautif doit échouer AVANT le boot, pas se taire. L'ancien
    // parseur traduisait tout token inconnu en « neutre » — une faute de frappe
    // devenait une entrée muette, donc un rejeu faussé sans le moindre message.
    checkRejected("'[' non fermé",             "R*30[DF");
    checkRejected("token inconnu 'Z'",         "R*30ZZ");
    checkRejected("'*' sans compte",           "R*");
    checkRejected("groupe vide '[]'",          "[]");
    checkRejected("masque hexa > $FF",         "[$1FF]");
    checkRejected("hexa invalide '[$ZZ]'",     "[$ZZ]");
    checkRejected("lettre inconnue en groupe", "[UX]");
    checkRejected("répétition démesurée",      "R*99999999");
    // Le plafond PAR TOKEN ne bornait pas le TOTAL : « R*10000000 » répété 200 fois
    // compilait 2 milliards de trames et 2,5 Go de RSS avant que quiconque ne s'en
    // aperçoive. C'est le total qui doit être borné.
    checkRejected("total > 10M trames",        "R*9000000 R*9000000");
    checkMasks("10M trames exactement acceptées", "R*10000000",
               std::vector<uint8_t>(10000000, 0x08));

    // parseHexU32 : partagé avec --probe/--hash-ram, donc testé ici aussi.
    uint32_t v = 0;
    checkBool("parseHexU32($FF8240)", parseHexU32("$FF8240", v) && v == 0xFF8240u, true);
    checkBool("parseHexU32(0x1000)",  parseHexU32("0x1000", v)  && v == 0x1000u,   true);
    checkBool("parseHexU32(466)",     parseHexU32("466", v)     && v == 0x466u,    true);
    checkBool("parseHexU32(\"\") refusé",   parseHexU32("", v),          false);
    checkBool("parseHexU32(xyz) refusé",    parseHexU32("xyz", v),       false);
    checkBool("parseHexU32(9 chiffres) refusé", parseHexU32("123456789", v), false);
}

int main() {
    testDongleTable();
    testCartridgeKey();
    testPortDevices();
    testYmEventDomain();
    testMidiTdre();
    testMidiInJitter();
    testGmSynth();
    testMidiHomonymes();
    testIkbdTdre();
    testIkbdProtocol();
    testRtcSecond();
    testBlitterTruthTable();
    testDmaSoundTruthTable();
    testFdcTruthTable();
    if (testSccBrg()) ++g_ok; else ++g_fail;
    if (testSccSerial()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpTx()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpClock()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiBreak()) g_fail += failures; else ++g_ok;
    if (testSccRts()) ++g_ok; else ++g_fail;
    if (testSccSync()) ++g_ok; else ++g_fail;
    if (const int failures = testIkbdMouseMonitor()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpRxFormat()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiTxFormat()) g_fail += failures; else ++g_ok;
    if (testSccTxFormat()) ++g_ok; else ++g_fail;
    if (testSccTxEnable()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpRxEnable()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdPause()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpUsartControl()) g_fail += failures; else ++g_ok;
    if (testSccBreak()) ++g_ok; else ++g_fail;
    if (const int failures = testIkbdOutputCapacity()) g_fail += failures; else ++g_ok;
    if (testSccClock()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpTurnaround()) g_fail += failures; else ++g_ok;
    if (testSccBrgClock()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpUnderrun()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcForceInterrupt()) g_fail += failures; else ++g_ok;
    if (testSccZeroCount()) ++g_ok; else ++g_fail;
    if (testSccRxParity()) ++g_ok; else ++g_fail;
    if (const int failures = testRtcBank()) g_fail += failures; else ++g_ok;
    if (const int failures = testRtcTimer()) g_fail += failures; else ++g_ok;
    if (const int failures = testRtcLongElapsed()) g_fail += failures; else ++g_ok;
    if (const int failures = testRtcElapsedDifferential()) g_fail += failures; else ++g_ok;
    if (const int failures = testRtcLongPhase()) g_fail += failures; else ++g_ok;
    if (testSccChannelReset()) ++g_ok; else ++g_fail;
    if (testSccMieIus()) ++g_ok; else ++g_fail;
    if (testSccAckPriority()) ++g_ok; else ++g_fail;
    if (testSccRxStop()) ++g_ok; else ++g_fail;
    if (testSccEchoTxClock()) ++g_ok; else ++g_fail;
    if (testSccEchoBrgConfig()) ++g_ok; else ++g_fail;
    if (testSccEchoBrgControl()) ++g_ok; else ++g_fail;
    if (const int failures = testBusMmioMirror()) g_fail += failures; else ++g_ok;
    if (const int failures = testBlitterResetRetention()) g_fail += failures; else ++g_ok;
    if (testSccExtendedRead()) ++g_ok; else ++g_fail;
    if (const int failures = testAcsiWriteDestination()) g_fail += failures; else ++g_ok;
    if (const int failures = testAcsiLargeBounds()) g_fail += failures; else ++g_ok;
    if (testSccRxErrorIrq()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpGpipComposite()) g_fail += failures; else ++g_ok;
    if (const int failures = testAcsiCapacityLimit()) g_fail += failures; else ++g_ok;
    if (testSccRxErrorMode()) ++g_ok; else ++g_fail;
    if (const int failures = testStxDeletedTrack()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxSectorRewrite()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxDataCrcWrite()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxSectorDam()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxRawTrackOverlay()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxRawTrackWrap()) g_fail += failures; else ++g_ok;
    if (const int failures = testStxReadAddressCrc()) g_fail += failures; else ++g_ok;
    if (const int failures = testIpfImage()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdAciaInit()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdAciaReset()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdSciReset()) g_fail += failures; else ++g_ok;
    if (testSccFirstErrorReset()) ++g_ok; else ++g_fail;
    if (const int failures = testDmaSoundMicrowireByte()) g_fail += failures; else ++g_ok;
    if (const int failures = testDmaSoundMicrowireTiming()) g_fail += failures; else ++g_ok;
    if (const int failures = testRtcWriteOnly()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiIrqTiming()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdIrqTiming()) g_fail += failures; else ++g_ok;
    if (const int failures = testGemdosSnapshot()) g_fail += failures; else ++g_ok;
    if (const int failures = testDesktopMouse()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpVectorRegister()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcTrackIdLocation()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpInternalLoopback()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcSectorSizeSwitch()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpRxOverrun()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpRxIrqTiming()) g_fail += failures; else ++g_ok;
    if (testSccExtStatus()) ++g_ok; else ++g_fail;
    if (const int failures = testMidiTxControl()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiTxReclock()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdTxControl()) g_fail += failures; else ++g_ok;
    if (testSccBreakStatus()) ++g_ok; else ++g_fail;
    if (const int failures = testMidiTxPhase()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdTxPhase()) g_fail += failures; else ++g_ok;
    if (testSccStatusModes()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpBreak()) g_fail += failures; else ++g_ok;
    if (testSccAutoEcho()) ++g_ok; else ++g_fail;
    if (const int failures = testStxWrittenTrack()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpAsyncBreak()) g_fail += failures; else ++g_ok;
    if (testSccEchoReceive()) ++g_ok; else ++g_fail;
    if (const int failures = testAcsiTransfer()) g_fail += failures; else ++g_ok;
    if (testSccLocalLoopback()) ++g_ok; else ++g_fail;
    if (const int failures = testMfpGpipLines()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcMediaChange()) g_fail += failures; else ++g_ok;
    if (testSccEomReset()) ++g_ok; else ++g_fail;
    if (const int failures = testMachineTimerBAer()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpMmioDecode()) g_fail += failures; else ++g_ok;
    if (testPsgOddWrite()) ++g_ok; else ++g_fail;
    if (const int failures = testDmaSoundMmioState()) g_fail += failures; else ++g_ok;
    if (testPsgReset()) ++g_ok; else ++g_fail;
    if (const int failures = testBlitterRmw()) g_fail += failures; else ++g_ok;
    if (const int failures = testMachineFdcMediaVbl()) g_fail += failures; else ++g_ok;
    if (const int failures = testGemdosDriveMapping()) g_fail += failures; else ++g_ok;
    if (const int failures = testAcsiPartitions()) g_fail += failures; else ++g_ok;
    if (const int failures = testGemdosDirectoryEntries()) g_fail += failures; else ++g_ok;
    if (const int failures = testGemdosDtaReuse()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpUnderrunClear()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcLiveMedia()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiRxPhase()) g_fail += failures; else ++g_ok;
    if (testSccAutoEnables()) ++g_ok; else ++g_fail;
    if (const int failures = testMachineMfpTx()) g_fail += failures; else ++g_ok;
    if (const int failures = testMidiRx()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpRxTiming()) g_fail += failures; else ++g_ok;
    if (testSccAsync()) ++g_ok; else ++g_fail;
    if (const int failures = testMidiSerial()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdAciaFormat()) g_fail += failures; else ++g_ok;
    if (const int failures = testMachineMidi()) g_fail += failures; else ++g_ok;
    if (const int failures = testMfpSerial()) g_fail += failures; else ++g_ok;
    if (testSccRxFifo()) ++g_ok; else ++g_fail;
    if (const int failures = testIkbdCpuSerial()) g_fail += failures; else ++g_ok;
    if (const int failures = testAcsiIrqTiming()) g_fail += failures; else ++g_ok;
    if (const int failures = testMachineScc()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcRegisters()) g_fail += failures; else ++g_ok;
    if (const int failures = testMmuMapping()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcSelection()) g_fail += failures; else ++g_ok;
    if (const int failures = testMmioFidelity()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdResponses()) g_fail += failures; else ++g_ok;
    if (const int failures = testFdcTiming()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdMonitor()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdMonitorMachine()) g_fail += failures; else ++g_ok;
    if (const int failures = testDmaSoundMono()) g_fail += failures; else ++g_ok;
    if (const int failures = testShifterStateGeometry()) g_fail += failures; else ++g_ok;
    if (const int failures = testIkbdAutoSend()) g_fail += failures; else ++g_ok;
    testPacing();
    testMmioTable();
    testTwoCpus();
    testJoyScript();
    testWindowsPaths();
    testPosixPaths();
    testNativeDefaults();
    testPrecomposeUtf8();
    testMouseScale();
    testMissingHwHint();
    testConfigParser();
    testRamDisplay();
    testConfigPath();
    testConfigWriteCreatesDir();
    testFixBools();
    testChannelMask();
    std::printf("[selftest-logic] %d OK, %d FAIL\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
