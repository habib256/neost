# =============================================================================
#  make_floppy.py — Génère une image disquette FAT12 720 Ko de test
#  (disks/diskA.st) avec des fichiers + un dossier, montable dans NeoST.
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import struct, os

SECT=512; SPT=9; SIDES=2; TRACKS=80
TOTAL=TRACKS*SIDES*SPT          # 1440
img=bytearray(TOTAL*SECT)

# --- Boot sector / BPB (offsets identiques DOS/Atari) ---
img[0x00:0x02]=b'\x60\x1c'      # BRA.S (non bootable, peu importe)
img[0x02:0x08]=b'NeoST '        # OEM
struct.pack_into('<H',img,0x0b,SECT)   # octets/secteur
img[0x0d]=2                      # secteurs/cluster
struct.pack_into('<H',img,0x0e,1)      # secteurs reserves (boot)
img[0x10]=2                      # nb de FAT
struct.pack_into('<H',img,0x11,112)    # entrees racine
struct.pack_into('<H',img,0x13,TOTAL)  # total secteurs
img[0x15]=0xF9                   # media descriptor
struct.pack_into('<H',img,0x16,3)      # secteurs/FAT
struct.pack_into('<H',img,0x18,SPT)    # secteurs/piste
struct.pack_into('<H',img,0x1a,SIDES)  # faces
struct.pack_into('<H',img,0x1c,0)      # secteurs caches

RES=1; NFAT=2; SPF=3; NDIRS=112
fat1=RES*SECT
root=(RES+NFAT*SPF)*SECT
rootsects=(NDIRS*32+SECT-1)//SECT
data=(RES+NFAT*SPF+rootsects)*SECT     # debut zone data (cluster 2)

def set_fat(idx,val):
    for base in (fat1, fat1+SPF*SECT):
        off=base+idx*3//2
        if idx & 1:
            img[off]=(img[off]&0x0F)|((val<<4)&0xF0)
            img[off+1]=(val>>4)&0xFF
        else:
            img[off]=val&0xFF
            img[off+1]=(img[off+1]&0xF0)|((val>>8)&0x0F)

set_fat(0,0xFF9); set_fat(1,0xFFF)

DATE=(46<<9)|(5<<5)|31; TIME=(12<<11)
def cluster_off(c): return data+(c-2)*2*SECT

def add_file(slot,name,ext,cluster,content):
    e=root+slot*32
    img[e:e+8]=name.ljust(8).encode()[:8]
    img[e+8:e+11]=ext.ljust(3).encode()[:3]
    img[e+11]=0x20                       # archive
    struct.pack_into('<H',img,e+22,TIME)
    struct.pack_into('<H',img,e+24,DATE)
    struct.pack_into('<H',img,e+26,cluster)
    struct.pack_into('<I',img,e+28,len(content))
    o=cluster_off(cluster); img[o:o+len(content)]=content
    set_fat(cluster,0xFFF)

def add_dir(slot,name,cluster):
    e=root+slot*32
    img[e:e+8]=name.ljust(8).encode()[:8]
    img[e+8:e+11]=b'   '
    img[e+11]=0x10                       # directory
    struct.pack_into('<H',img,e+22,TIME)
    struct.pack_into('<H',img,e+24,DATE)
    struct.pack_into('<H',img,e+26,cluster)
    set_fat(cluster,0xFFF)
    # entrees . et ..
    o=cluster_off(cluster)
    img[o:o+11]=b'.          '; img[o+11]=0x10; struct.pack_into('<H',img,o+26,cluster)
    img[o+32:o+43]=b'..         '; img[o+32+11]=0x10; struct.pack_into('<H',img,o+32+26,0)

add_file(0,'LISEZMOI','TXT',2,b'Bienvenue sur NeoST !\r\nEmulateur Atari ST.\r\n(c) 2026 VERHILLE Arnaud\r\n')
add_file(1,'NEOST','TXT',3,b'Disquette FAT12 generee pour tester le FDC WD1772.\r\n')
add_dir(2,'PROGS',4)

_out = os.path.join(os.path.dirname(__file__), '..', 'disks', 'diskA.st')
os.makedirs(os.path.dirname(_out), exist_ok=True)
with open(_out,'wb') as f:
    f.write(img)
print("disk.st :", len(img), "octets,", TOTAL, "secteurs, SPT", SPT, "faces", SIDES)
