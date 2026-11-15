#!/usr/bin/env python3
# Relevé reproductible DE/LOAD, sans changer les sources RTL.
# Référence : GSTMCU 4a6db7ccf0e2bfbeaa90353717262185cf1d908d (MiSTery a00deb5b).
# Nécessite Verilator et le RTL de tools/setup_silicon_refs.sh.
# Le mode ST est activé après programmation en STE pour ne pas bloquer DTACK.
# Ce banc est un modèle, pas une mesure matérielle. Voir CASE_STUDIES.md, Ika.
from pathlib import Path
import subprocess
import argparse
root = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description="Relevé de la phase DE/LOAD du RTL GSTMCU épinglé (grade D)")
ap.add_argument('--rtl', type=Path, default=root/'extern/mistery/gstmcu')
ap.add_argument('--out', type=Path, default=root/'build/video-load-probe')
ap.add_argument('--st', action='store_true')
ap.add_argument('--reload', action='store_true', help='vérifier le reload du compteur pendant VSYNC (GSTMCU STE)')
a = ap.parse_args()
r = a.out.resolve(); r.mkdir(parents=True, exist_ok=True)
rtl = a.rtl.resolve()
src = (root/'tools/gstmcu_bench.cpp').read_text()
src=src.replace('    int prev = 0;\n    for (size_t i', '    int prev = 0, hsPrev = 1, loadPrev = 1, dePrev = 0, reloadPrev = 0, line = 0;\n    size_t hsTick = 0;\n    for (size_t i')
src=src.replace('        frame.push_back(uint16_t', '''        if (hsPrev && !tb->HSYNC_N) { ++line; hsTick = i; }
        hsPrev = tb->HSYNC_N;
        const int ld = r->ste_tb__DOT__dcyc_n, de = r->ste_tb__DOT__de;
        const int rel = r->ste_tb__DOT__gstshifter__DOT__reload;
        if (line == 100 && ((!loadPrev && ld) || de != dePrev || (!reloadPrev && rel)))
            std::printf("t=%.2f load=%d de=%d reload=%d addr=%06x\\n", (i-hsTick)/4.0, ld,de,rel,tb->ram_a*2);
        loadPrev=ld; dePrev=de; reloadPrev=rel;
        frame.push_back(uint16_t''')
src=src.replace('    // Deux trames pour laisser', f'    tb->st_mode = {int(a.st)};\n    // Deux trames pour laisser')
if a.reload:
    entry = 'int main(int argc, char** argv) {'
    assert entry in src
    src = src.replace(entry, entry + '    if (argc == 2 && std::strcmp(argv[1], "--reload") == 0) {\n        tb = new Vste_tb; setupProbe();\n        for (int i=0;i<200;++i) period();\n        const unsigned before = sweep::vidNow()*2;\n        const bool inSync = !tb->VSYNC_N;\n        writeReg(0xff8202, 0x34);\n        for (int i=0;i<16;++i) period();\n        const unsigned during = sweep::vidNow()*2;\n        const bool stillSync = !tb->VSYNC_N;\n        for (int i=0;i<16000;++i) period();\n        writeReg(0xff8202, 0x56);\n        for (int i=0;i<16;++i) period();\n        const unsigned after = sweep::vidNow()*2;\n        const bool ended = tb->VSYNC_N;\n        const unsigned expected = (kVideoBase & 0xff00ff) | 0x3400;\n        const bool ok = inSync && stillSync && ended && before == kVideoBase\n                     && during == expected && after == expected;\n        std::printf("reload: before=%06x during=%06x after=%06x %s\\n", before, during, after, ok ? "PASS" : "FAIL");\n        delete tb; return ok ? 0 : 1;\n    }\n')
(r/'load_probe.cpp').write_text(src)
tb=(rtl/'tb/ste_tb.v').read_text().replace('input clk32,', 'input clk32, input st_mode,').replace(".st(1'b0)",'.st(st_mode)').replace('.ste(1)', '.ste(!st_mode)');(r/'ste_tb.v').write_text(tb)
hdl=rtl/'hdl';names='gstmcu clockgen mcucontrol hsyncgen hdegen vsyncgen vdegen vidcnt sndcnt latch register modules gstshifter shifter_video shifter_video_async'.split()
cmd=['verilator','--cc','--exe','--build','-j','4','--public-flat-rw','--no-timing','-Wno-fatal','-Wno-lint','-Wno-style','-CFLAGS','-O2','--Mdir',str(r/'load-build'),'--top-module','ste_tb',str(r/'load_probe.cpp'),str(r/'ste_tb.v'),*[str(hdl/(n+'.v')) for n in names],'-y',str(rtl/'tb')]
with (r/'load-build.log').open('w') as f: subprocess.run(cmd,stdout=f,stderr=f,check=True,timeout=120)
(r/'load-vram.bin').write_bytes(bytes(32000))
args = ['--reload'] if a.reload else [str(r/'load-vram.bin'),str(r/'load-pixels.bin'),'0','0','1',*['0']*16]
with (r/'load-probe.log').open('w') as f:
    subprocess.run([str(r/'load-build/Vste_tb'), *args],stdout=f,stderr=f,check=True,timeout=120)
