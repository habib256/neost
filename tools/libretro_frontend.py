"""Frontend libretro minimal, sans fenêtre, pour éprouver neost_libretro.

Charge le module par ctypes et joue le rôle de RetroArch : environnement, image,
son, entrées, états, changement de disquette. Sur le modèle de
tools/libretro_frontend.py de POM2 (même auteur), adapté à NeoST.

Un module par processus : ctypes ne charge pas deux fois la même bibliothèque.

(c) 2026 VERHILLE Arnaud — projet NeoST.
"""

import ctypes as C
import hashlib
from pathlib import Path

ENVIRONMENT = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
INPUT_POLL = C.CFUNCTYPE(None)
INPUT_STATE = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
KEYBOARD = C.CFUNCTYPE(None, C.c_bool, C.c_uint, C.c_uint32, C.c_uint16)
LED = C.CFUNCTYPE(None, C.c_int, C.c_int)
MIDI_ENABLED = C.CFUNCTYPE(C.c_bool)
MIDI_READ = C.CFUNCTYPE(C.c_bool, C.POINTER(C.c_uint8))
MIDI_WRITE = C.CFUNCTYPE(C.c_bool, C.c_uint8, C.c_uint32)
MIDI_FLUSH = C.CFUNCTYPE(C.c_bool)


class MidiInterface(C.Structure):
    _fields_ = [("input_enabled", MIDI_ENABLED), ("output_enabled", MIDI_ENABLED),
                ("read", MIDI_READ), ("write", MIDI_WRITE), ("flush", MIDI_FLUSH)]

EXP = 0x10000
ENV = {
    "GET_CAN_DUPE": 3, "SET_MESSAGE": 6, "GET_SYSTEM_DIRECTORY": 9, "SET_PIXEL_FORMAT": 10,
    "SET_INPUT_DESCRIPTORS": 11, "SET_KEYBOARD_CALLBACK": 12, "SET_DISK_CONTROL_INTERFACE": 13,
    "GET_VARIABLE": 15, "SET_VARIABLES": 16, "GET_VARIABLE_UPDATE": 17, "SET_SUPPORT_NO_GAME": 18,
    "GET_LOG_INTERFACE": 27, "GET_SAVE_DIRECTORY": 31, "SET_SYSTEM_AV_INFO": 32,
    "SET_CONTROLLER_INFO": 35, "SET_GEOMETRY": 37, "GET_LED_INTERFACE": 46 | EXP,
    "GET_AUDIO_VIDEO_ENABLE": 47 | EXP, "GET_INPUT_BITMASKS": 51 | EXP,
    "GET_CORE_OPTIONS_VERSION": 52, "GET_DISK_CONTROL_INTERFACE_VERSION": 57,
    "SET_DISK_CONTROL_EXT_INTERFACE": 58, "GET_MESSAGE_INTERFACE_VERSION": 59,
    "SET_MESSAGE_EXT": 60, "SET_CORE_OPTIONS_V2": 67, "GET_LANGUAGE": 39,
    "SET_CORE_OPTIONS_V2_INTL": 68, "SET_MEMORY_MAPS": 36 | EXP, "GET_MIDI_INTERFACE": 48 | EXP,
}
CMD = {v: k for k, v in ENV.items()}

DEVICE_JOYPAD, DEVICE_MOUSE, DEVICE_ANALOG = 1, 2, 5
PAD = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
       "A": 8, "X": 9, "L": 10, "R": 11, "L2": 12, "R2": 13, "L3": 14, "R3": 15}
RETROK = {"RETURN": 13, "ESCAPE": 27, "SPACE": 32, "UP": 273, "DOWN": 274, "RIGHT": 275,
          "LEFT": 276, "F1": 282, "F2": 283}
RETROK.update({chr(c): c for c in range(ord("a"), ord("z") + 1)})
RETROK.update({str(d): ord(str(d)) for d in range(10)})


class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class Game(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


class SystemInfo(C.Structure):
    _fields_ = [("name", C.c_char_p), ("version", C.c_char_p), ("extensions", C.c_char_p),
                ("need_fullpath", C.c_bool), ("block_extract", C.c_bool)]


class Geometry(C.Structure):
    _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint), ("max_width", C.c_uint),
                ("max_height", C.c_uint), ("aspect", C.c_float)]


