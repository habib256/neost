// =============================================================================
//  GemdosHd.hpp — Émulation « disque dur GEMDOS » (port de Hatari gemdos.c).
//
//  Principe (identique à Hatari) : au lieu d'émuler un vrai contrôleur ACSI/IDE,
//  on INTERCEPTE les appels GEMDOS (trap #1) et on redirige les opérations de
//  fichier d'un lecteur virtuel (C:, D:…) vers un dossier de l'OS hôte. Les
//  programmes croient parler à un disque dur Atari ; en réalité tout passe par
//  fopen/fread/opendir… sur le système de fichiers hôte.
//
//  Mécanisme d'interception (port fidèle) : une CARTOUCHE système est exposée à
//  $FA0000 (octets assemblés de cart_asm.s). Au boot, le TOS exécute son C-INIT
//  qui installe un nouveau vecteur GEMDOS ($84) pointant dans la cartouche. Ce
//  code cartouche déclenche des opcodes « illégaux » magiques (8/9/10) que le
//  cœur 68000 (Cpu68k::run) capte et route ici :
//    - 8  GEMDOS   → trap()           : dispatch d'un appel GEMDOS
//    - 9  PEXEC    → pexecBpCreated()  : 2e phase d'un Pexec (chargement+reloc)
//    - 10 SYSINIT  → sysInit()         : installe le hook + le masque de lecteurs
//  Le handler pose les codes condition du SR (N/Z/V) que le code cartouche teste
//  pour décider : exécuter l'ancien vecteur GEMDOS (TOS), revenir (rte), ou Pexec.
//
//  Le cœur ne dépend PAS du GUI : cette émulation vit dans neost_core et tourne
//  donc à l'identique en headless. POSIX uniquement (Linux/macOS, comme Hatari).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

class Bus;
class Cpu68k;
class StateArchive;

class GemdosHd {
public:
    GemdosHd(Bus& bus, Cpu68k& cpu);
    ~GemdosHd();

    // Active l'émulation HD sur `hostDir` (dossier hôte). Selon le contenu :
    //  - dossier « normal »  → un lecteur après les partitions ACSI, mappé sur ce dossier ;
    //  - dossier ne contenant QUE des sous-dossiers d'une lettre C..Z → multi-
    //    partitions (C: = sous-dossier C, D: = sous-dossier D…), comme Hatari.
    // Installe la cartouche système ($FA0000) et branche bus.gemdos. À appeler
    // AVANT le reset/boot de la machine. Renvoie true si ≥1 lecteur a été mappé.
    bool setDirectory(const std::string& hostDir);

    // Démonte l'émulation HD : ferme les fichiers hôtes ouverts, retire la
    // cartouche système ($FA0000) et débranche bus.gemdos. Le TOS ne voit la
    // disparition des lecteurs qu'au prochain boot (hard reset conseillé).
    void unmount();

    // Configuration/cartouche installée ; driveMask peut être nul si ACSI occupe C..Z.
    bool active() const { return active_; }
    // Liaison moteur non persistée ; réévaluée au montage et au prochain boot/reset.
    void setPartitionCountProvider(std::function<int()> provider) { partitionCountProvider_ = std::move(provider); }
    // Copie du texte qu'un PROGRAMME écrit sur la console — Cconout, Cconws, Fwrite
    // sur le handle 1 non redirigé (trap #1), Bconout sur CON ou l'écran brut
    // (trap #13) —, relevée SANS rien intercepter : TOS l'affiche toujours. Les appels
    // venus de la ROM sont écartés (le GEMDOS du TOS passe lui-même par le BIOS, et
    // le desktop n'est pas le programme testé). Équivalent de la console de --tos none
    // d'Hatari, d'où les programmes de sa suite (cpu/, gemdos/) tirent leur verdict.
    // Non sérialisée.
    void setConsoleSink(std::function<void(const std::string&)> sink);
    // Démarrage automatique d'un programme après le boot (port de Hatari inffile.c,
    // option --auto) : au C-INIT de la cartouche, un DESKTOP.INF / NEWDESK.INF /
    // EMUDESK.INF VIRTUEL portant la ligne « #Z » est préparé, puis servi au Fopen
    // du desktop — même depuis A:, rien n'est écrit sur la disquette. Sans dossier
    // --gemdos, la cartouche système est installée sans lecteur (comme Hatari
    // AUTOSTART_INTERCEPT). TOS ≥ 1.04 / EmuTOS. Chemin sans lecteur → « C:\ ».
    // Renvoie false pour un chemin refusé (INF_SetAutoStart). Non sérialisé.
    bool setAutoStart(const std::string& name);
    const std::string& autoStartProgram() const { return autoPrg_; }
    // Liaison moteur (comme setPartitionCountProvider) : disquette dans A: ? Sert à
    // ouvrir la fenêtre du lecteur de boot dans l'INF par défaut (get_builtin_inf).
    void setFloppyProvider(std::function<bool()> provider) { floppyProvider_ = std::move(provider); }
    uint32_t driveMask() const;
    int firstMappedDrive() const;
    // PUBLIQUES : l'empreinte de cartouche des save-states (Machine.cpp) doit masquer
    // les octets que sysInit mute ici — la valeur ne doit pas être dupliquée en dur.
    static constexpr uint32_t CART_OLDGEMDOS = 0xFA0024u;
    static constexpr uint32_t CART_GEMDOS    = 0xFA002Au;


