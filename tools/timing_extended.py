"""Sondes invitées supplémentaires : Timer B vidéo, DMA FDC et blitter."""
import struct

EXTENDED = ("timer_b_event", "fdc_read", "fdc_read_stx", "fdc_write", "blitter_hog", "blitter_shared",
            "blitter_shared_small", "blitter_shared_large", "blitter_shared_mulu",
            "blitter_hog_mulu", "blitter_hog_tas", "blitter_shared_tas",
            "blitter_shared_ram", "blitter_hog_ram", "cpu_div_ram")

# Couples (dividende, diviseur) de cpu_div_ram : durées DIVU/DIVS variées, donc
# les deux phases du créneau RAM de 4 cycles avant le préfetch, débordements compris.
DIV_PAIRS = [(0x00000000, 1), (0x0000ffff, 1), (0x00010000, 2), (0x7fffffff, 0x7fff),
             (0x12345678, 0x1234), (0x00ff00ff, 0x00ff), (0xfffffffe, 0xffff),
             (0x80000000, 0x8000), (0x00000005, 3), (0x0001ffff, 0x0003),
             (0x55555555, 0x5555), (0x2aaaaaaa, 0xaaaa), (0x00123456, 0x0789),
             (0xffff0000, 0x0001), (0x00007fff, 0xffff), (0x87654321, 0x4321),
             (0x0000a5a5, 0x005a), (0x3c3c3c3c, 0x3c3c), (0x00000001, 0xfffe),
             (0x0fedcba9, 0x7654), (0xc0000000, 0x0002), (0x00012345, 0x0013),
             (0x7ffe0001, 0x7fff), (0x00f0f0f0, 0x0f0f)]


class Assembler:
    def __init__(self, base=0xfa0000):
        self.base = base
        self.code = bytearray(struct.pack(">I", 0xFA52235F))
        self.labels = {}
        self.fixups = []

    def w(self, *words):
        for word in words:
            self.code.extend(struct.pack(">H", word & 0xffff))

    def long(self, value):
        self.code.extend(struct.pack(">I", value))

    def byte(self, address, value):
        self.w(0x11fc, value, address)

    def word(self, address, value):
        self.w(0x31fc, value, address)

    def address(self, address, value):
        self.w(0x21fc); self.long(value); self.w(address)

    def label(self, name):
        self.labels[name] = self.base + len(self.code)

    def branch(self, opcode, name):
        self.w(opcode)
        self.fixups.append((len(self.code), name, 2))
        self.w(0)

    def vector(self, address, name):
        self.w(0x21fc)
        self.fixups.append((len(self.code), name, 4))
        self.long(0); self.w(address)

    def finish(self):
        for offset, name, size in self.fixups:
            value = self.labels[name]
            if size == 2:
                struct.pack_into(">h", self.code, offset, value - (self.base + offset))
            else:
                struct.pack_into(">I", self.code, offset, value)
        return bytes(self.code), self.labels


