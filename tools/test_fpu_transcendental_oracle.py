#!/usr/bin/env python3
"""Compare les transcendantes 68881 par CIR au véritable FPSP SoftFloat d'Hatari.

Usage : python3 tools/test_fpu_transcendental_oracle.py [--check-mutation] [--random N]

Pour chacun des 19 opmodes (FSINH … FSINCOS), NeoST piloté par ses registres CIR
est confronté à `softfloat_fpsp.c` compilé tel quel (avec `softfloat.c`) : les
80 bits du résultat, l'octet EXC + AEXC du FPSR et les codes conditions, dans les
4 modes d'arrondi × 4 motifs de précision FPCR (dont le motif réservé 11).
Entrées : zéros, dénormaux, infinis, NaN/SNaN, extrêmes, valeurs de domaine
interdit (logs < 0, |x| > 1 pour asin/acos/atanh), les SEUILS DE BRANCHE extraits
automatiquement du source Hatari (± 1 ulp) et des valeurs pseudo-aléatoires
déterministes. FSINCOS vérifie aussi FPc, et le cas FPc == FPn.
Aucun build partagé ni accès réseau. Le contrôle de mutation rétablit dans une
copie temporaire le calcul FSIN en `double` hôte ; il doit être détecté.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

PROBE = r"""
#include <cstdint>
#include <cstdio>
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
static floatx80 readReg(int r){cmd(0x6800|(r<<7));const uint32_t hi=get(),m1=get(),m2=get();
 floatx80 x;x.high=uint16_t(hi>>16);x.low=(uint64_t(m1)<<32)|m2;return x;}
