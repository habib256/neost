#!/usr/bin/env python3
# Génère l'icône de NeoST à partir du logo de la page web (web/shell.html, `.fuji`,
# variante « C3 » choisie par l'auteur le 2026-11-13) : cinq barres inclinées, rouge,
# orange, jaune, vert, bleu, la jaune sur toute la hauteur — un clin d'œil au Fuji sans
# en reprendre la silhouette (marque d'Atari). Posées sur une dalle sombre façon écran
# CRT : léger dégradé, halo coloré, scanlines discrètes.
#
# Sorties :
#   packaging/neost.png                              512×512 (.app via sips, AppImage)
#   packaging/android/app/src/main/res/mipmap-*/ic_launcher.png   48…192 px
#
# Régénérer : python3 packaging/make_icon.py  (nécessite Pillow). Aucune police : le
# résultat ne dépend pas de la machine.
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
SS = 4                      # suréchantillonnage (anticrénelage des barres)
SIZE = 512                  # neost.png : 512 = plafond de linuxdeploy et du 256@2x macOS
W = SIZE * SS
RADIUS = int(W * 0.18)      # coins de la dalle

# Les cinq barres du SVG de la page (viewBox 0 0 64 42, stroke-width 6, bouts ronds).
BARS = [
    ((0xe8, 0x44, 0x3a), (12.1, 22), (9.5, 37)),
    ((0xf5, 0xa6, 0x23), (22.4, 14), (19.5, 31)),
    ((0xf8, 0xe7, 0x1c), (36.0, 4), (30.0, 39)),
    ((0x57, 0xd9, 0xa3), (45.4, 14), (42.5, 31)),
    ((0x4a, 0x90, 0xd9), (55.1, 22), (52.5, 37)),
]
STROKE = 6.0
# Boîte englobante du dessin (traits compris) dans le repère du SVG.
BX0, BY0, BX1, BY1 = 6.5, 1.0, 58.1, 42.0


def tile_mask():
    m = Image.new("L", (W, W), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, W - 1, W - 1], radius=RADIUS, fill=255)
    return m


def background():
    # Dégradé vertical sombre (bleu nuit → presque noir), comme le bezel de la page.
    top, bot = (40, 40, 56), (14, 14, 22)
    col = Image.new("RGB", (1, W))
    for y in range(W):
        t = y / (W - 1)
        col.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bot)))
    return col.resize((W, W)).convert("RGBA")


def bars_layer(scale, ox, oy):
    lay = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    r = STROKE * scale / 2
    for rgb, (x0, y0), (x1, y1) in BARS:
        p0 = (ox + x0 * scale, oy + y0 * scale)
        p1 = (ox + x1 * scale, oy + y1 * scale)
        d.line([p0, p1], fill=rgb + (255,), width=int(round(2 * r)))
        for cx, cy in (p0, p1):
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb + (255,))
    return lay


def main():
    mask = tile_mask()
    img = background()

    # Le dessin occupe ~74 % de la largeur, centré optiquement.
    scale = W * 0.74 / (BX1 - BX0)
    ox = (W - (BX1 - BX0) * scale) / 2 - BX0 * scale
    oy = (W - (BY1 - BY0) * scale) / 2 - BY0 * scale
    bars = bars_layer(scale, ox, oy)

    # Halo : les barres floutées, éclaircies, sous les barres nettes (phosphore).
    glow = bars.filter(ImageFilter.GaussianBlur(W * 0.035))
    glow.putalpha(glow.getchannel("A").point(lambda a: int(a * 0.55)))
    img = Image.alpha_composite(img, glow)
    # Ombre portée légère, décalée vers le bas.
    shadow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    shadow.putalpha(bars.getchannel("A").point(lambda a: int(a * 0.5)))
    shadow = ImageChops.offset(shadow, 0, int(W * 0.012)).filter(ImageFilter.GaussianBlur(W * 0.01))
    img = Image.alpha_composite(img, shadow)
    img = Image.alpha_composite(img, bars)

    # Scanlines : une ligne sombre toutes les 4 lignes de la taille finale.
    scan = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    sd = ImageDraw.Draw(scan)
    step = 4 * SS
    for y in range(0, W, step):
        sd.rectangle([0, y, W, y + SS - 1], fill=(0, 0, 0, 38))
    img = Image.alpha_composite(img, scan)

    # Reflet de verre en haut de la dalle, très discret.
    sheen = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(sheen).ellipse([-W * 0.3, -W * 0.75, W * 1.3, W * 0.42], fill=(255, 255, 255, 18))
    img = Image.alpha_composite(img, sheen.filter(ImageFilter.GaussianBlur(W * 0.02)))

    # Liseré intérieur clair pour détacher la dalle sur un fond sombre.
    rim = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle([SS, SS, W - 1 - SS, W - 1 - SS], radius=RADIUS - SS,
                                          outline=(255, 255, 255, 34), width=3 * SS)
    img = Image.alpha_composite(img, rim)

    out = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    big = out.resize((SIZE, SIZE), Image.LANCZOS)
    big.save(HERE / "neost.png", optimize=True)
    print("écrit :", HERE / "neost.png")

    res = HERE / "android/app/src/main/res"
    for dpi, px in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        p = res / f"mipmap-{dpi}/ic_launcher.png"
        if p.parent.is_dir():
            out.resize((px, px), Image.LANCZOS).save(p, optimize=True)
            print("écrit :", p)


if __name__ == "__main__":
    main()