def build_extended(case):
    in_ram = case.endswith("_ram")
    a = Assembler(0x10000 if in_ram else 0xfa0000)
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    a.byte(0x820a, 2)
    for address in (0xfa07, 0xfa09, 0xfa13, 0xfa15, 0xfa19, 0xfa1b, 0xfa1d,
                    0xfa03, 0xfa05):
        a.byte(address, 0)
    a.byte(0xfa17, 0x40)
    if case == "timer_b_event":
        # La trame de reset est une transition 60→50 Hz. Attendre deux VBL
        # avant d'exercer DE ; comparer cette trame transitoire au régime PAL
        # ajouterait une différence de nombre de lignes sans rapport avec TB.
        a.vector(0x70, "vbl")
        a.w(0x4e72, 0x2300, 0x4e72, 0x2300, 0x46fc, 0x2700)
        a.vector(0x120, "handler")
        a.byte(0xfa07, 1); a.byte(0xfa13, 1); a.byte(0xfa21, 1)
        a.label("arm"); a.byte(0xfa1b, 8)
        a.label("idle"); a.w(0x4e72, 0x2500); a.branch(0x6000, "idle")
        a.label("handler"); a.w(0x4e73)
        a.label("vbl"); a.w(0x4e73)
    elif case.startswith("blitter_"):
        rows = 1 if case.endswith("small") else 16 if case.endswith("large") else 8
        words = 16 * rows
        a.vector(0x10c, "irq")  # GPU_DONE = GPIP3, canal 3
        a.byte(0xfa09, 8); a.byte(0xfa15, 8)
        for address in (0x8a20, 0x8a22, 0x8a2e, 0x8a30):
            a.word(address, 2)
        for address in (0x8a28, 0x8a2a, 0x8a2c):
            a.word(address, 0xffff)
        a.byte(0x8a3a, 2); a.byte(0x8a3b, 3); a.byte(0x8a3d, 0)
        # Motif source, destination remise à zéro à chaque blit.
        a.w(0x207c); a.long(0x18000); a.w(0x303c, words-1)
        a.label("fill"); a.w(0x30fc, 0x5aa5); a.branch(0x51c8, "fill")
        if case.endswith("mulu"):
            a.w(0x7203, 0x7407)         # multiplicande fixe, coût MULU reproductible
        a.w(0x3e3c, 71, 0x46fc, 0x2500)  # huit échauffements + 64 transferts
        a.label("loop")
        a.w(0x207c); a.long(0x19000); a.w(0x303c, words-1)
        a.label("clear"); a.w(0x4258); a.branch(0x51c8, "clear")
        a.address(0x8a24, 0x18000); a.address(0x8a32, 0x19000)
        a.word(0x8a36, 16); a.word(0x8a38, rows)
        if case.endswith("tas"):
            a.byte(0x8a3c, 0x40 if case.startswith("blitter_hog") else 0)
            a.label("arm"); a.w(0x4af8, 0x8a3c)  # TAS pose BUSY sous verrou RMW
        else:
            a.label("arm"); a.byte(0x8a3c, 0xc0 if case.startswith("blitter_hog") else 0x80)
        a.label("poll")
        if case.endswith("mulu"):
            a.w(0xc4c1)                 # mulu.w d1,d2 pendant le partage de bus
        a.w(0x0838, 7, 0x8a3c); a.branch(0x6600, "poll")
        a.label("handler"); a.w(0x4e71)  # transfert observé terminé par le CPU
        a.w(0x207c); a.long(0x19000); a.w(0x303c, words-1)
        a.label("verify"); a.w(0x0c58, 0x5aa5); a.branch(0x6600, "failure")
        a.branch(0x51c8, "verify")
        a.branch(0x51cf, "loop")
        a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
        a.label("failure"); a.w(0x4e72, 0x2700); a.branch(0x6000, "failure")
        a.label("irq"); a.w(0x4e73)
    elif case in ("fdc_read", "fdc_read_stx"):
        a.vector(0x11c, "irq")  # GPIP5, fin de commande FDC
        a.byte(0xfa09, 0x80); a.byte(0xfa15, 0x80)
        a.byte(0x8800, 14); a.byte(0x8802, 5)  # A:, face 0
        a.w(0x207c); a.long(0x18000); a.w(0x303c, 255)
        a.label("clear"); a.w(0x4258); a.branch(0x51c8, "clear")
        # Toggle du sens DMA : vider la FIFO avant le compte de secteurs.
        a.word(0x8606, 0x190); a.word(0x8606, 0x90); a.word(0x8604, 1)
        for address, value in ((0x8609, 1), (0x860b, 0x80), (0x860d, 0)):
            a.byte(address, value)
        a.word(0x8606, 0x82); a.word(0x8604, 0)  # piste 0
        a.word(0x8606, 0x84); a.word(0x8604, 2)  # secteur 2
        a.word(0x8606, 0x80)
        a.w(0x207c); a.long(0x18000); a.w(0x3e3c, 31)
        a.label("arm"); a.word(0x8604, 0x88)  # READ SECTOR, sans attente spin-up
        a.label("poll"); a.w(0x0c50, 0x5aa5); a.branch(0x6600, "poll")
        a.label("handler"); a.w(0x4e71)  # première donnée de la rafale de 16 octets
        a.w(0x41e8, 16); a.branch(0x51cf, "poll")
        a.w(0x4e72, 0x2500)
        # Après IRQ, contrôle de TOUS les octets et du statut WD1772.
        a.w(0x3038, 0x8604, 0x0240, 0x001c); a.branch(0x6600, "failure")
        a.w(0x207c); a.long(0x18000); a.w(0x303c, 255)
        a.label("verify"); a.w(0x0c58, 0x5aa5); a.branch(0x6600, "failure")
        a.branch(0x51c8, "verify")
        a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
        a.label("failure"); a.w(0x4e72, 0x2700); a.branch(0x6000, "failure")
        a.label("irq"); a.w(0x4e73)
    elif case == "fdc_write":
        # En écriture, le DMA LIT la RAM : la scruter ne montrerait rien. Le
        # compteur d'adresse $FF860D, relisible, avance de 16 à chaque rafale.
        a.vector(0x11c, "irq")  # GPIP5, fin de commande FDC
        a.byte(0xfa09, 0x80); a.byte(0xfa15, 0x80)
        a.byte(0x8800, 14); a.byte(0x8802, 5)  # A:, face 0
        a.w(0x207c); a.long(0x18000); a.w(0x303c, 255)
        a.label("fill"); a.w(0x30fc, 0xa55a); a.branch(0x51c8, "fill")
        # Toggle du sens DMA vers l'ÉCRITURE (bit 8) : vide la FIFO ; le bit 8
        # reste ensuite levé pour ne pas la réinitialiser une seconde fois.
        a.word(0x8606, 0x90); a.word(0x8606, 0x190); a.word(0x8604, 1)
        for address, value in ((0x8609, 1), (0x860b, 0x80), (0x860d, 0)):
            a.byte(address, value)
        a.word(0x8606, 0x182); a.word(0x8604, 0)  # piste 0
        a.word(0x8606, 0x184); a.word(0x8604, 2)  # secteur 2
        a.word(0x8606, 0x180)
        a.w(0x7c00, 0x1038, 0x860d, 0x3e3c, 31)   # d6 drapeau IRQ, d0 adresse basse
        a.label("arm"); a.word(0x8604, 0xa8)  # WRITE SECTOR, sans attente spin-up
        a.label("poll"); a.w(0xb038, 0x860d); a.branch(0x6700, "poll")
        a.label("handler"); a.w(0x4e71)  # rafale de 16 octets lue en RAM
        a.w(0x1038, 0x860d); a.branch(0x51cf, "poll")
        # Les 16 derniers octets et le CRC partent APRÈS la dernière rafale.
        a.w(0x4e72, 0x2500)
        a.label("wait"); a.w(0x4a46); a.branch(0x6700, "wait")
        # Statut : ni protection, ni RNF, ni CRC, ni donnée perdue.
        a.w(0x3038, 0x8604, 0x0240, 0x005c); a.branch(0x6600, "failure")
        a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
        a.label("failure"); a.w(0x4e72, 0x2700); a.branch(0x6000, "failure")
        a.label("irq"); a.w(0x7c01, 0x4e73)
    elif case == "cpu_div_ram":
        # Ordre cycles internes / préfetch de DIVU et DIVS : la durée d'une
        # itération dépend de la phase du créneau RAM au moment du préfetch.
        for opcode, mark in ((0x82c2, "mark_divu"), (0x87c2, "mark_divs")):
            a.w(0x41f9); a.fixups.append((len(a.code), "table", 4)); a.long(0)
            a.w(0x3e3c, len(DIV_PAIRS) - 1)
            a.label("loop_" + mark)
            a.w(0x2218, 0x3418, 0x2601)     # move.l (a0)+,d1 ; move.w (a0)+,d2 ; d3=d1
            a.label(mark); a.w(opcode)      # divu.w d2,d1 / divs.w d2,d3
            a.branch(0x51cf, "loop_" + mark)
        a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
        a.label("table")
        for dividend, divisor in DIV_PAIRS:
            a.long(dividend); a.w(divisor)
    else:
        raise ValueError(case)
    data, pcs = a.finish()
    if in_ram:
        # Cartouche trampoline : copier le programme sous 128 Ko, puis le lancer
        # en RAM ST. Les mêmes instructions exercent alors les créneaux GLUE.
        stub = struct.pack(">I14H", 0xfa52235f,
                           0x207c, 0x00fa, 0x0020, 0x227c, 0x0001, 0x0000,
                           0x303c, len(data)-1, 0x12d8, 0x51c8, 0xfffc,
                           0x4ef9, 0x0001, 0x0004)
        assert len(stub) == 32
        data = stub + data
    return data, pcs