static uint32_t fold(uint8_t flags){
 uint32_t r=0;
 if(flags&float_flag_signaling)r|=0x4080;
 if(flags&float_flag_invalid)r|=0x2080;
 if(flags&float_flag_divbyzero)r|=0x0410;
 if(flags&float_flag_overflow)r|=0x1048;
 if(flags&float_flag_underflow)r|=0x800;
 if(flags&float_flag_inexact)r|=0x208;
 if((flags&float_flag_underflow)&&(flags&float_flag_inexact))r|=0x20;
 return r;
}
// Codes conditions attendus (fpsr_set_result_always + fpsr_set_result, avec les
// prédicats floatx80_is_nan/is_zero/is_infinity d'Hatari : un significande nul
// sous un exposant non nul — « zéro dénormalisé » rendu par l'arrondi simple/double
// en sous-dépassement — compte comme zéro).
static uint32_t cc(floatx80 r){
 const bool nan=(r.high&0x7fff)==0x7fff&&(r.low<<1);
 uint32_t c=(r.high&0x8000)?0x08000000u:0;
 if(nan)c|=0x01000000u;
 else if((r.high&0x7fff)<0x7fff&&r.low==0)c|=0x04000000u;
 else if((r.high&0x7fff)==0x7fff)c|=0x02000000u;
 return c;
}
typedef floatx80 (*Fn)(floatx80,float_status*);
struct Op{int code;const char*name;Fn fn;};
static const Op ops[]={
 {0x02,"FSINH",floatx80_sinh},{0x06,"FLOGNP1",floatx80_lognp1},{0x08,"FETOXM1",floatx80_etoxm1},
 {0x09,"FTANH",floatx80_tanh},{0x0A,"FATAN",floatx80_atan},{0x0C,"FASIN",floatx80_asin},
 {0x0D,"FATANH",floatx80_atanh},{0x0E,"FSIN",floatx80_sin},{0x0F,"FTAN",floatx80_tan},
 {0x10,"FETOX",floatx80_etox},{0x11,"FTWOTOX",floatx80_twotox},{0x12,"FTENTOX",floatx80_tentox},
 {0x14,"FLOGN",floatx80_logn},{0x15,"FLOG10",floatx80_log10},{0x16,"FLOG2",floatx80_log2},
 {0x19,"FCOSH",floatx80_cosh},{0x1C,"FACOS",floatx80_acos},{0x1D,"FCOS",floatx80_cos},
 {0x30,"FSINCOS",nullptr},
};
static uint64_t rng=0x9E3779B97F4A7C15ull;
static uint64_t next(){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static floatx80 X(uint16_t h,uint64_t l){floatx80 x;x.high=h;x.low=l;return x;}
// Seuils de branche « compact » / exposant extraits de softfloat_fpsp.c.
static const uint32_t compacts[]={@COMPACTS@};
static const uint16_t exps[]={@EXPS@};
int main(){
 std::vector<floatx80> in={
  X(0,0),X(0x8000,0),X(0,1),X(0x8000,1),X(0,0x4000000000000000ull),X(0x8000,0x7fffffffffffffffull),
  X(0,0x8000000000000000ull),X(0x8000,0x8000000000000000ull),
  X(0x7fff,0),X(0xffff,0),X(0x7fff,0x8000000000000000ull),X(0xffff,0x8000000000000000ull),
  X(0x7fff,0xc00000000badf00dull),X(0xffff,0xc00000000badf00dull),
  X(0x7fff,0x800000000badf00dull),X(0xffff,0x800000000badf00dull),X(0x7fff,0xffffffffffffffffull),
  X(0x7ffe,0xffffffffffffffffull),X(0xfffe,0xffffffffffffffffull),X(0x7ffe,0x8000000000000000ull),
  X(0x3fff,0x8000000000000000ull),X(0xbfff,0x8000000000000000ull),
  X(0x3fff,0x8000000000000001ull),X(0xbfff,0x8000000000000001ull),
  X(0x3ffe,0xffffffffffffffffull),X(0xbffe,0xffffffffffffffffull),
  X(0x3ffe,0x8000000000000000ull),X(0xbffe,0x8000000000000000ull),
  X(0x4000,0x8000000000000000ull),X(0xc000,0x8000000000000000ull),
  X(0x4000,0xa000000000000000ull),X(0x4002,0xa000000000000000ull),X(0xc002,0xa000000000000000ull),
  // Multiples de π/2, π, 15π et au-delà (réduction d'argument et boucle REDUCEX).
  X(0x3fff,0xc90fdaa22168c235ull),X(0x4000,0xc90fdaa22168c235ull),X(0xc000,0xc90fdaa22168c234ull),
  X(0x4000,0x96cbe3f9990e91a8ull),X(0x4004,0xbc7edcf7ff523611ull),X(0x4004,0xbc7f000000000000ull),
  X(0x401d,0xc90fdaa22168c235ull),X(0x403e,0xc90fdaa22168c235ull),X(0x4100,0xc90fdaa22168c235ull),
  X(0x7ffe,0xc90fdaa22168c235ull),X(0x7ffe,0xffff000000000000ull),X(0xfffe,0xffff123456789abcull),
  // Logarithmes autour de 1 et de -1, exponentielles aux bornes de dépassement.
  X(0x3ffe,0xf07d000000000000ull),X(0x3fff,0x8841000000000000ull),X(0xbffe,0xffffffffffffffffull),
  X(0x400c,0xb17217f7d1cf79abull),X(0xc00c,0xb17217f7d1cf79abull),X(0x400c,0xb2b3000000000000ull),
  X(0xc00c,0xb2b4000000000000ull),X(0x400c,0xb27c000000000000ull),X(0x400d,0x80c0000000000000ull),
  X(0xc00d,0x80c1000000000000ull),X(0x400b,0x9b07000000000000ull),X(0xc00b,0x9b08000000000000ull),
  X(0x3f99,0x8000000000000000ull),X(0xbf99,0x8000000000000000ull),X(0x3f99,0x8000000000000001ull),
  X(0x0033,0x8000000000000000ull),X(0x8032,0x8000000000000000ull),X(0x3fbe,0x8000000000000000ull),
 };
 for(uint32_t c:compacts)for(int sgn=0;sgn<2;++sgn){
  const uint16_t h=uint16_t((c>>16)|(sgn<<15));const uint64_t l=uint64_t(c&0xffff)<<48;
  for(uint64_t m:{l,l-1,l+1,l|0xffffffffffffull})if(m&0x8000000000000000ull)in.push_back(X(h,m));
 }
 for(uint16_t e:exps)for(int sgn=0;sgn<2;++sgn)for(int de=-1;de<=1;++de){
  const uint16_t h=uint16_t(((e+de)&0x7fff)|(sgn<<15));
  in.push_back(X(h,0x8000000000000000ull));in.push_back(X(h,0xffffffffffffffffull));
  in.push_back(X(h,0x8000000000000001ull));
 }
 const size_t special=in.size();
 for(int i=0;i<RANDOM;++i){
  uint64_t r=next(),m=next()|0x8000000000000000ull;uint16_t s=uint16_t(r&0x8000);
  int e;
  switch((r>>16)%8){
   case 0: e=int((r>>24)%0x7fff); break;                          // plage complète
   case 1: e=0x3fbe + int((r>>24)%0x60); break;                     // [2^-65, 2^31)
   case 2: e=0x3ff8 + int((r>>24)%0x16); break;                     // [2^-7, 2^15)
   case 3: e=0x3ffe + int((r>>24)%2); break;                        // [0.5, 2)
   case 4: e=0x400a + int((r>>24)%4); break;                        // bornes exp/tentox/twotox
   case 5: e=0x3f80 + int((r>>24)%0x80); break;                     // petits arguments
   case 6: e=0; m>>=1+(r>>24)%63; break;                          // dénormaux
   default: e=0x4004 + int((r>>24)%0x40); break;                    // réduction trigonométrique
  }
  in.push_back(X(uint16_t(s|e),m));
 }
 const int precisions[]={80,32,64,64};
 const int modes[]={float_round_nearest_even,float_round_to_zero,float_round_down,float_round_up};
 long count=0,fail=0;
 for(const Op& op:ops){
  long opCount=0,opFail=0;
  for(size_t k=0;k<in.size();++k){
   const floatx80 a=in[k];
   // Chaque entrée traverse les 16 couples (précision FPCR, mode d'arrondi).
   for(int pm=0;pm<16;++pm){
    const int prec=pm>>2,rm=pm&3;
    for(int same=0;same<(op.fn?1:2);++same){
     const int c=same?0:2;                                         // FSINCOS : FPc
     f.reset();ctrl(0x9000,(prec<<6)|(rm<<4));ctrl(0x8800,0);
     cmd(uint16_t(0x4800|(op.fn?op.code:(0x30|c))));operand(a);
     cmd(0xa800);const uint32_t status=get();
     const floatx80 got=readReg(0),gotC=op.fn?got:readReg(c);
     float_status s{};s.float_detect_tininess=1;s.floatx80_rounding_precision=precisions[prec];s.float_rounding_mode=modes[rm];
     floatx80 wantC=X(0,0);
     floatx80 want=op.fn?op.fn(a,&s):floatx80_sincos(a,&wantC,&s);
     if(!op.fn&&same)wantC=want;                                   // FPc == FPn : le sinus l'emporte
     ++opCount;
     const bool bad=got.high!=want.high||got.low!=want.low||(status&0xffff)!=fold(s.float_exception_flags)
      ||(status&0x0f000000)!=cc(want)||(!op.fn&&(gotC.high!=wantC.high||gotC.low!=wantC.low));
     if(bad){
      if(fail<24)std::printf("%s c=%d FPCR=%02x input=%04x:%016llx got=%04x:%016llx/%04x:%016llx FPSR=%08x want=%04x:%016llx/%04x:%016llx flags=%02x\n",
       op.name,c,(prec<<6)|(rm<<4),a.high,(unsigned long long)a.low,got.high,(unsigned long long)got.low,
       gotC.high,(unsigned long long)gotC.low,status,want.high,(unsigned long long)want.low,
       wantC.high,(unsigned long long)wantC.low,s.float_exception_flags);
      ++opFail;++fail;
     }
    }
   }
  }
  count+=opCount;
  std::printf("  %-8s %7ld comparisons, %ld failures\n",op.name,opCount,opFail);
 }
 // Livraison CIR : INEX2 (FSIN 1.0), DZ (FLOGN 0), OPERR (FASIN 2), SNAN (FCOS SNaN).
 struct Cir{int op;uint32_t fpcr;floatx80 a;uint16_t expected;};
 const Cir cirs[]={{0x0E,0x0200,X(0x3fff,0x8000000000000000ull),0x7031},
                   {0x14,0x0400,X(0,0),0x7032},
                   {0x0C,0x2000,X(0x4000,0x8000000000000000ull),0x7034},
                   {0x1D,0x4000,X(0x7fff,0x800000000badf00dull),0x7036}};
 for(const Cir& t:cirs){
  f.reset();ctrl(0x9000,t.fpcr);cmd(uint16_t(0x4800|t.op));operand(t.a);
  const uint16_t response=(uint16_t(f.read8(0xfffa40))<<8)|f.read8(0xfffa41);
  ++count;if(response!=t.expected){++fail;std::printf("op=%02x response=%04x expected %04x\n",t.op,response,t.expected);}
 }
 std::printf("Transcendental CIR vs Hatari FPSP: %zu inputs per op (%zu special), %ld comparisons, %ld failures\n",
  in.size(),special,count,fail);
 return fail?1:0;
}
"""

ORIGINAL_SIN = "case 0x0E: rf = sf::fpsp::floatx80_sin(s, &st);"
HOST_SIN = "case 0x0E: rf = toF(dToExt(std::sin(extToD(src))));"


def run(command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("Command failed: " + " ".join(map(str, command))
                           + "\n" + result.stdout + result.stderr[-2000:])
    return result


def thresholds(fpsp_source):
    """Seuils de branche du FPSP : constantes « compact » et exposants comparés."""
    text = fpsp_source.read_text()
    compacts = sorted({int(v, 16) for v in re.findall(r"compact\s*[<>]=?\s*(0x[0-9A-Fa-f]{8})", text)}
                      | {int(v, 16) for v in re.findall(r"compact\s*==\s*(0x[0-9A-Fa-f]{8})", text)})
    exps = sorted({int(v, 16) for v in re.findall(r"aExp\s*[<>]=?\s*(0x[0-9A-Fa-f]{1,4})\b", text)})
    if len(compacts) < 20 or not exps:
        raise RuntimeError("Seuils FPSP introuvables dans " + str(fpsp_source))
    return compacts, exps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-mutation", action="store_true",
                        help="exiger aussi la détection d'un FSIN calculé en double hôte")
    parser.add_argument("--random", type=int, default=3000,
                        help="entrées pseudo-aléatoires par opération (défaut 3000)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    hatari = root / "extern/hatari"
    if not (hatari / "src/cpu/softfloat/softfloat.c").is_file():
        # Un worktree n'a pas extern/hatari (gitignoré) : se rabattre sur le dépôt principal.
        common = run(["git", "-C", str(root), "rev-parse", "--path-format=absolute",
                      "--git-common-dir"]).stdout.strip()
        alt = Path(common).parent / "extern/hatari"
        if (alt / "src/cpu/softfloat/softfloat.c").is_file():
            hatari = alt
    softfloat = hatari / "src/cpu/softfloat"
    if not (softfloat / "softfloat_fpsp.c").is_file():
        parser.error("Sources Hatari absentes : lancer tools/setup_hatari.sh")
    cc, cxx = os.environ.get("CC", "cc"), os.environ.get("CXX", "c++")
    if not shutil.which(cc) or not shutil.which(cxx):
        parser.error("Compilateurs C/C++ requis (CC et CXX acceptés)")
    revision = run(["git", "-C", str(hatari), "rev-parse", "HEAD"]).stdout.strip()
    print("Hatari source:", revision)
    compacts, exps = thresholds(softfloat / "softfloat_fpsp.c")
    probe = (PROBE.replace("@COMPACTS@", ",".join(f"0x{v:08X}u" for v in compacts))
                  .replace("@EXPS@", ",".join(f"0x{v:04X}" for v in exps)))
    with tempfile.TemporaryDirectory(prefix="neost-fpu-transcendental-") as directory:
        tmp = Path(directory)
        driver, executable = tmp / "probe.cpp", tmp / "probe"
        driver.write_text(probe)
        objects = []
        for name in ("softfloat.c", "softfloat_fpsp.c"):
            obj = tmp / (name + ".o")
            run([cc, "-O2", "-I", str(hatari / "src/cpu"), "-c", str(softfloat / name), "-o", str(obj)])
            objects.append(str(obj))
        build = [cxx, "-std=c++17", "-O2", f"-DRANDOM={args.random}", "-I", str(root / "src"),
                 "-I", str(root / "src/io"), "-I", str(hatari / "src/cpu"), str(driver)]
        run(build + [str(root / "src/io/Fpu.cpp")] + objects + ["-o", str(executable)])
        print(run([str(executable)]).stdout, end="")
        if args.check_mutation:
            current = (root / "src/io/Fpu.cpp").read_text()
            if current.count(ORIGINAL_SIN) != 1:
                raise RuntimeError("Câblage FSIN FPSP introuvable dans Fpu.cpp")
            mutant = tmp / "Fpu-mutant.cpp"
            mutant.write_text(current.replace(ORIGINAL_SIN, HOST_SIN))
            run(build + [str(mutant)] + objects + ["-o", str(executable)])
            result = subprocess.run([str(executable)], text=True, capture_output=True)
            lines = result.stdout.strip().splitlines()
            fsin = [l for l in lines if l.startswith("  FSIN ")]
            # Seul FSIN doit tomber : les autres opérations restent à zéro écart.
            others_clean = all(l.rstrip().endswith(" 0 failures") for l in lines
                               if l.startswith("  F") and not l.strip().startswith("FSIN "))
            if (result.returncode != 1 or not fsin or fsin[0].rstrip().endswith(" 0 failures")
                    or not others_clean):
                raise RuntimeError("Mutation non détectée proprement\n" + result.stdout + result.stderr)
            print("Mutation detected:", fsin[0].strip())


if __name__ == "__main__":
    main()
