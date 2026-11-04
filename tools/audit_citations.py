#!/usr/bin/env python3
# =============================================================================
#  audit_citations.py — A43 : les citations d'Hatari du code, confrontées à l'épingle.
#
#  POURQUOI. CLAUDE.md : une citation de grade D nomme sa révision, et se vérifie avant d'être écrite.
#  Une citation est confirmée si un symbole du commentaire existe à moins de 12 lignes de la ligne citée
#  dans extern/hatari @ f0736b24. Le reste se relit à la main : l'outil n'y pose jamais de révision.
#
#  Usage : audit_citations.py list [fichiers]    bilan et citations à relire
#          audit_citations.py apply [fichiers]   ajoute « [D] … @ f0736b24 » aux citations confirmées
#  Sans fichier : src/core et src/io. Code 77 sans extern/hatari.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
import re,sys,os
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
H=str(ROOT/'extern'/'hatari'/'src')
if not os.path.isdir(H):
    print('SKIP — extern/hatari absent'); sys.exit(77)
idx={}
for root,_,fs in os.walk(H):
    for f in fs:
        if f.endswith(('.c','.h')): idx.setdefault(f,os.path.join(root,f))
cache={}
def src(f):
    if f not in cache: cache[f]=open(idx[f],errors='replace').read().splitlines()
    return cache[f]
sym=re.compile(r'\b([A-Za-z]+_[A-Za-z0-9_]+|[a-z]+[A-Z][A-Za-z0-9]+|[A-Z][a-z]+[A-Z][A-Za-z0-9]+)\b')
pat=re.compile(r'(?<![\w/])([A-Za-z0-9_-]+\.(?:c|h)):(\d+)((?:[-–,/]\d+)*)')
apply_=len(sys.argv)>1 and sys.argv[1]=='apply'
done=rest=0; out=[]
import subprocess
files=[a for a in sys.argv[2:]] or subprocess.run(['git','grep','-l','-E',r'[a-zA-Z0-9_]+\.(c|h):[0-9]+','--','src/core/*','src/io/*'],cwd=ROOT,capture_output=True,text=True).stdout.split()
for path in files:
    path=str(ROOT/path) if not os.path.isabs(path) else path
    lines=open(path).read().split('\n')
    changed=False
    for n,l in enumerate(lines):
        new=l; shift=0
        for m in pat.finditer(l):
            if '@' in l[m.end():m.end()+3] or '[D]' in l[max(0,m.start()-4):m.start()]: continue
            f,a=m[1],int(m[2])
            if f not in idx: continue
            S=src(f)
            ctx=' '.join(lines[max(0,n-1):n+2])
            base=os.path.splitext(f)[0]
            ids=[x for x in sym.findall(ctx) if not x.startswith(('NEOST','kS','kV','glue')) and len(x)>4 and x!=base]
            win=' '.join(S[max(0,a-12):a+12]) if a<=len(S) else ''
            if any(x in win for x in ids):
                done+=1
                if apply_:
                    t=m[0]+' @ f0736b24'
                    new=new[:m.start()+shift]+'[D] '+t+new[m.end()+shift:]
                    shift+=len('[D] ')+len(' @ f0736b24')
            else:
                rest+=1
                out.append(f"{path.split('/')[-1]}:{n+1} {f}:{a} «{l.strip()[:74]}» => {S[a-1].strip()[:44] if a<=len(S) else 'HORS'}")
        if new!=l: lines[n]=new; changed=True
    if apply_ and changed: open(path,'w').write('\n'.join(lines))
print('confirmées',done,'à relire',rest)
print('\n'.join(out))
