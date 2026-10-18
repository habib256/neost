#!/usr/bin/env python3
"""Compare FMOVECR CIR aux sources Hatari épinglées : 128 offsets × 4 PREC × 4 RND.

Les tables et fpu_get_constant sont extraites sans réécriture de fpp.c ; les
arrondis sont ceux du vrai softfloat.c compilé. Les adapteurs seuls remplacent
les pointeurs du moteur et l'état CPU, car Hatari n'expose pas de CIR SFP004.
Les offsets $40–$7F exercent la fonction ROM ; le décodeur CPU Hatari les refuse
avant son appel (exception F-line). Ce banc ne simule pas ce décodeur CPU.

Usage : python3 tools/check_fmovecr_oracle.py
--fpu-source permet une mutation isolée de Fpu.cpp, sans modifier le dépôt.
Requiert extern/hatari à l'épingle, cc et c++ ; aucune ROM propriétaire.
"""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

PIN = 'f0736b24b32b0439300b52107ba6ab434469ec3c'
ROOT = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--fpu-source', type=Path, default=ROOT/'src/io/Fpu.cpp')
args = ap.parse_args()
root = ROOT
hatari = ROOT/'extern/hatari'
if not (hatari/'src/cpu/fpp.c').is_file():
    raise SystemExit('Sources Hatari absentes : lancer tools/setup_hatari.sh')
revision = subprocess.check_output(['git', '-C', str(hatari), 'rev-parse', 'HEAD'], text=True).strip()
if revision != PIN:
    raise SystemExit(f'Oracle non épinglé : {revision}, attendu {PIN}')