    // Reset du système de fichiers GEMDOS (ferme les fichiers ouverts, ré-init des
    // chemins courants). Appelé au reset machine. Port de GemDOS_Reset.
    void reset();

    // Fichiers hôtes hors snapshot : réouverture sans troncature, validation puis
    // remplacement atomique des handles. Aucun pointeur FILE* n'est sérialisé.
    void serialize(StateArchive& ar);

    // Appelé par Cpu68k::run quand un opcode magique (8/9/10) s'exécute DANS la
    // cartouche. Traite l'appel et renvoie true (l'appelant le consomme en NOP).
    bool handleOpcode(uint16_t opcode);

    // A39 (2026-08-28) — auto-test du BAC À SABLE : monte un dossier temporaire et
    // vérifie qu'une table de noms GEMDOS hostiles reste SOUS la racine. Même forme
    // que Shifter::glueSelfTest (verdict par code de sortie, détail sur stderr) ;
    // vit ici pour atteindre createHostFileName sans ouvrir l'API. Cf. son bandeau.
    bool sandboxSelfTest();

private:
    // ---- Composants 68000 -----------------------------------------------------
    Bus&    bus_;
    Cpu68k& cpu_;
    bool    active_ = false;

    // ---- Accès mémoire ST (port des helpers STMemory_* de Hatari) -------------
    uint8_t  readByte (uint32_t a);
    uint16_t readWord (uint32_t a);
    uint32_t readLong (uint32_t a);
    void     writeByte(uint32_t a, uint8_t  v);
    void     writeWord(uint32_t a, uint16_t v);
    void     writeLong(uint32_t a, uint32_t v);
    // Copie une chaîne ST NUL-terminée à `addr` dans une std::string (vide si la
    // mémoire est invalide). Port de STMemory_GetStringPointer.
    bool     getString(uint32_t addr, std::string& out);
    // Plage [addr,addr+size) entièrement en RAM (et éventuellement ROM) ?
    bool     checkArea(uint32_t addr, uint32_t size, bool allowRom = false);
    // Invalide le cache 16 Ko du Mega STE après écriture directe en RAM.
    void     flushCache();

    // ---- Registres 68000 (alias façon Regs[REG_x]/M68000_*) -------------------
    uint32_t dreg(int n)  { return reg(n); }
    uint32_t areg(int n)  { return reg(8 + n); }
    uint32_t reg(int idx);
    void     setDreg(int n, uint32_t v);
    void     setAreg(int n, uint32_t v);
    uint16_t getSR();
    void     setSR(uint16_t v);
    uint32_t getUSP();
    uint32_t getPC();

    // ---- État GEMDOS (port des variables globales de gemdos.c) ----------------
    static constexpr int MAX_HARDDRIVES  = 24;
    static constexpr int MAX_FILE_HANDLES = 64;
    static constexpr int BASE_FILEHANDLE  = 64;
    static constexpr int TOS_NAMELEN      = 14;
    static constexpr int FORCED_HANDLES   = 5;
    static constexpr uint32_t DTA_MAGIC_NUMBER = 0x12983476u;
    static constexpr int DTA_CACHE_INC = 256;
    static constexpr int DTA_CACHE_MAX = 16 * 1024;   // == DTA_CACHE_MAX_SIZE d'Hatari (gemdos.c:118)

    struct EmuDrive {
        std::string hdEmuDir;     // racine hôte du lecteur
        std::string fsCurrPath;   // chemin courant hôte (toujours terminé par '/')
        int  driveNumber = -1;    // C: = 2, D: = 3…
        bool used = false;
    };
    EmuDrive emudrives_[MAX_HARDDRIVES];
    int      nDrives_ = 0;        // nombre d'entrées emudrives_ peuplées

    struct FileHandle {
        bool  used = false;
        bool  readOnly = false;
        char  mode[4] = {0};      // "rb" / "rb+" / "wb+"
        uint32_t basepage = 0;
        FILE* fp = nullptr;
        std::string actualName;   // chemin hôte (pour Fdatime)
    };
    FileHandle fileHandles_[MAX_FILE_HANDLES];

    struct ForcedHandle { int handle = -1; uint32_t basepage = 0; };
    ForcedHandle forced_[FORCED_HANDLES];

    struct InternalDta {
        bool used = false;
        uint32_t addr = 0;                  // adresse ST de la DTA (réutilisation)
        int  centry = 0;                    // entrée courante (Fsnext)
        std::vector<std::string> found;     // noms d'entrées correspondantes
        std::string path;                   // dossier hôte
        char dta_attrib = 0;
    };
    std::vector<InternalDta> dtas_;
    uint16_t dtaIndex_ = 0;