class Timing(C.Structure):
    _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]


class AvInfo(C.Structure):
    _fields_ = [("geometry", Geometry), ("timing", Timing)]


class DiskControlExt(C.Structure):
    _fields_ = [("set_eject_state", C.CFUNCTYPE(C.c_bool, C.c_bool)),
                ("get_eject_state", C.CFUNCTYPE(C.c_bool)),
                ("get_image_index", C.CFUNCTYPE(C.c_uint)),
                ("set_image_index", C.CFUNCTYPE(C.c_bool, C.c_uint)),
                ("get_num_images", C.CFUNCTYPE(C.c_uint)),
                ("replace_image_index", C.c_void_p), ("add_image_index", C.c_void_p),
                ("set_initial_image", C.c_void_p),
                ("get_image_path", C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_char_p, C.c_size_t)),
                ("get_image_label", C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_char_p, C.c_size_t))]


class Core:
    def __init__(self, module, system_dir, save_dir, options=None, av_enable=3, language=0):
        self.lib = C.CDLL(str(module))
        self.system = str(system_dir).encode()
        self.saves = str(save_dir).encode()
        self.options = {k: str(v).encode() for k, v in (options or {}).items()}
        self.options_changed = False
        self.av_enable = av_enable
        self.language = language           # RETRO_LANGUAGE_* (0 anglais, 2 français)
        self.option_labels = None          # (langue, libellé de la 1re option) reçus
        self.messages, self.geometries, self.av_infos = [], [], []
        self.frame = None            # (largeur, hauteur, octets XRGB8888 serrés)
        self.audio_frames = 0
        self.audio_energy = 0              # somme des |échantillons| (si measure_audio)
        self.measure_audio = False
        self.pad = {0: set(), 1: set()}
        self.mouse = [0, 0, False, False]
        self.right_stick = [0, 0]
        self.keys = set()                  # RETROK tenus sur le port clavier (relu)
        self.pointer = None                # (x, y) en -0x7FFF..0x7FFF tant que « touché »
        self.memory_maps = []              # (ptr, len) du 1er descripteur de chaque SET_MEMORY_MAPS
        self.keyboard = None
        self.disk = None
        self.led = {}
        self._keep = []
        self._cb = [ENVIRONMENT(self._env), VIDEO(self._video), AUDIO(lambda l, r: None),
                    AUDIO_BATCH(self._audio), INPUT_POLL(lambda: None), INPUT_STATE(self._input)]
        self._led_cb = LED(self._set_led)
        # MIDI de l'hôte : ce que le cœur écrit (octet, écart en µs), ce qu'on lui fait lire.
        self.midi_out = []
        self.midi_in = []
        self.midi_flushes = 0

        def midi_read(p):
            if not self.midi_in:
                return False
            p[0] = self.midi_in.pop(0)
            return True

        def midi_write(b, delta):
            self.midi_out.append((b, delta))
            return True

        def midi_flush():
            self.midi_flushes += 1
            return True
        self._midi = MidiInterface(MIDI_ENABLED(lambda: True), MIDI_ENABLED(lambda: True),
                                   MIDI_READ(midi_read), MIDI_WRITE(midi_write), MIDI_FLUSH(midi_flush))
        L = self.lib
        L.retro_set_environment(self._cb[0])
        L.retro_set_video_refresh(self._cb[1])
        L.retro_set_audio_sample(self._cb[2])
        L.retro_set_audio_sample_batch(self._cb[3])
        L.retro_set_input_poll(self._cb[4])
        L.retro_set_input_state(self._cb[5])
        L.retro_init()
        L.retro_serialize_size.restype = C.c_size_t
        L.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        L.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.restype = C.c_size_t

    # --- Environnement ----------------------------------------------------------------
    def _env(self, cmd, data):
        name = CMD.get(cmd)
        if name in ("GET_SYSTEM_DIRECTORY", "GET_SAVE_DIRECTORY"):
            buf = C.c_char_p(self.system if name == "GET_SYSTEM_DIRECTORY" else self.saves)
            self._keep.append(buf)
            C.cast(data, C.POINTER(C.c_char_p))[0] = buf.value
            return True
        if name == "SET_PIXEL_FORMAT":
            return C.cast(data, C.POINTER(C.c_int))[0] == 1   # XRGB8888
        if name == "GET_VARIABLE":
            var = C.cast(data, C.POINTER(Variable))[0]
            val = self.options.get(var.key.decode())
            if val is None:
                return False
            C.cast(data, C.POINTER(Variable))[0].value = val
            return True
        if name == "GET_VARIABLE_UPDATE":
            C.cast(data, C.POINTER(C.c_bool))[0] = self.options_changed
            self.options_changed = False
            return True
        if name == "GET_CORE_OPTIONS_VERSION":
            C.cast(data, C.POINTER(C.c_uint))[0] = 2
            return True
        if name == "SET_MEMORY_MAPS":
            descs = C.cast(data, C.POINTER(C.c_void_p))[0]            # retro_memory_map.descriptors
            words = C.cast(descs, C.POINTER(C.c_uint64))              # flags, ptr, offset, start, select, disconnect, len
            self.memory_maps.append((words[1], words[6]))
            return True
        if name == "GET_LANGUAGE":
            C.cast(data, C.POINTER(C.c_uint))[0] = self.language
            return True
        if name == "SET_CORE_OPTIONS_V2_INTL":
            us, local = C.cast(data, C.POINTER(C.c_void_p))[0], C.cast(data, C.POINTER(C.c_void_p))[1]
            chosen = local or us
            defs = C.cast(chosen, C.POINTER(C.c_void_p))[1]          # retro_core_options_v2.definitions
            desc = C.cast(defs, C.POINTER(C.c_char_p))[1]             # 1re définition, champ desc
            self.option_labels = ("local" if local else "us", desc.decode("utf-8"))
            return True
        if name in ("SET_CORE_OPTIONS_V2", "SET_SUPPORT_NO_GAME", "SET_INPUT_DESCRIPTORS",
                    "SET_CONTROLLER_INFO", "SET_VARIABLES"):
            return True
        if name == "GET_CAN_DUPE":
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        if name == "GET_INPUT_BITMASKS":
            return True
        if name == "GET_AUDIO_VIDEO_ENABLE":
            C.cast(data, C.POINTER(C.c_int))[0] = self.av_enable
            return True
        if name == "GET_MESSAGE_INTERFACE_VERSION":
            C.cast(data, C.POINTER(C.c_uint))[0] = 1
            return True
        if name in ("SET_MESSAGE_EXT", "SET_MESSAGE"):
            self.messages.append(C.cast(data, C.POINTER(C.c_char_p))[0].decode("utf-8", "replace"))
            return True
        if name == "SET_GEOMETRY":
            g = C.cast(data, C.POINTER(Geometry))[0]
            self.geometries.append((g.base_width, g.base_height, round(g.aspect, 4)))
            return True
        if name == "SET_SYSTEM_AV_INFO":
            a = C.cast(data, C.POINTER(AvInfo))[0]
            self.av_infos.append((a.geometry.base_width, a.geometry.base_height,
                                  round(a.geometry.aspect, 4), round(a.timing.fps, 4)))
            return True
        if name == "SET_KEYBOARD_CALLBACK":
            self.keyboard = C.cast(C.cast(data, C.POINTER(C.c_void_p))[0], KEYBOARD)
            return True
        if name == "GET_DISK_CONTROL_INTERFACE_VERSION":
            C.cast(data, C.POINTER(C.c_uint))[0] = 1
            return True
        if name == "SET_DISK_CONTROL_EXT_INTERFACE":
            self.disk = C.cast(data, C.POINTER(DiskControlExt))[0]
            return True
        if name == "GET_MIDI_INTERFACE":
            if data:
                C.memmove(data, C.addressof(self._midi), C.sizeof(MidiInterface))
            return True
        if name == "GET_LED_INTERFACE":
            C.cast(data, C.POINTER(C.c_void_p))[0] = C.cast(self._led_cb, C.c_void_p)
            return True
        return False

    def _set_led(self, led, state):
        self.led[led] = state

    def _video(self, data, width, height, pitch):
        if not data:
            return
        row = width * 4
        raw = C.string_at(data, pitch * (height - 1) + row)
        self.frame = (width, height, b"".join(raw[y * pitch:y * pitch + row] for y in range(height)))

    def _audio(self, data, frames):
        self.audio_frames += frames
        if self.measure_audio:
            from array import array
            pcm = array("h")
            pcm.frombytes(C.string_at(data, frames * 4))
            self.audio_energy += sum(abs(v) for v in pcm)
        return frames

    def _input(self, port, device, index, ident):
        if device == DEVICE_JOYPAD:
            held = self.pad.get(port, set())
            if ident == 256:   # RETRO_DEVICE_ID_JOYPAD_MASK
                return sum(1 << PAD[b] for b in held)
            return int(any(PAD[b] == ident for b in held))
        if device == 6 and port == 0:     # RETRO_DEVICE_POINTER : X, Y, PRESSED
            if self.pointer is None:
                return 0
            return [self.pointer[0], self.pointer[1], 1][ident] if ident < 3 else 0
        if device == 3:   # RETRO_DEVICE_KEYBOARD
            return int(port == 2 and ident in self.keys)
        if device == DEVICE_MOUSE and port == 0:
            return [self.mouse[0], self.mouse[1], int(self.mouse[2]), int(self.mouse[3])][ident] if ident < 4 else 0
        if device == DEVICE_ANALOG and port == 0 and index == 1:
            return self.right_stick[ident] if ident < 2 else 0
        return 0

    # --- Pilotage ---------------------------------------------------------------------
    def info(self):
        info = SystemInfo()
        self.lib.retro_get_system_info(C.byref(info))
        return info

    def load(self, path=None):
        game = None
        if path is not None:
            game = Game(str(path).encode(), None, 0, None)
            self._keep.append(game)
        ok = self.lib.retro_load_game(C.byref(game) if game else None)
        av = AvInfo()
        self.lib.retro_get_system_av_info(C.byref(av))
        self.av = (av.geometry.base_width, av.geometry.base_height, round(av.geometry.aspect, 4),
                   round(av.timing.fps, 4), av.timing.sample_rate)
        return bool(ok)

    def touch(self, fx, fy, hold=2, after=3):
        """Toucher bref à (fx, fy), fractions 0..1 de l'image montrée."""
        self.pointer = (int(fx * 0xFFFE) - 0x7FFF, int(fy * 0xFFFE) - 0x7FFF)
        self.run(hold)
        self.pointer = None
        self.run(after)

    def set_port(self, port, device):
        self.lib.retro_set_controller_port_device(port, device)

    def set_option(self, key, value):
        self.options[key] = str(value).encode()
        self.options_changed = True

    def run(self, frames=1):
        for _ in range(frames):
            self.lib.retro_run()

    def key(self, name, down):
        self.keyboard(down, RETROK[name], 0, 0)

    def tap(self, name, hold=3):
        self.key(name, True)
        self.run(hold)
        self.key(name, False)
        self.run(hold)

    def save_state(self):
        size = self.lib.retro_serialize_size()
        buf = C.create_string_buffer(size)
        if not self.lib.retro_serialize(buf, size):
            return None
        return buf.raw

    def load_state(self, blob):
        buf = C.create_string_buffer(blob, len(blob))
        return bool(self.lib.retro_unserialize(buf, len(blob)))

    def ram(self):
        return C.string_at(self.lib.retro_get_memory_data(2), self.lib.retro_get_memory_size(2))

    def frame_hash(self):
        return hashlib.sha1(self.frame[2]).hexdigest()[:12] if self.frame else None

    def write_ppm(self, path):
        w, h, raw = self.frame
        rgb = bytearray(w * h * 3)
        rgb[0::3] = raw[2::4]
        rgb[1::3] = raw[1::4]
        rgb[2::3] = raw[0::4]
        Path(path).write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb))

    def lit_fraction(self):
        w, h, raw = self.frame
        px = len(raw) // 4
        lit = sum(1 for i in range(0, len(raw), 4) if raw[i] | raw[i + 1] | raw[i + 2])
        return lit / px if px else 0.0

    def close(self):
        self.lib.retro_unload_game()
        self.lib.retro_deinit()
