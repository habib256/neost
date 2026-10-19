#!/usr/bin/env python3
"""Compare FSQRT/FGETEXP/FGETMAN/FMOD/FREM/FSCALE/FCMP/FTST par CIR à Hatari.

Résultats étendus, FPSR (exceptions et codes de condition), octet quotient et
préservation du registre pour FCMP/FTST. Les entrées comprennent les limites
binaires, NaN/SNaN, infinis, dénormalisés et 40 motifs déterministes supplémentaires.
Usage : python3 tools/test_fpu_arithmetic_oracle.py [--check-mutation]
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

PROBE = r"""
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "io/Fpu.hpp"
extern "C" {
#include "softfloat/softfloat.h"
}
static Fpu f;
static void cmd(uint16_t v) { f.write8(0xfffa4a,v>>8);f.write8(0xfffa4b,v); }
static void put(uint32_t v) { for(int i=3;i>=0;--i)f.write8(0xfffa50+(3-i),v>>(8*i)); }
static uint32_t get() {uint32_t v=0;for(int i=0;i<4;++i)v=(v<<8)|f.read8(0xfffa50+i);return v;}
static void ctrl(uint16_t c,uint32_t v){cmd(c);put(v);}
static void operand(floatx80 a){put(uint32_t(a.high)<<16);put(a.low>>32);put(a.low);}
static uint32_t fold(uint8_t flags){
 uint32_t r=0;
 if(flags&float_flag_signaling)r|=0x4080;
 if(flags&float_flag_invalid)r|=0x2080;
 if(flags&float_flag_overflow)r|=0x1048;
 if(flags&float_flag_underflow)r|=0x800;
 if(flags&float_flag_inexact)r|=0x208;
 if((flags&float_flag_underflow)&&(flags&float_flag_inexact))r|=0x20;
 return r;
}
int main(){
 const floatx80 cases[]={
  {0,0},{0x8000,0},{0,1},{0x8000,1},{0,0x4000000000000000},{0,0x8000000000000000},
  {0x3fff,0x800000c000000000},{0xbfff,0x800000c000000000},
  {0x3ffe,0x8000000000000000},{0x3ffe,0x8000000000000001},
  {0xbffe,0x8000000000000001},{0x4000,0xa000000000000000},
  {0x4000,0xe000000000000000},{0x403d,0xffffffffffffffff},
  {0x3fff,0x8000008000000000},{0xbfff,0x8000008000000000},
  {0x3fff,0x8000000000000600},{0xbfff,0x8000000000000600},
  {0x3f37,0x8000000000000000},{0xbf37,0x8000000000000000},
  {0x40c7,0x8000000000000000},{0xc0c7,0x8000000000000000},
  {0x47cf,0x8000000000000000},{0xc7cf,0x8000000000000000},
  {0x3f81,0x8000000000000000},{0x3f80,0x8000000000000000},
  {0x43fe,0xffffffffffffffff},{0x407e,0xffffffffffffffff},
  {0x7fff,0},{0xffff,0},{0x7fff,0x8000000000000000},{0xffff,0x8000000000000000},
  {0x7fff,0xc00000000badf00d},{0xffff,0xc00000000badf00d},
  {0x7fff,0x800000000badf00d},{0xffff,0x800000000badf00d},
 };
 std::vector<floatx80> inputs(std::begin(cases),std::end(cases));
 uint64_t seed=0x6888168881ULL;
 auto next=[&](){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
 for(int i=0;i<40;++i){const uint16_t h=uint16_t(next());const uint64_t m=next()|0x8000000000000000ULL;inputs.push_back({h,m});}
 const int precisions[]={80,32,64,64};
 const int modes[]={float_round_nearest_even,float_round_to_zero,float_round_down,float_round_up};
 int count=0,fail=0;
 for(int op:{4,30,31,33,37,38,56,58})for(int prec=0;prec<4;++prec)for(int rm=0;rm<4;++rm)for(auto raw:inputs)for(auto b:inputs){auto a=raw;
  f.reset();ctrl(0x9000,0);ctrl(0x8800,0);cmd(0x4800);operand(a);ctrl(0x9000,(prec<<6)|(rm<<4));ctrl(0x8800,0);cmd(0x4800|op);operand(b);
  cmd(0xa800);const uint32_t status=get();cmd(0x6800);
  const uint32_t hi=get(),m1=get(),m2=get();const uint64_t man=(uint64_t(m1)<<32)|m2;
  float_status s{};s.float_detect_tininess=1;s.floatx80_rounding_precision=precisions[prec];s.float_rounding_mode=modes[rm];
  uint64_t q=0;flag sign=0; float_status load=s;load.floatx80_rounding_precision=80;load.float_rounding_mode=float_round_nearest_even;load.float_exception_flags=0; a=floatx80_move(a,&load); floatx80 want{}; switch(op){case 4:want=floatx80_sqrt(b,&s);break;case 30:want=floatx80_getexp(b,&s);break;case 31:want=floatx80_getman(b,&s);break;case 56:want=floatx80_cmp(a,b,&s);break;case 58:want=floatx80_tst(b,&s);break;case 33:want=floatx80_mod(a,b,&q,&sign,&s);break;case 37:want=floatx80_rem(a,b,&q,&sign,&s);break;case 38:want=floatx80_scale(a,b,&s);break;}
  const bool nan=(want.high&0x7fff)==0x7fff && (want.low<<1);
  const uint32_t cc=(want.high&0x8000?0x08000000:0)|(nan?0x01000000:0)|(!nan&&(want.high&0x7fff)==0x7fff?0x02000000:0)|((want.high&0x7fff)<0x7fff&&!want.low?0x04000000:0);
  if(op==56||op==58)want=a;
  ++count;
  if((status&0xff000000)!=cc || ((op==33||op==37)&&((status>>16)&255)!=((q&127)|(sign?128:0))) || hi!=(uint32_t(want.high)<<16)||man!=want.low||(status&0xffff)!=fold(s.float_exception_flags)){
   if(fail<18)std::printf("op=%02x FPCR=%02x dst=%04x:%016llx src=%04x:%016llx got=%08x:%016llx FPSR=%08x want=%04x:%016llx flags=%02x\n",op,(prec<<6)|(rm<<4),a.high,(unsigned long long)a.low,b.high,(unsigned long long)b.low,hi,(unsigned long long)man,status,want.high,(unsigned long long)want.low,s.float_exception_flags);
   ++fail;
  }
 }
 std::printf("Arithmetic CIR vs Hatari: %d cases, %d failures\n",count,fail);return fail?1:0;
}
"""

def run(command, quiet_stderr=False):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL if quiet_stderr else subprocess.PIPE)
    if result.returncode:
        raise RuntimeError("Command failed: " + " ".join(map(str, command))
                           + "\n" + result.stdout + (result.stderr or "")[-1000:])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-mutation", action="store_true",
                        help="exiger aussi la détection d’un FCMP aux zéros signés incorrect")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    hatari = root / "extern/hatari"
    source = hatari / "src/cpu/softfloat/softfloat.c"
    if not source.is_file():
        parser.error("Sources Hatari absentes : lancer tools/setup_hatari.sh")
    cc, cxx = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++")
    if not shutil.which(cc) or not shutil.which(cxx):
        parser.error("Compilateurs C/C++ requis (CC et CXX acceptés)")
    revision = run(["git", "-C", str(hatari), "rev-parse", "HEAD"]).stdout.strip()
    print("Hatari source:", revision)
    with tempfile.TemporaryDirectory(prefix="neost-fpu-arithmetic-") as directory:
        tmp = Path(directory)
        driver, obj, executable = tmp / "probe.cpp", tmp / "softfloat.o", tmp / "probe"
        driver.write_text(PROBE)
        run([cc, "-O2", "-I", str(hatari / "src/cpu"), "-c", str(source), "-o", str(obj)])
        build = [cxx, "-std=c++17", "-O2", "-I", str(root / "src"),
                 "-I", str(root / "src/io"), "-I", str(hatari / "src/cpu"), str(driver)]
        run(build + [str(root / "src/io/Fpu.cpp"), str(obj), "-o", str(executable)])
        print(run([str(executable)], quiet_stderr=True).stdout, end="")
        if args.check_mutation:
            current = (root / "src/io/Fpu.cpp").read_text()
            fixed = "else if (aSig == 0)          { z = 1; neg = aSign; }"
            if fixed not in current:
                raise RuntimeError("Chemin FCMP attendu absent")
            mutant = tmp / "Fpu-mutant.cpp"
            mutant.write_text(current.replace(fixed, "else if (aSig == 0) { z = 1; neg = 0; }"))
            run(build + [str(mutant), str(obj), "-o", str(executable)])
            result = subprocess.run([str(executable)], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            if result.returncode != 1 or "cases," not in result.stdout:
                raise RuntimeError("Mutation non détectée proprement\n" + result.stdout + (result.stderr or "")[-1000:])
            print("Mutation detected:", result.stdout.strip().splitlines()[-1])


if __name__ == "__main__":
    main()