    // Options (défauts façon Hatari) : write-protect off, dates Atari, casse
    // préservée, pas de conversion de jeu de caractères. trace_ activé par la
    // variable d'environnement NEOST_GEMDOS_TRACE (débogage).
    bool writeProtect_ = false;
    bool hostTime_     = false;
    int  caseConv_     = 0;        // 0=normal, 1=majuscules, 2=minuscules
    [[maybe_unused]] bool filenameConv_ = false;
    bool trace_        = false;

    uint16_t currentDrive_ = 0;   // lecteur courant (0=A, 2=C…)
    uint32_t actPd_ = 0;          // adresse du pointeur de basepage courant
    uint32_t callingPC_ = 0;      // PC de l'appelant GEMDOS
    uint32_t savedPexecParams_ = 0;
    bool     initGemdos_ = false;
    uint32_t connectedDriveMask_ = 0;
    int      bootDrive_ = 0;      // lecteur de boot (→ currentDrive_ au reset)

    // ---- Interception (port hatari-glue.c + GemDOS_Boot) ----------------------
    void boot();              // GemDOS_Boot : installe le hook $84
    void sysInit();           // OpCode_SysInit : masque lecteurs + boot
    int  trap();              // GemDOS_Trap : dispatch + codes condition SR
    void pexecBpCreated();    // GemDOS_PexecBpCreated : charge+relocalise le PRG

    // ---- Init / gestion des lecteurs & handles --------------------------------
    bool initDrives(const std::string& hostDir);
    void initCurPaths();
    void clearAllFileHandles();
    void closeFileHandle(int i);
    void unforceFileHandle(int i);
    void clearAllInternalDTAs();
    void clearInternalDTA(int idx);
    int  findFreeFileHandle();
    bool basepageMatches(uint32_t checkbase);
    int  getValidFileHandle(int handle);
    int  findDriveNumber(const std::string& name);
    bool isDriveEmulated(int drive);
    int  fileName2HardDriveID(const std::string& name);

    // ---- Conversion de chemins ST → hôte (port des helpers de gemdos.c) -------
    void createHostFileName(int drive, const std::string& gemName, std::string& out);
    // Rabat un chemin hôte sous la racine du lecteur (cf. .cpp) — garde-fou de sécurité.
    void clampToSandbox(const EmuDrive& d, const std::string& gemName, std::string& out);
    // Nombre de partitions images fourni par le moteur, zéro sans liaison.
    int partitionCount() const;
    bool addPathComponent(std::string& path, const std::string& origname, bool isDir);
    void addRemainingPath(const std::string& src, std::string& dstpath);
    std::string matchHostDirEntry(const std::string& path, const std::string& name,
                                  bool pattern, bool onlyInvalid = false);

    // ---- Opérations GEMDOS (un handler par appel, port 1:1) -------------------
    bool gemPterm0(uint32_t p);
    bool gemSetDrv(uint32_t p);
    bool gemDFree(uint32_t p);
    bool gemMkDir(uint32_t p);
    bool gemRmDir(uint32_t p);
    bool gemChDir(uint32_t p);
    bool gemCreate(uint32_t p);
    bool gemOpen(uint32_t p);
    bool gemClose(uint32_t p);
    bool gemRead(uint32_t p);
    bool gemWrite(uint32_t p);
    bool gemFDelete(uint32_t p);
    bool gemLSeek(uint32_t p);
    bool gemFattrib(uint32_t p);
    bool gemForce(uint32_t p);
    bool gemGetDir(uint32_t p);
    int  gemPexec(uint32_t p);
    bool gemPterm(uint32_t p);
    bool gemPtermres(uint32_t p);
    bool gemSFirst(uint32_t p);
    bool gemSNext(bool trace);
    bool gemRename(uint32_t p);
    bool gemGSDToF(uint32_t p);

    // ---- DTA / dates / attributs ----------------------------------------------
    enum class DtaResult { Ok, Skip, Error };
    DtaResult populateDTA(InternalDta& iDTA, const std::string& fname, uint32_t dtaGemdos);
    void terminateClose();

    int  loadAndReloc(const std::string& prgName, uint32_t baseaddr, bool fullBpSetup);
    std::function<int()> partitionCountProvider_; // liaison moteur, non sérialisée
    std::function<void(const std::string&)> consoleSink_; // idem
    std::function<bool()> floppyProvider_;               // idem
    // INF virtuel d'autostart (TosOverride d'inffile.c).
    std::string autoPrg_;                                 // « A:\EMAIL.PRG » ; vide = inactif
    FILE*       infFile_   = nullptr;                     // contenu virtuel (fichier temporaire)
    std::string infName_;                                 // nom que le TOS ouvre (sans lecteur)
    int         infCloses_ = 0;
    void installSystemCart();
    void createInfOverride();
    bool closeInfOverride(FILE* fp);
    bool infNameMatches(const std::string& name) const;
    void captureConsole(uint16_t call, uint32_t params);
    void captureBios();
    bool inRom(uint32_t pc) const;
    std::string mountedDirectory_;                // configuration hôte, pas un état invité
};