with tempfile.TemporaryDirectory(prefix='neost-fmovecr-') as work:
    work = Path(work)
    oracle_cpp = work/'oracle.cpp'
    s = (root/'extern/hatari/src/cpu/fpp.c').read_text()
    soft = (root/'extern/hatari/src/cpu/fpp_softfloat.c').read_text()
    def function(src, name):
        match = re.search(r'^(?:static\s+)?(?:bool|void|uae_u32)\s+' + re.escape(name) + r'\s*\(', src, re.MULTILINE)
        if match is None:
            raise SystemExit(f'Fonction oracle introuvable : {name}')
        start = match.start()
        brace = src.index('{', start)
        level = 1
        end = brace+1
        while level:
            level += (src[end] == '{') - (src[end] == '}')
            end += 1
        return src[start:end]
    tables = s[s.index('struct fpp_cr_entry {'):s.index('uae_u32 xhex_nan[]')]
    defines = s[s.index('#define FPSR_ZEROBITS'):s.index('static struct {', s.index('#define FPSR_ZEROBITS'))]
    prefix = '''#include <cstdint>
    #include <cstdio>
    extern "C" {
    #include "softfloat/softfloat.h"
    }
    using uae_u8=uint8_t; using uae_s8=int8_t; using uae_u16=uint16_t;
    using uae_u32=uint32_t; using uae_u64=uint64_t;
    struct fpdata { floatx80 fpx; };
    static struct { uint32_t fpcr, fpsr; } regs;
    static float_status fs;
    #define FPSR_SNAN 0x4000
    #define FPSR_OPERR 0x2000
    #define FPSR_DZ 0x0400
    #define FPSR_OVFL 0x1000
    #define FPSR_UNFL 0x0800
    #define FPSR_INEX2 0x0200
    #define FPSR_INEX1 0x0100
    static void fpsr_set_exception(uint32_t v) { regs.fpsr |= v; }
    static bool fpp_is_init(fpdata*) { return false; }
    static bool fpp_is_neg(fpdata* p) { return p->fpx.high & 0x8000; }
    static bool fpp_is_nan(fpdata* p) { return floatx80_is_any_nan(p->fpx); }
    static bool fpp_is_zero(fpdata* p) { return (p->fpx.high&0x7fff)<0x7fff && p->fpx.low==0; }
    static bool fpp_is_infinity(fpdata* p) { return (p->fpx.high&0x7fff)==0x7fff && (p->fpx.low<<1)==0; }
    #define fpp_to_exten_fmovem to_exten_fmovem
    #define fpp_from_exten_fmovem from_exten_fmovem
    #define fpp_round32 fp_round32
    #define fpp_round64 fp_round64
    '''
    parts = [prefix, tables, defines]
    for name in ['to_exten_fmovem','from_exten_fmovem','fp_round32','fp_round64','fp_get_status']:
        parts.append(function(soft, name))
    for name in ['fpsr_set_result_always','fpsr_set_result','fpu_get_constant']:
        parts.append(function(s, name))
    parts.append('''int main() {
     const int precision[4]={80,32,64,64}, rounding[4]={0,3,1,2};
     for(int p=0;p<4;++p) for(int r=0;r<4;++r) for(int off=0;off<128;++off) {
      fs={}; fs.float_detect_tininess=1; fs.floatx80_rounding_precision=precision[p]; fs.float_rounding_mode=rounding[r];
      regs.fpcr=(p<<6)|(r<<4); regs.fpsr=0;
      fpdata value{}; fpu_get_constant(&value,off); fp_get_status(&regs.fpsr);
      if(regs.fpsr&FPSR_OVFL) regs.fpsr|=FPSR_AE_OVFL;
      if(regs.fpsr&(FPSR_OVFL|FPSR_INEX2|FPSR_INEX1)) regs.fpsr|=FPSR_AE_INEX;
      if(regs.fpsr&(0x8000|FPSR_SNAN|FPSR_OPERR)) regs.fpsr|=FPSR_AE_IOP;
      if((regs.fpsr&FPSR_UNFL)&&(regs.fpsr&FPSR_INEX2)) regs.fpsr|=FPSR_AE_UNFL;
      if(regs.fpsr&FPSR_DZ) regs.fpsr|=FPSR_AE_DZ;
      std::printf("%02x %d %d %04x %016llx %08x\\n",off,p,r,value.fpx.high,(unsigned long long)value.fpx.low,regs.fpsr);
     }
    }
    ''')
    oracle_cpp.write_text('\n'.join(parts))

    cir_cpp = work/'cir.cpp'
    cir_cpp.write_text(r'''
#include "Fpu.hpp"
#include <cstdio>
static Fpu f;
static void cmd(uint16_t v) { f.write8(0xfffa4a,v>>8); f.write8(0xfffa4b,v); }
static void put(uint32_t v) { for(int i=3;i>=0;--i) f.write8(0xfffa50+3-i,v>>(i*8)); }
static uint32_t get() { uint32_t v=0;for(int i=0;i<4;++i) v=(v<<8)|f.read8(0xfffa50+i);return v; }
int main() {
 for(int p=0;p<4;++p) for(int r=0;r<4;++r) for(int off=0;off<128;++off) {
  f.reset(); cmd(0x9000);put((p<<6)|(r<<4));cmd(0x5c00|off);
  cmd(0xa800);uint32_t status=get();cmd(0x6800);
  uint32_t se=get();uint64_t man=get();man=(man<<32)|get();
  std::printf("%02x %d %d %04x %016llx %08x\n",off,p,r,se>>16,(unsigned long long)man,status);
 }
}
''')
    sf_object = work/'softfloat.o'
    oracle_bin, cir_bin = work/'oracle', work/'cir'
    def run(argv):
        return subprocess.run([str(a) for a in argv], check=True, capture_output=True, text=True)
    try:
        run([os.environ.get('CC', 'cc'), '-O2', '-I', hatari/'src/cpu', '-c',
             hatari/'src/cpu/softfloat/softfloat.c', '-o', sf_object])
        run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-I', hatari/'src/cpu',
             oracle_cpp, sf_object, '-o', oracle_bin])
        run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-I', ROOT/'src/io',
             '-I', ROOT/'src', cir_cpp, args.fpu_source.resolve(), '-o', cir_bin])
        oracle = run([oracle_bin]).stdout.splitlines()
        actual = run([cir_bin]).stdout.splitlines()
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.stderr or str(exc)) from exc
    if len(oracle) != 2048 or len(actual) != 2048:
        raise SystemExit(f'Nombre de cas invalide : Hatari={len(oracle)}, NeoST={len(actual)}')
    mismatches = [(ref, got) for ref, got in zip(oracle, actual) if ref != got]
    for ref, got in mismatches[:8]:
        print(f'Hatari: {ref}\nNeoST : {got}')
    print(f'FMOVECR oracle {PIN[:8]}: {len(oracle)} cas, {len(mismatches)} écarts')
    raise SystemExit(bool(mismatches))
