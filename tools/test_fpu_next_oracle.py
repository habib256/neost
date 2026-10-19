#!/usr/bin/env python3
"""Compare FINT/FINTRZ par CIR au véritable SoftFloat Hatari local.

Usage : python3 tools/test_fpu_next_oracle.py [--check-mutation]
Aucun build partagé ni accès réseau. Le contrôle de mutation rétablit dans une
copie temporaire le chemin FINTRZ sans propagation des exceptions ; il doit être détecté.
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
 const int precisions[]={80,32,64,64};
 const int modes[]={float_round_nearest_even,float_round_to_zero,float_round_down,float_round_up};
 int count=0,fail=0;
 for(int op:{1,3})for(int prec=0;prec<4;++prec)for(int rm=0;rm<4;++rm)for(auto a:cases){
  f.reset();ctrl(0x9000,(prec<<6)|(rm<<4));ctrl(0x8800,0);cmd(0x4800|op);operand(a);
  cmd(0xa800);const uint32_t status=get();cmd(0x6800);
  const uint32_t hi=get(),m1=get(),m2=get();const uint64_t man=(uint64_t(m1)<<32)|m2;
  float_status s{};s.float_detect_tininess=1;s.floatx80_rounding_precision=precisions[prec];s.float_rounding_mode=modes[rm];
  floatx80 want=op==1?floatx80_round_to_int(a,&s):floatx80_round_to_int_toward_zero(a,&s);
  ++count;
  if(hi!=(uint32_t(want.high)<<16)||man!=want.low||(status&0xffff)!=fold(s.float_exception_flags)){
   if(fail<18)std::printf("op=%02x FPCR=%02x input=%04x:%016llx got=%08x:%016llx FPSR=%08x want=%04x:%016llx flags=%02x\n",op,(prec<<6)|(rm<<4),a.high,(unsigned long long)a.low,hi,(unsigned long long)man,status,want.high,(unsigned long long)want.low,s.float_exception_flags);
   ++fail;
  }
 }
 // Livraison CIR des exceptions INEX2 et SNAN pour les deux opérations.
 for(int op:{1,3})for(int signaling:{0,1}){
  f.reset();ctrl(0x9000,signaling?0x4040:0x240);cmd(0x4800|op);
  operand(signaling?floatx80{0x7fff,0x800000000badf00d}:floatx80{0x3fff,0x800000c000000000});
  const uint16_t response=(uint16_t(f.read8(0xfffa40))<<8)|f.read8(0xfffa41);
  const uint16_t expected=signaling?0x7036:0x7031;
  ++count;if(response!=expected){++fail;std::printf("op=%02x signaling=%d response=%04x expected%04x\n",op,signaling,response,expected);}
 }
 std::printf("Integer CIR vs Hatari: %d cases, %d failures\n",count,fail);return fail?1:0;
}
"""

def run(command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Command failed: " + " ".join(map(str, command))
                           + "\n" + result.stdout + result.stderr[-1000:])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-mutation", action="store_true",
                        help="exiger aussi la détection du chemin entier antérieur")
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
    with tempfile.TemporaryDirectory(prefix="neost-fpu-integer-") as directory:
        tmp = Path(directory)
        driver, obj, executable = tmp / "probe.cpp", tmp / "softfloat.o", tmp / "probe"
        driver.write_text(PROBE)
        run([cc, "-O2", "-I", str(hatari / "src/cpu"), "-c", str(source), "-o", str(obj)])
        build = [cxx, "-std=c++17", "-O2", "-I", str(root / "src"),
                 "-I", str(root / "src/io"), "-I", str(hatari / "src/cpu"), str(driver)]
        run(build + [str(root / "src/io/Fpu.cpp"), str(obj), "-o", str(executable)])
        print(run([str(executable)]).stdout, end="")
        if args.check_mutation:
            current = (root / "src/io/Fpu.cpp").read_text()
            fixed = "rf = sf::roundToInt(s, z); st.exceptionFlags |= z.exceptionFlags;"
            if fixed not in current:
                raise RuntimeError("Correctif FINTRZ absent")
            mutant = tmp / "Fpu-mutant.cpp"
            mutant.write_text(current.replace(fixed, "rf = sf::roundToInt(s, z);"))
            run(build + [str(mutant), str(obj), "-o", str(executable)])
            result = subprocess.run([str(executable)], text=True, capture_output=True)
            if result.returncode != 1 or "cases," not in result.stdout:
                raise RuntimeError("Mutation non détectée proprement\n" + result.stdout + result.stderr)
            print("Mutation detected:", result.stdout.strip().splitlines()[-1])


if __name__ == "__main__":
    main()