# Piste STX de fdc_read_stx : IDs à des angles non standard, et un ReadTime
# explicite pour le secteur lu (µs pour tout le secteur ; 0 = 32 µs par octet).
STX_SECTOR_BYTE = [100 + s * 660 for s in range(9)]
STX_READ_TIME = 20000


def crc16_ccitt(data, crc=0xffff):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xffff
    return crc


def stx_image():
    """Image Pasti d'une seule piste (0, face 0) à blocs secteur explicites."""
    sectors = 9
    data = bytearray()
    for s in range(sectors):
        data += (b"\x5a\xa5" if s == 1 else bytes([0xd0 + s, 0])) * 256
    descriptors = bytearray()
    for s in range(sectors):
        number = s + 1
        crc = crc16_ccitt(bytes([0xa1, 0xa1, 0xa1, 0xfe, 0, 0, number, 2]))
        descriptors += struct.pack("<IHH", s * 512, STX_SECTOR_BYTE[s] * 8,
                                   STX_READ_TIME if number == 2 else 0)
        descriptors += bytes([0, 0, number, 2]) + struct.pack(">H", crc) + bytes([0, 0])
    block = 16 + len(descriptors) + len(data)
    track = struct.pack("<IIHHHBB", block, 0, sectors, 0x01, 6250, 0, 0)  # SECTOR_BLOCK
    header = b"RSY\0" + struct.pack("<HHHBB", 3, 1, 0, 1, 2) + bytes(4)
    return header + track + descriptors + bytes(data)


def disk_image():
    """Disquette DD de géométrie standard, secteur 2 au motif connu."""
    data = bytearray(720 * 1024)
    struct.pack_into("<H", data, 11, 512)
    struct.pack_into("<H", data, 19, 1440)
    struct.pack_into("<H", data, 24, 9)
    struct.pack_into("<H", data, 26, 2)
    data[512:1024] = b"\x5a\xa5" * 256
    return bytes(data)
