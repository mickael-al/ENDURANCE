#!/usr/bin/env python3
"""Render documented noise-library atlases from actual OpenGL shader samples.

Requires the existing EGL/OpenGL test driver and Pillow only for this script.
No CPU noise approximation or automatic contrast adjustment is used.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
from pathlib import Path

from check_noise_library import Probe
from gl_headless import GL

ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Panel:
    title: str
    function: str
    arguments: str = ""
    caption: str = ""
    plane: str = ""
    slice_offset: float = 0.0


def panels_for(dimension: int) -> list[Panel]:
    """Select at most sixteen comparable examples, using the public 01 API."""
    suffix = f"{dimension}D01"
    panels = [
        Panel("Blanc par cellule", f"noiseWhite{suffix}", caption="Valeur constante par cellule"),
        Panel("Valeurs quintiques", f"noiseValue{suffix}", caption="Interpolation quintique"),
        Panel("Valeurs cubiques", f"noiseValueCubic{suffix}", caption="Interpolation cubique"),
        Panel("Perlin", f"noisePerlin{suffix}", caption="Bruit à gradients sur grille"),
        Panel("Simplex", f"noiseSimplex{suffix}", caption="Bruit sur simplexes"),
        Panel("Voronoï normalisé", f"noiseVoronoi{suffix}", caption="Distance F1 euclidienne"),
        Panel("Identité des cellules", f"noiseCellValue{suffix}", caption="Valeur du site le plus proche"),
        Panel("Fractale Perlin", f"noiseFbm{suffix}", ",4,2.0,0.5", "4 oct. · lac. 2 · gain 0,5"),
        Panel("Crêtes fractales", f"noiseRidged{suffix}", ",4,2.0,0.5", "4 oct. · lac. 2 · gain 0,5"),
        Panel("Turbulence", f"noiseTurbulence{suffix}", ",4,2.0,0.5", "4 oct. · lac. 2 · gain 0,5"),
        Panel("Perlin déformé", f"noiseWarpedPerlin{suffix}", ",0.8,0.75", "Amplitude 0,8 · fréquence 0,75"),
        Panel("Veines de marbre", f"noiseMarble{suffix}", ",0.65,0.4", "Fréquence 0,65 · distorsion 0,4", "xy" if dimension == 3 else ""),
        Panel("Anneaux de bois", f"noiseWood{suffix}", ",0.75,0.2", "Fréquence 0,75 · distorsion 0,2"),
        Panel("Fissures cellulaires", f"noiseCracks{suffix}", ",0.12", "Largeur en F2 − F1 : 0,12"),
    ]
    if dimension == 2:
        panels += [
            Panel("Dunes orientées", "noiseDunes2D01", caption="Direction (0,8 ; 0,6) · λ = 1,6"),
            Panel("Formations locales", "noiseLandmark2D01", caption="Esp. 1,4 · rayon 0,30 · occ. 0,8"),
        ]
    else:
        panels += [
            Panel("Perlin périodique", "noisePerlinPeriodic3D01", ",ivec3(4)", "Période : 4 cellules par axe"),
            Panel("Perlin, autre hauteur", "noisePerlin3D01", caption="Même seed, plan décalé de +1", slice_offset=1.0),
        ]
    return panels


def expression_for(panel: Panel, dimension: int) -> str:
    """Return a vec4 expression compatible with the shared GPU Probe."""
    p = "p.xy" if dimension == 2 else "p.xyz"
    if panel.function == "noiseDunes2D01":
        return "vec4(noiseDunes2D01(p.xy,vec2(0.8,0.6),1.6,probeSeed))"
    if panel.function == "noiseLandmark2D01":
        return "vec4(noiseLandmark2D01(p.xy,1.4,probeSeed,NOISE_EUCLIDEAN,0.30,0.45,0.8))"
    return f"vec4({panel.function}({p},probeSeed{panel.arguments}))"


def sample_points(size: int, extent: float, dimension: int, plane: str, height: float):
    """Sample pixel centers; the vertical image axis points toward positive world values."""
    for row in range(size):
        vertical = extent * (1.0 - 2.0 * (row + 0.5) / size)
        for column in range(size):
            horizontal = extent * (2.0 * (column + 0.5) / size - 1.0)
            if dimension == 2:
                yield (horizontal, vertical)
            elif plane == "xy":
                yield (horizontal, vertical, height)
            else:
                yield (horizontal, height, vertical)


def french_number(value: float) -> str:
    return f"{value:g}".replace(".", ",")


def font_loader(image_font):
    """Prefer portable TrueType families; Pillow's default is the last fallback."""
    def load(size: int, *, bold: bool = False, mono: bool = False):
        names = ("DejaVuSansMono.ttf", "LiberationMono-Regular.ttf") if mono else (
            ("DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf") if bold else
            ("DejaVuSans.ttf", "LiberationSans-Regular.ttf")
        )
        directories = (Path("/usr/share/fonts/truetype/dejavu"),
                       Path("/usr/share/fonts/truetype/liberation2"))
        for name in names:
            for candidate in (name, *(str(directory / name) for directory in directories)):
                try:
                    return image_font.truetype(candidate, size=size)
                except OSError:
                    pass
        try:
            return image_font.load_default(size=size)
        except TypeError:
            return image_font.load_default()
    return load


