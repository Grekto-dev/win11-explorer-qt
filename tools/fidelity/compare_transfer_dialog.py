#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare ExplorerTransferDialog captures with the measured Windows 11 copy-dialog frames.

Inputs
  --ours  directory with the PNGs written by the Qt test
          `transferDialogCapturesMeasuredStatesWhenArtifactsRequested`
          (EXPLORER_TEST_ARTIFACTS=<dir> -> <dir>/transfer-dialog/*.png, DPR 1)
  --ref   directory with the VM reference frames (kept OUTSIDE Git; never copied here)

Output (outside Git): <out-root>/<UTC>/comparison.json + side-by-side / diff PNGs.

Scope: non-text regions only. The title strip (y < 32, covered by the VM banner and with
no client-side equivalent) and all text regions are excluded. No pass/fail threshold is
chosen: every region is only classified as
  igual     (<= 2 px and dRGB <= 4)
  proxima   (within 2 px but 4 < dRGB <= 8)
  diferente (> 2 px or dRGB > 8)
  ausente   (not measurable on one side; the reason is given)
Stdlib + Pillow only.
"""
import argparse
import hashlib
import json
import math
import os
import sys
from datetime import datetime, timezone

from PIL import Image

WINDOW_ORIGIN = (496, 152)          # visible DWM window top-left on the VM screen
TITLE_STRIP = 32                    # excluded rows
SEP_COLOR = (77, 77, 77)
BG = (44, 44, 44)

# state -> (reference frame, window height, our capture)
STATES = {
    "running-79": ("A_1794614114758.png", 165, "running-79.png"),
    "paused-32": ("B_1794614304648.png", 165, "paused-32.png"),
    "paused-32-expanded": ("B_1794614319274.png", 287, "paused-32-expanded.png"),
    "cancelling-53": ("C_post_cmd1_1794614487559.png", 165, "cancelling-53.png"),
}
# Extra reference only used to cross-check the 53 % fill width (paused, before cancelling).
EXTRA_REF = {"paused-53-before-cancel": ("C_1794614481415.png", 165),
             # same state as B_1794614319274 (paused 32 %, expanded) but with the mouse halo away from the
             # graph; used ONLY for graph/border geometry (the key frame has a halo over its left side)
             "paused-32-expanded-clean-graph": ("B_pre_cmd2_1794614329586.png", 287)}
# Frames whose bar right side / ✕ lies under the VM cursor halo (blue glow): those readings are not trusted.
HALO_ON_RIGHT = {"paused-32", "cancelling-53"}

# Text rectangles (x0, y0, x1, y1) masked out of the diff images / pixel statistics.
TEXT_RECTS_COMMON = [(26, 40, 424, 63), (26, 62, 350, 85), (48, 0, 130, 0)]  # footer text added per state
LABEL_PX = {"igual": 0, "proxima": 1, "diferente": 2, "ausente": 3}


# ------------------------------------------------------------------ helpers
def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def crop_reference(path, height):
    im = Image.open(path).convert("RGB")
    x, y = WINDOW_ORIGIN
    return im.crop((x, y, x + 449, y + height))


def drgb(a, b):
    return max(abs(a[i] - b[i]) for i in range(3))


def lab(c):
    def f(t):
        return t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    lin = []
    for v in c:
        v /= 255.0
        lin.append(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4)
    x = (0.4124 * lin[0] + 0.3576 * lin[1] + 0.1805 * lin[2]) / 0.95047
    y = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
    z = (0.0193 * lin[0] + 0.1192 * lin[1] + 0.9505 * lin[2]) / 1.08883
    fx, fy, fz = f(x), f(y), f(z)
    return 116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)


def delta_e76(a, b):
    la, lb = lab(a), lab(b)
    return math.sqrt(sum((la[i] - lb[i]) ** 2 for i in range(3)))


def median_rect(im, x0, y0, x1, y1):
    px = [im.getpixel((x, y)) for y in range(y0, y1) for x in range(x0, x1)]
    return tuple(sorted(p[c] for p in px)[len(px) // 2] for c in range(3))


def lum(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def sat(c):
    return max(c) - min(c)


def near(a, b, tol):
    return drgb(a, b) <= tol


def classify(px, dc):
    """px: positional difference (px) or None; dc: max-channel RGB difference or None."""
    if px is None and dc is None:
        return "ausente"
    pxv = 0 if px is None else px
    dcv = 0 if dc is None else dc
    if pxv > 2 or dcv > 8:
        return "diferente"
    if dcv > 4:
        return "proxima"
    return "igual"


def region(ref, ours, px=None, dc=None, note="", de=None):
    out = {"ref": ref, "ours": ours, "delta": {}, "note": note}
    if px is not None:
        out["delta"]["px"] = round(px, 2)
    if dc is not None:
        out["delta"]["dRGB"] = dc
    if de is not None:
        out["delta"]["dE76"] = round(de, 2)
    out["class"] = classify(px, dc)
    return out


def missing(ref, ours, note):
    return {"ref": ref, "ours": ours, "delta": {}, "class": "ausente", "note": note}


def color_region(ref_c, our_c, note=""):
    if ref_c is None or our_c is None:
        return missing(ref_c, our_c, note or "cor não medida")
    return region(list(ref_c), list(our_c), dc=drgb(ref_c, our_c), de=delta_e76(ref_c, our_c), note=note)


def pos_region(ref_v, our_v, note=""):
    if ref_v is None or our_v is None:
        return missing(ref_v, our_v, note or "posição não medida")
    if isinstance(ref_v, (tuple, list)):
        d = math.hypot(ref_v[0] - our_v[0], ref_v[1] - our_v[1])
        rv, ov = [round(v, 2) for v in ref_v], [round(v, 2) for v in our_v]
    else:
        d = abs(ref_v - our_v)
        rv, ov = ref_v, our_v
    return region(rv, ov, px=d, note=note)


# ------------------------------------------------------------------ measurements
def bar_geometry(im, pct):
    """Outer extents, border/track/fill colours and fill extent of the progress bar."""
    y_mid = 97
    xs = [x for x in range(0, 449) if drgb(im.getpixel((x, y_mid)), BG) > 6]
    # window border columns (x 0, 448) are not bar pixels: restrict to the content area
    xs = [x for x in xs if 8 <= x <= 440]
    if not xs:
        return None
    left, right = min(xs), max(xs)
    fill_col = im.getpixel((left + 10, y_mid))
    fill_right = None
    if sat(fill_col) > 60:                      # contiguous run of the fill colour from the left edge
        fill_right = left + 1
        while fill_right + 1 < right and near(im.getpixel((fill_right + 1, y_mid)), fill_col, 14):
            fill_right += 1
    if fill_right is not None and fill_right + 8 < 330:
        track_x = fill_right + (330 - fill_right) // 2
    else:
        track_x = right - 6
    track_col = im.getpixel((track_x, y_mid))
    col_x = left + 6
    ys = [y for y in range(78, 116) if drgb(im.getpixel((col_x, y)), BG) > 6]
    top, bottom = (min(ys), max(ys)) if ys else (None, None)
    border_col = im.getpixel((track_x, top)) if top is not None else None
    sat_ys = [y for y in range(78, 116) if near(im.getpixel((col_x, y)), fill_col, 14)]
    fill_top, fill_bottom = (min(sat_ys), max(sat_ys)) if sat_ys else (None, None)
    return {"left": left, "right": right, "top": top, "bottom": bottom, "fill_right": fill_right,
            "fill_color": fill_col, "track_color": track_col, "border_color": border_col,
            "fill_top": fill_top, "fill_bottom": fill_bottom, "track_x": track_x}


def separator(im, h):
    for y in range(100, h - 8):
        if all(near(im.getpixel((x, y)), SEP_COLOR, 6) for x in (10, 224, 430)):
            xs = [x for x in range(0, 449) if near(im.getpixel((x, y)), SEP_COLOR, 6)]
            return {"y": y, "color": im.getpixel((224, y)), "x0": min(xs), "x1": max(xs)}
    return None


def window_border(im, h):
    """Border colour (left column, body rows) and bottom thickness/colour."""
    left_col = median_rect(im, 0, 40, 1, h - 8)
    thick_left = 0
    for x in range(0, 6):
        if drgb(im.getpixel((x, 60)), BG) > 8:      # >8: ignore the faint VM banner glow (44,46,49)
            thick_left += 1
        else:
            break
    bottom_rows = []
    for y in range(h - 1, h - 8, -1):
        c = im.getpixel((300, y))
        if drgb(c, BG) > 8:
            bottom_rows.append((y, c))
        else:
            break
    return {"left_color": left_col, "left_thickness": thick_left,
            "bottom_rows": bottom_rows, "bottom_thickness": len(bottom_rows),
            "bottom_color": bottom_rows[0][1] if bottom_rows else None}


def corner_inset(im, h):
    n = 0
    for x in range(0, 12):
        if lum(im.getpixel((x, h - 1))) < 35:
            n += 1
        else:
            break
    return n


def glyph(im, box, rel=0.3):
    """Weighted centroid + bbox of low-saturation bright pixels inside box.

    A pixel belongs to the glyph when its brightness above the background is >= `rel` of the box's
    peak (so a dimmed glyph is thresholded like a bright one) and at least 15 levels."""
    x0, y0, x1, y1 = box
    px = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = im.getpixel((x, y))
            if sat(c) < 40:
                px[(x, y)] = (lum(c) - lum(BG), c)
    peak = max((w for w, _ in px.values()), default=0)
    if peak < 30:
        return None
    thr = max(15.0, rel * peak)
    tot = sx = sy = ink = 0.0
    xs, ys = [], []
    members = []
    for (x, y), (wgt, c) in px.items():
        ink += max(0.0, wgt) / (255 - lum(BG))
        if wgt >= thr:
            tot += wgt
            sx += wgt * x
            sy += wgt * y
            xs.append(x)
            ys.append(y)
            members.append(c)
    return {"ink": ink, "cx": sx / tot + 0.5, "cy": sy / tot + 0.5, "bbox": [min(xs), min(ys), max(xs), max(ys)],
            "w": max(xs) - min(xs) + 1, "h": max(ys) - min(ys) + 1, "color": max(members, key=lum)}


PAUSE_BOX = (350, 58, 382, 88)
CANCEL_BOX = (397, 58, 429, 88)


def graph_geometry(im):
    row = 130
    xs = [x for x in range(8, 441) if drgb(im.getpixel((x, row)), BG) > 3]
    left, right = (min(xs), max(xs)) if xs else (None, None)
    col = 300
    ys = [y for y in range(78, 178) if drgb(im.getpixel((col, y)), BG) > 3]
    top, bottom = (min(ys), max(ys)) if ys else (None, None)
    if left is None or top is None:
        return None
    border = im.getpixel((left, row))
    # tinted progress area: scan the row just above the first grid row, avoiding the grid columns
    y_tint = top + 5
    tint_probe = im.getpixel((left + 18, y_tint))
    tint_xs = [x for x in range(left + 1, right) if near(im.getpixel((x, y_tint)), tint_probe, 5)]
    tint_right = max(tint_xs) if tint_xs else None
    if drgb(tint_probe, BG) <= 3:
        tint_probe, tint_right = None, None
    # vertical grid columns: scan a row below the grid cell top, outside the tint (right of progress)
    y_grid = top + 5
    gx = []
    out_start = (tint_right + 2) if tint_right else left + 2
    for x in range(out_start, right):
        if drgb(im.getpixel((x, y_grid)), BG) > 6:
            gx.append(x)
    # horizontal grid rows: column right of the tinted area
    gy = [y for y in range(top + 1, bottom) if drgb(im.getpixel((right - 12, y)), BG) > 6]
    # curve colour near the bottom-left (inside tint) and above-curve tint colour
    curve = im.getpixel((left + 12, bottom - 3))
    return {"left": left, "right": right, "top": top, "bottom": bottom, "border_color": border,
            "tint_color": tint_probe, "tint_right": tint_right, "curve_color": curve,
            "vgrid_x_outside_tint": gx, "hgrid_y": gy}


def footer_chevron(im, sep_y):
    return glyph(im, (28, sep_y + 4, 46, sep_y + 40))


def pct_edge(info, pct):
    return None if info is None else (info["left"] + 1) + (info["right"] - info["left"] - 1) * pct / 100.0


# ------------------------------------------------------------------ comparison per state
def compare_state(name, ref, ours, h, pct, extra):
    r = {}
    # 1. background (left margin of the body, away from text and from the cursor halo) and footer
    r["fundo_corpo"] = color_region(median_rect(ref, 5, 40, 22, 110), median_rect(ours, 5, 40, 22, 110),
                                    "mediana x5..22,y40..110 (margem esquerda, sem texto)")
    sep_r, sep_o = separator(ref, h), separator(ours, h)
    r["rodape_fundo"] = color_region(
        median_rect(ref, 5, (sep_r or {"y": h - 40})["y"] + 6, 22, h - 6),
        median_rect(ours, 5, (sep_o or {"y": h - 40})["y"] + 6, 22, h - 6),
        "margem esquerda do rodapé")
    # 2. window border
    wr, wo = window_border(ref, h), window_border(ours, h)
    r["borda_janela_cor_esquerda"] = color_region(wr["left_color"], wo["left_color"], "coluna x=0")
    r["borda_janela_espessura_esquerda"] = pos_region(wr["left_thickness"], wo["left_thickness"],
                                                      "colunas não-fundo a partir de x=0 em y=60")
    r["borda_janela_base_cor"] = color_region(wr["bottom_color"], wo["bottom_color"], "linha inferior em x=300")
    r["cantos_inferiores_arredondados_px"] = pos_region(corner_inset(ref, h), corner_inset(ours, h),
                                                        "colunas 'fora da janela' (lum<35) na última linha, a partir de x=0; arredondamento é do DWM (não do widget)")
    r["borda_janela_base_espessura"] = pos_region(wr["bottom_thickness"], wo["bottom_thickness"],
                                                   "linhas não-fundo a partir de y=H-1, x=300; ref=%s ours=%s" %
                                                   (wr["bottom_rows"], wo["bottom_rows"]))
    # 3. separator
    if sep_r and sep_o:
        r["separador_y"] = pos_region(sep_r["y"], sep_o["y"], "linha do separador (esperado 120 recolhido / 242 expandido)")
        r["separador_cor"] = color_region(sep_r["color"], sep_o["color"])
        r["separador_extensao_x"] = pos_region(max(abs(sep_r["x0"] - sep_o["x0"]), abs(sep_r["x1"] - sep_o["x1"])),
                                               0, "ref x%d..%d, ours x%d..%d" % (sep_r["x0"], sep_r["x1"], sep_o["x0"], sep_o["x1"]))
    else:
        r["separador"] = missing(sep_r, sep_o, "separador não encontrado em um dos lados")
    # 4. progress bar (collapsed states only; expanded replaces it by the graph)
    if name != "paused-32-expanded":
        br, bo = bar_geometry(ref, pct), bar_geometry(ours, pct)
        if br and bo:
            r["barra_borda_esquerda_x"] = pos_region(br["left"], bo["left"])
            if name in HALO_ON_RIGHT:
                r["barra_borda_direita_x"] = missing(br["right"], bo["right"], "halo azul do cursor sobre o lado direito do quadro de referência (leitura não confiável); medido em running-79")
            else:
                r["barra_borda_direita_x"] = pos_region(br["right"], bo["right"])
            r["barra_topo_y"] = pos_region(br["top"], bo["top"])
            r["barra_base_y"] = pos_region(br["bottom"], bo["bottom"])
            r["barra_cor_borda"] = color_region(br["border_color"], bo["border_color"])
            r["barra_cor_trilho"] = color_region(br["track_color"], bo["track_color"],
                                                 "amostra em x=%d / %d (fora do halo do cursor)" % (br["track_x"], bo["track_x"]))
            r["barra_cor_preenchimento"] = color_region(br["fill_color"], bo["fill_color"])
            r["barra_preenchimento_fim_x"] = pos_region(br["fill_right"], bo["fill_right"],
                                                        "último x saturado em y=97; esperado ≈ %.1f para %d%%" %
                                                        (pct_edge(br, pct) or -1, pct))
            if br["fill_right"] is not None and bo["fill_right"] is not None:
                wi_r, wi_o = br["right"] - br["left"] - 1, bo["right"] - bo["left"] - 1
                fr = (br["fill_right"] - br["left"]) / wi_r * 100
                fo = (bo["fill_right"] - bo["left"]) / wi_o * 100
                r["barra_preenchimento_percentual_medido"] = region(round(fr, 2), round(fo, 2), px=abs(br["fill_right"] - bo["fill_right"]),
                                                                    note="%% medido (interior=%d/%d px) frente ao %d%% nominal" % (wi_r, wi_o, pct))
            r["barra_preenchimento_topo_y"] = pos_region(br["fill_top"], bo["fill_top"])
            r["barra_preenchimento_base_y"] = pos_region(br["fill_bottom"], bo["fill_bottom"])
        else:
            r["barra"] = missing(br, bo, "barra não encontrada")
        if extra:
            be = bar_geometry(extra, pct)
            if be and bo:
                r["barra_preenchimento_fim_x_vs_quadro_pausado_antes_cancelar"] = pos_region(
                    be["fill_right"], bo["fill_right"], "quadro C_1794614481415 (pausado 53%) × captura cancelando 53%")
                r["barra_cor_preenchimento_vs_quadro_pausado_antes_cancelar"] = color_region(be["fill_color"], bo["fill_color"])
    # 5. glyph centres
    def glyph_region(label, box, ref_im, our_im, note):
        gr, go = glyph(ref_im, box), glyph(our_im, box)
        if not gr or not go:
            r[label] = missing(gr, go, "glifo não encontrado")
            return
        r[label + "_centro"] = pos_region((gr["cx"], gr["cy"]), (go["cx"], go["cy"]), note)
        r[label + "_caixa_tamanho"] = pos_region(max(abs(gr["w"] - go["w"]), abs(gr["h"] - go["h"])), 0,
                                                 "ref %dx%d bbox %s; ours %dx%d bbox %s" % (gr["w"], gr["h"], gr["bbox"], go["w"], go["h"], go["bbox"]))
        r[label + "_cor"] = color_region(gr["color"], go["color"],
                                         "pixel mais claro do glifo (pico de antialiasing em traços finos; tinta ref=%.1f px² ours=%.1f px²)" % (gr["ink"], go["ink"]))
    if name == "running-79":
        glyph_region("glifo_pausa", PAUSE_BOX, ref, ours, "quadro limpo (sem halo do cursor)")
        glyph_region("glifo_cancelar", CANCEL_BOX, ref, ours, "quadro limpo")
    elif name == "paused-32":
        r["glifo_retomar"] = missing(None, None, "halo+cursor do mouse cobre o ▶ neste quadro; medido no quadro expandido")
        glyph_region("glifo_cancelar", CANCEL_BOX, ref, ours, "quadro com halo à esquerda; ✕ limpo")
    elif name == "paused-32-expanded":
        glyph_region("glifo_retomar", PAUSE_BOX, ref, ours, "quadro limpo")
        glyph_region("glifo_cancelar", CANCEL_BOX, ref, ours, "quadro limpo")
    elif name == "cancelling-53":
        glyph_region("glifo_retomar_esmaecido", PAUSE_BOX, ref, ours, "▶ desabilitado (121,121,121)")
        r["glifo_cancelar"] = missing(None, None, "halo+cursor do mouse cobre o ✕ neste quadro")
    # 6. graph (expanded)
    if name == "paused-32-expanded":
        gr, go = graph_geometry(extra), graph_geometry(ours)
        if gr and go:
            r["grafico_anel_interno_cor"] = color_region(extra.getpixel((gr["left"] + 1, gr["top"] + 30)),
                                                         ours.getpixel((go["left"] + 1, go["top"] + 30)),
                                                         "pixel logo após a borda esquerda do gráfico (ref: anel #383838; x=%d y=%d)" % (gr["left"] + 1, gr["top"] + 30))
            r["grafico_anel_interno_direita_cor"] = color_region(extra.getpixel((gr["right"] - 1, gr["top"] + 30)),
                                                                 ours.getpixel((go["right"] - 1, go["top"] + 30)),
                                                                 "pixel logo antes da borda direita do gráfico")
            r["grafico_anel_interno_topo_cor"] = color_region(extra.getpixel((gr["left"] + 200, gr["top"] + 1)),
                                                              ours.getpixel((go["left"] + 200, go["top"] + 1)),
                                                              "pixel logo abaixo da borda superior (fora da área tingida)")
            r["grafico_anel_interno_base_cor"] = color_region(extra.getpixel((gr["left"] + 200, gr["bottom"] - 1)),
                                                              ours.getpixel((go["left"] + 200, go["bottom"] - 1)),
                                                              "pixel logo acima da borda inferior (fora da área tingida)")
            r["grafico_borda_esquerda_x"] = pos_region(gr["left"], go["left"])
            r["grafico_borda_direita_x"] = pos_region(gr["right"], go["right"])
            r["grafico_topo_y"] = pos_region(gr["top"], go["top"])
            r["grafico_base_y"] = pos_region(gr["bottom"], go["bottom"])
            r["grafico_cor_borda"] = color_region(gr["border_color"], go["border_color"])
            r["grafico_area_tingida_cor"] = color_region(gr["tint_color"], go["tint_color"], "yellow 15% sobre o fundo")
            r["grafico_area_tingida_fim_x"] = pos_region(gr["tint_right"], go["tint_right"],
                                                         "fim da área tingida (progresso 32%%); amostra y=%d" % (gr["top"] + 5))
            r["grafico_cor_curva"] = color_region(gr["curve_color"], go["curve_color"], "pixel no canto inferior esquerdo")
            def nearest_match(ref_lines, our_lines, label, note):
                if not ref_lines or not our_lines:
                    r[label] = missing(ref_lines, our_lines, "linhas de grade não encontradas")
                    return
                # each of OUR lines against the nearest reference line (the reference may draw extra ring lines)
                worst = max(min(abs(a - b) for b in ref_lines) for a in our_lines)
                r[label] = pos_region(worst, 0, "ref %s ours %s %s" % (ref_lines, our_lines, note))
            # inner ring rows/cols (x=left+1,right-1 / y=top+1,bottom-1) are not grid lines
            gx_r = [x for x in gr["vgrid_x_outside_tint"] if gr["left"] + 1 < x < gr["right"] - 1]
            gx_o = [x for x in go["vgrid_x_outside_tint"] if go["left"] + 1 < x < go["right"] - 1]
            nearest_match(gx_r, gx_o, "grafico_grade_vertical_x", "(fora da área tingida; anel interno excluído)")
            hy_r = [y for y in gr["hgrid_y"] if gr["top"] + 1 < y < gr["bottom"] - 1]
            hy_o = [y for y in go["hgrid_y"] if go["top"] + 1 < y < go["bottom"] - 1]
            nearest_match(hy_r, hy_o, "grafico_grade_horizontal_y", "(coluna x=%d)" % (gr["right"] - 12))
        else:
            r["grafico"] = missing(gr, go, "gráfico não encontrado")
    # 7. footer chevron
    if sep_r and sep_o:
        cr, co = footer_chevron(ref, sep_r["y"]), footer_chevron(ours, sep_o["y"])
        if cr and co:
            r["rodape_chevron_centro_relativo_ao_separador"] = pos_region(
                (cr["cx"], cr["cy"] - sep_r["y"]), (co["cx"], co["cy"] - sep_o["y"]),
                "ref bbox %s ours bbox %s" % (cr["bbox"], co["bbox"]))
            r["rodape_chevron_tamanho"] = pos_region(max(abs(cr["w"] - co["w"]), abs(cr["h"] - co["h"])), 0,
                                                     "ref %dx%d ours %dx%d" % (cr["w"], cr["h"], co["w"], co["h"]))
        else:
            r["rodape_chevron"] = missing(cr, co, "chevron não encontrado")
    return r


# ------------------------------------------------------------------ images
def text_rects(h, sep_y):
    rects = [(0, 0, 449, 40), (26, 40, 424, 63), (26, 62, 350, 85)]   # rows 0..39: title strip + VM banner glow
    if h > 200:
        rects.append((26, 178, 424, 232))
    rects.append((48, (sep_y or 120) + 6, 135, h - 4))
    return rects


def side_by_side(name, ref, ours, h, sep_y, out_dir):
    mask = text_rects(h, sep_y)
    diff = Image.new("RGB", (449, h), (0, 0, 0))
    n_cmp = n_diff = 0
    for y in range(h):
        for x in range(449):
            if any(a <= x < c and b <= y < d for a, b, c, d in mask):
                diff.putpixel((x, y), (60, 0, 60))
                continue
            a, b = ref.getpixel((x, y)), ours.getpixel((x, y))
            d = drgb(a, b)
            n_cmp += 1
            if d > 8:
                n_diff += 1
            diff.putpixel((x, y), (min(255, d * 6),) * 3)
    sheet = Image.new("RGB", (449 * 3 + 20, h), (20, 20, 20))
    sheet.paste(ref, (0, 0))
    sheet.paste(ours, (459, 0))
    sheet.paste(diff, (918, 0))
    path = os.path.join(out_dir, "side_by_side_%s.png" % name)
    sheet.save(path)
    diff.save(os.path.join(out_dir, "diff_%s.png" % name))
    return {"side_by_side": os.path.basename(path), "diff": "diff_%s.png" % name,
            "compared_pixels_non_text_non_title": n_cmp,
            "pixels_dRGB_gt_8": n_diff, "fraction_dRGB_gt_8": round(n_diff / n_cmp, 4)}


# ------------------------------------------------------------------ main
def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ours", required=True, help="directory containing the Qt capture PNGs (…/transfer-dialog)")
    ap.add_argument("--ref", default="/home/pedro/win11-explorer-qt/.state/fidelity/reference/phase2/controls-20261113T235224Z/frames")
    ap.add_argument("--out-root", default=os.path.join(repo, ".state", "fidelity", "transfer-dialog-compare"))
    args = ap.parse_args(argv)

    utc = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_dir = os.path.join(args.out_root, utc)
    os.makedirs(out_dir, exist_ok=True)
    result = {"utc": utc, "window_origin_in_reference_frames": WINDOW_ORIGIN, "title_strip_excluded_px": TITLE_STRIP,
              "classes": {"igual": "<=2 px and dRGB<=4", "proxima": "<=2 px and 4<dRGB<=8",
                          "diferente": ">2 px or dRGB>8", "ausente": "not measurable on one side"},
              "inputs": {}, "states": {}, "regions": {}, "images": {}}
    for state, (frame, h, cap) in STATES.items():
        ref_path, our_path = os.path.join(args.ref, frame), os.path.join(args.ours, cap)
        for p in (ref_path, our_path):
            if not os.path.isfile(p):
                sys.exit("missing input: %s" % p)
        result["inputs"][state] = {"reference_frame": ref_path, "reference_sha256": sha256(ref_path),
                                   "capture": our_path, "capture_sha256": sha256(our_path)}
        ref = crop_reference(ref_path, h)
        ours = Image.open(our_path).convert("RGB")
        if ours.size != (449, h):
            result["states"][state] = {"error": "capture size %s != (449, %d)" % (ours.size, h)}
            continue
        extra = None
        extra_key = {"cancelling-53": "paused-53-before-cancel",
                     "paused-32-expanded": "paused-32-expanded-clean-graph"}.get(state)
        if extra_key:
            ef, eh = EXTRA_REF[extra_key]
            ep = os.path.join(args.ref, ef)
            result["inputs"][extra_key] = {"reference_frame": ep, "reference_sha256": sha256(ep)}
            extra = crop_reference(ep, eh)
        pct = int(state.split("-")[-1]) if state != "paused-32-expanded" else 32
        regs = compare_state(state, ref, ours, h, pct, extra)
        result["regions"][state] = regs
        sep = separator(ref, h)
        result["images"][state] = side_by_side(state, ref, ours, h, (sep or {}).get("y"), out_dir)
        result["states"][state] = {"height": h, "nominal_percent": pct,
                                   "counts": {c: sum(1 for v in regs.values() if v["class"] == c) for c in LABEL_PX}}
    script_path = os.path.abspath(__file__)
    result["script"] = {"path": script_path, "sha256": sha256(script_path)}
    with open(os.path.join(out_dir, "comparison.json"), "w") as f:
        json.dump(result, f, indent=1, ensure_ascii=False, default=list)
    print(out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
