#!/usr/bin/env python3
"""Vérifie les shaders slang de NeoST sans RetroArch (packaging/libretro/shaders).

Pour chaque .slang : résout les #include, découpe les étapes comme RetroArch
(« #pragma stage vertex|fragment »), retire les #pragma propres au slang, et
compile chaque étape en SPIR-V Vulkan par glslangValidator. Puis contrôle que
chaque réglage du bloc UBO est déclaré par un « #pragma parameter », et que les
préréglages .slangp désignent des fichiers qui existent.

  python3 tools/check_slang.py        # glslangValidator requis (brew/apt glslang)

(c) 2026 VERHILLE Arnaud — projet NeoST.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "packaging" / "libretro" / "shaders"
BUILTIN = {"MVP", "OutputSize", "SourceSize", "OriginalSize", "FinalViewportSize", "FrameCount",
           "FrameDirection", "Rotation"}


def expand(path, seen=()):
    out = []
    for line in path.read_text().splitlines():
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        if m:
            inc = path.parent / m.group(1)
            if inc in seen:
                raise SystemExit(f"{path}: include loop on {inc}")
            out += expand(inc, seen + (inc,))
        else:
            out.append(line)
    return out


def stages(lines):
    common, parts, cur = [], {"vertex": [], "fragment": []}, None
    for line in lines:
        m = re.match(r"\s*#pragma\s+stage\s+(\w+)", line)
        if m:
            cur = m.group(1)
            continue
        if re.match(r"\s*#pragma\s+(parameter|name|format)\b", line):
            continue
        (parts[cur] if cur else common).append(line)
    return {k: "\n".join(common + v) + "\n" for k, v in parts.items()}


def main():
    glslang = shutil.which("glslangValidator")
    if not glslang:
        print("glslangValidator not found (brew install glslang / apt install glslang-tools)")
        return 2
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for shader in sorted(ROOT.rglob("*.slang")):
            lines = expand(shader)
            text = "\n".join(lines)
            params = set(re.findall(r"#pragma\s+parameter\s+(\w+)", text))
            ubo = re.search(r"uniform\s+UBO\s*\{(.*?)\}", text, re.S)
            fields = set(re.findall(r"\b(?:float|uint|int|vec4|mat4)\s+(\w+)\s*;", ubo.group(1))) if ubo else set()
            missing = {f for f in fields - params - BUILTIN if not f.endswith("Size")}
            for stage, src in stages(lines).items():
                f = Path(tmp) / f"{shader.stem}.{stage[:4]}"
                f.write_text(src)
                r = subprocess.run([glslang, "-V", "--target-env", "vulkan1.0", "-S",
                                    "vert" if stage == "vertex" else "frag", str(f), "-o", str(f) + ".spv"],
                                   capture_output=True, text=True)
                if r.returncode:
                    failures += 1
                    print(f"FAIL {shader.relative_to(ROOT)} [{stage}]\n{r.stdout}{r.stderr}")
            if missing:
                failures += 1
                print(f"FAIL {shader.relative_to(ROOT)}: no #pragma parameter for {sorted(missing)}")
            else:
                print(f"OK   {shader.relative_to(ROOT)} ({len(params)} parameters)")
    for preset in sorted(ROOT.rglob("*.slangp")):
        text = preset.read_text()
        refs = re.findall(r'^\s*shader\d+\s*=\s*"?([^"\n]+)"?', text, re.M)
        refs += re.findall(r'^#reference\s+"([^"]+)"', text, re.M)
        bad = [r for r in refs if not (preset.parent / r.strip()).exists()]
        names = re.findall(r'^parameters\s*=\s*"([^"]*)"', text, re.M)
        declared = set()
        for s in ROOT.rglob("*.slang"):
            declared |= set(re.findall(r"#pragma\s+parameter\s+(\w+)", "\n".join(expand(s))))
        unknown = [n for n in (names[0].split(";") if names else []) if n and n not in declared]
        if bad or unknown:
            failures += 1
            print(f"FAIL {preset.relative_to(ROOT)}: missing {bad} unknown parameters {unknown}")
        else:
            print(f"OK   {preset.relative_to(ROOT)} ({len(refs)} files)")
    print("\nSLANG OK" if not failures else f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