def fitted_font(draw, text: str, load_font, width: int, size: int, **options):
    """Keep exact API labels on one line, without clipping or ellipsis."""
    for candidate_size in range(size, 8, -1):
        font = load_font(candidate_size, **options)
        if draw.textbbox((0, 0), text, font=font)[2] <= width:
            return font
    raise ValueError(f"Label does not fit the panel: {text}")


def render_atlas(probe, args, dimension, image_module, image_draw, load_font):
    panels = panels_for(dimension)
    size, margin, gap = args.size, 24, 16
    panel_width, panel_height = size + 32, size + 106
    header_height, footer_height = 150, 76
    width = 2 * margin + 4 * panel_width + 3 * gap
    height = header_height + 4 * panel_height + 3 * gap + footer_height
    canvas = image_module.new("RGB", (width, height), (247, 249, 250))
    draw = image_draw.Draw(canvas)
    ink, secondary = (24, 34, 43), (72, 85, 97)

    title = f"Bibliothèque de bruits · {dimension}D"
    draw.text((margin, 19), title, font=load_font(27, bold=True), fill=ink)
    domain = f"[−{french_number(args.extent)}, {french_number(args.extent)}]"
    subtitle = f"Seed {args.seed} · domaine {domain} sur les deux axes visibles · {size} × {size} échantillons/panneau"
    draw.text((margin, 61), subtitle, font=fitted_font(draw, subtitle, load_font, width-2*margin, 13), fill=secondary)
    if dimension == 3:
        line = "Coupes XZ à Y fixé ; marbre en XY à Z fixé. Le plan exact est indiqué sous chaque panneau."
    else:
        line = "Même domaine pour toutes les fonctions ; les paramètres des compositions sont indiqués sous les images."
    draw.text((margin, 84), line, font=fitted_font(draw, line, load_font, width-2*margin, 12), fill=secondary)

    # One shared linear grayscale: each function keeps its actual output contrast.
    ramp = image_module.frombytes("L", (128, 1), bytes(round(i * 255 / 127) for i in range(128)))
    canvas.paste(ramp.resize((128, 12)), (margin, 113))
    legend = "0 = noir · 1 = blanc · intensité linéaire commune, sans autocontraste"
    draw.text((margin+140, 111), legend,
              font=fitted_font(draw, legend, load_font, width-2*margin-140, 12), fill=secondary)

    for index, panel in enumerate(panels):
        x = margin + (index % 4) * (panel_width + gap)
        y = header_height + (index // 4) * (panel_height + gap)
        draw.rounded_rectangle((x, y, x+panel_width, y+panel_height), radius=6,
                               fill=(255, 255, 255), outline=(220, 226, 231))
        draw.text((x+10, y+10), panel.title,
                  font=fitted_font(draw, panel.title, load_font, panel_width-20, 13, bold=True), fill=ink)
        draw.text((x+10, y+30), panel.function,
                  font=fitted_font(draw, panel.function, load_font, panel_width-20, 11, mono=True), fill=secondary)

        plane = panel.plane or "xz"
        slice_height = args.slice + panel.slice_offset
        points = list(sample_points(size, args.extent, dimension, plane, slice_height))
        expression = expression_for(panel, dimension)
        _, rows = probe.sample(expression, points, args.seed, dimension)
        values = [row[0] for row in rows]
        if not all(math.isfinite(value) and -1e-6 <= value <= 1.000001 for value in values):
            raise ValueError(f"{panel.function}: nonfinite or out-of-range shader samples")
        pixels = bytes(round(min(1.0, max(0.0, value)) * 255.0) for value in values)
        sample_image = image_module.frombytes("L", (size, size), pixels)
        image_x, image_y = x + (panel_width-size)//2, y + 52
        canvas.paste(sample_image, (image_x, image_y))
        draw.rectangle((image_x-1, image_y-1, image_x+size, image_y+size), outline=(208, 217, 223))
        draw.text((x+10, y+size+62), panel.caption,
                  font=fitted_font(draw, panel.caption, load_font, panel_width-20, 10), fill=secondary)
        if dimension == 2:
            coordinate_label = "Axes : x → · y ↑"
        else:
            fixed_axis = "Z" if plane == "xy" else "Y"
            vertical_axis = "y" if plane == "xy" else "z"
            coordinate_label = f"{plane.upper()} · {fixed_axis} = {french_number(slice_height)} · x → / {vertical_axis} ↑"
        draw.text((x+10, y+size+81), coordinate_label,
                  font=fitted_font(draw, coordinate_label, load_font, panel_width-20, 10), fill=secondary)
        print(f"{dimension}D {index+1:02d}/{len(panels)} {panel.function}: [{min(values):.4f}, {max(values):.4f}]", flush=True)

    footer_y = height-footer_height+17
    note = "Valeurs des fonctions, sans éclairage ni rendu d'isosurface. Les variantes 01 gardent leur amplitude réelle."
    draw.text((margin, footer_y), note,
              font=fitted_font(draw, note, load_font, width-2*margin, 11), fill=secondary)
    renderer = "Shaders GLSL exécutés via OpenGL · " + probe.gl.renderer
    draw.text((margin, footer_y+23), renderer,
              font=fitted_font(draw, renderer, load_font, width-2*margin, 10), fill=secondary)
    output = args.output_dir / f"noise-library-{dimension}d.png"
    canvas.save(output, optimize=True)
    print(f"Wrote {output} ({output.stat().st_size:,} bytes)", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/images")
    parser.add_argument("--size", type=int, choices=(128, 160), default=160)
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=1337)
    parser.add_argument("--extent", type=float, default=4.0, help="Positive half-width of the visible coordinate domain.")
    parser.add_argument("--slice", type=float, default=0.375, help="Fixed coordinate of the 3D slice.")
    parser.add_argument("--dimension", choices=("2", "3", "all"), default="all")
    args = parser.parse_args()
    if not 0 <= args.seed <= 0xffffffff:
        parser.error("--seed must fit an unsigned 32-bit integer")
    if not math.isfinite(args.extent) or not 0 < args.extent <= 4096:
        parser.error("--extent must be finite and in (0,4096]")
    if not math.isfinite(args.slice) or abs(args.slice) > 4096:
        parser.error("--slice must be finite with abs(value)<=4096")
    # Pillow is optional for the test suite; only the atlas command imports it.
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as error:
        raise SystemExit("Atlas rendering needs Pillow: python3 -m pip install Pillow") from error
    args.output_dir.mkdir(parents=True, exist_ok=True)
    dimensions = (2, 3) if args.dimension == "all" else (int(args.dimension),)
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}", flush=True)
        probe = Probe(gl)
        for dimension in dimensions:
            render_atlas(probe, args, dimension, Image, ImageDraw, font_loader(ImageFont))


if __name__ == "__main__":
    main()
