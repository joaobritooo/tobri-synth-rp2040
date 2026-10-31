# Gera o PDF com o esquema de ligação do Tobri Synth no Raspberry Pi Pico (RP2040).
import os, sys
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, CondPageBreak, KeepTogether, NextPageTemplate)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase.pdfmetrics import registerFontFamily

HERE = os.path.dirname(os.path.abspath(__file__))
FIG = os.path.join(HERE, 'fig_w')
OUT = sys.argv[1]
os.makedirs(FIG, exist_ok=True)

F = 'C:/Windows/Fonts/'
pdfmetrics.registerFont(TTFont('UI', F + 'segoeui.ttf'))
pdfmetrics.registerFont(TTFont('UIB', F + 'segoeuib.ttf'))
pdfmetrics.registerFont(TTFont('UII', F + 'segoeuii.ttf'))
pdfmetrics.registerFont(TTFont('Mono', F + 'consola.ttf'))
registerFontFamily('UI', normal='UI', bold='UIB', italic='UII', boldItalic='UIB')


def font(size, bold=False, mono=False):
    return ImageFont.truetype(F + ('consolab.ttf' if mono and bold else 'consola.ttf' if mono else
                                   'segoeuib.ttf' if bold else 'segoeui.ttf'), size)


INK = colors.HexColor('#1d2433')
SUB = colors.HexColor('#5b6475')
ACC = colors.HexColor('#e0562b')
PALE = colors.HexColor('#f3f4f7')
LINE = colors.HexColor('#d9dce3')

# grupos de sinais (cor do fio sugerida)
G = {
    'tft':   ((37, 99, 235), 'Display (SPI1)'),
    'touch': ((147, 51, 234), 'Touch XPT2046'),
    'spi':   ((14, 116, 144), 'SPI1 compartilhado'),
    'dac':   ((22, 163, 74), 'DAC I2S'),
    'enc':   ((234, 88, 12), 'Encoder'),
    'pwr':   ((220, 38, 38), 'Alimentação'),
    'gnd':   ((30, 30, 30), 'Terra (GND)'),
}

# pinagem física do Pico: número -> nome
LEFT = ['GP0', 'GP1', 'GND', 'GP2', 'GP3', 'GP4', 'GP5', 'GND', 'GP6', 'GP7', 'GP8', 'GP9', 'GND', 'GP10', 'GP11',
        'GP12', 'GP13', 'GND', 'GP14', 'GP15']
RIGHT = ['VBUS', 'VSYS', 'GND', '3V3_EN', '3V3(OUT)', 'ADC_VREF', 'GP28', 'GND', 'GP27', 'GP26', 'RUN', 'GP22', 'GND',
         'GP21', 'GP20', 'GP19', 'GP18', 'GND', 'GP17', 'GP16']          # pinos 40 .. 21 (de cima para baixo)


def phys(name):
    if name in LEFT:
        return LEFT.index(name) + 1
    return 40 - RIGHT.index(name)


# uso de cada pino no projeto: nome -> (grupo, função)
USE = {
    'GP2': ('enc', 'ENC CLK (A)'), 'GP3': ('enc', 'ENC DT (B)'), 'GP4': ('enc', 'ENC SW (botão)'),
    'GP6': ('dac', 'I2S BCLK'), 'GP7': ('dac', 'I2S LRCLK / WSEL'), 'GP9': ('dac', 'I2S DIN'),
    'GP10': ('spi', 'SCK (tela + touch)'), 'GP11': ('spi', 'MOSI (tela + touch)'), 'GP12': ('touch', 'MISO (T_DO)'),
    'GP13': ('tft', 'TFT CS'), 'GP14': ('tft', 'TFT DC'), 'GP15': ('tft', 'TFT RESET'),
    'GP17': ('touch', 'T_CS'), 'GP21': ('touch', 'T_IRQ'),
    'VBUS': ('pwr', '5 V do USB > DAC VIN'), '3V3(OUT)': ('pwr', '3,3 V > tela, touch, encoder'),
}
FREE = ['GP0', 'GP1', 'GP5', 'GP8', 'GP16', 'GP18', 'GP19', 'GP20', 'GP22', 'GP26', 'GP27', 'GP28']


def pico_pinout(path, highlight=None, scale=1.0, with_labels=True):
    """Pico visto de cima (USB para cima) com as funções de cada pino ao lado."""
    s = scale
    P = int(44 * s)                       # passo entre pinos
    BW, BH = int(300 * s), int(P * 20 + 140 * s)
    LW = int(400 * s) if with_labels else int(40 * s)
    W, H = BW + 2 * LW, BH + int(40 * s)
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    bx, by = LW, int(20 * s)
    d.rounded_rectangle([bx, by, bx + BW, by + BH], int(18 * s), fill=(22, 101, 52), outline=(14, 70, 36), width=3)
    # USB
    d.rounded_rectangle([bx + BW / 2 - 50 * s, by - 14 * s, bx + BW / 2 + 50 * s, by + 46 * s], 8, fill=(190, 194, 200),
                        outline=(120, 124, 130), width=2)
    d.text((bx + BW / 2, by + 62 * s), 'USB', fill=(220, 235, 225), font=font(int(20 * s), True), anchor='mt')
    # chip
    cx, cy = bx + BW / 2, by + BH / 2 + 10 * s
    d.rectangle([cx - 62 * s, cy - 62 * s, cx + 62 * s, cy + 62 * s], fill=(28, 28, 30))
    d.text((cx, cy - 10 * s), 'RP2040', fill=(200, 200, 205), font=font(int(22 * s), True), anchor='mm')
    d.text((cx, cy + 18 * s), 'Raspberry Pi Pico', fill=(140, 140, 150), font=font(int(14 * s)), anchor='mm')
    d.text((cx, by + BH - 30 * s), 'vista de cima', fill=(200, 225, 210), font=font(int(16 * s)), anchor='mm')
    y0 = by + 110 * s
    fpin = font(int(15 * s), True, mono=True)
    fnum = font(int(13 * s), False)
    flab = font(int(17 * s), True)
    for side in (0, 1):
        names = LEFT if side == 0 else RIGHT
        for i, n in enumerate(names):
            y = y0 + i * P
            px = bx + 24 * s if side == 0 else bx + BW - 24 * s
            hl = highlight is None or n in highlight
            used = n in USE or n == 'GND'
            if n == 'GND':
                col = G['gnd'][0]
            elif n in USE:
                col = G[USE[n][0]][0]
            else:
                col = (160, 165, 175)
            if not hl:
                col = (200, 203, 210)
            d.ellipse([px - 13 * s, y - 13 * s, px + 13 * s, y + 13 * s], fill=(214, 178, 72) if hl else (150, 170, 155),
                      outline=(120, 90, 20), width=2)
            d.ellipse([px - 5 * s, y - 5 * s, px + 5 * s, y + 5 * s], fill=(40, 40, 40))
            num = i + 1 if side == 0 else 40 - i          # pela posição (há vários GND)
            tx = px + 22 * s if side == 0 else px - 22 * s
            d.text((tx, y), n, fill='white' if hl else (170, 200, 180), font=fpin, anchor='lm' if side == 0 else 'rm')
            d.text((px, y + 22 * s), str(num), fill=(200, 225, 210), font=fnum, anchor='mm')
            if not with_labels:
                continue
            if n in USE and hl:
                label = USE[n][1]
            elif n == 'GND' and hl:
                label = 'GND'
            elif n in FREE and highlight is None:
                label = 'livre'
            else:
                label = ''
            if not label:
                continue
            edge = bx if side == 0 else bx + BW
            lx = edge - 30 * s if side == 0 else edge + 30 * s
            d.line([edge if side == 0 else edge, y, lx, y], fill=col, width=int(4 * s))
            tw = d.textlength(label, font=flab)
            pad = 10 * s
            if side == 0:
                box = [lx - tw - 2 * pad, y - 16 * s, lx, y + 16 * s]
            else:
                box = [lx, y - 16 * s, lx + tw + 2 * pad, y + 16 * s]
            if label == 'livre':
                d.rounded_rectangle(box, int(8 * s), outline=(180, 184, 192), width=2, fill='white')
                d.text(((box[0] + box[2]) / 2, y), label, fill=(150, 155, 165), font=flab, anchor='mm')
            else:
                d.rounded_rectangle(box, int(8 * s), fill=col)
                d.text(((box[0] + box[2]) / 2, y), label, fill='white', font=flab, anchor='mm')
    im.save(path)
    return path, im.size


def module_diagram(path, title, mod_pins, links, mod_note=None, mod_color=(40, 44, 54), mod_w=330, extra=None):
    """Módulo à esquerda, coluna de pinos do Pico à direita, um fio reto por ligação.
    links: (pino do módulo, pino do Pico, grupo, observação)"""
    P = 58
    top = 110
    n = len(mod_pins)
    H = top + n * P + 70
    W = 1500
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    mx = 60
    d.rounded_rectangle([mx, 40, mx + mod_w, H - 30], 18, fill=mod_color)
    d.text((mx + mod_w / 2, 72), title, fill='white', font=font(30, True), anchor='mm')
    if mod_note:
        d.text((mx + mod_w / 2, H - 58), mod_note, fill=(170, 178, 192), font=font(19), anchor='mm')
    px = 1060
    d.rounded_rectangle([px, 40, px + 380, H - 30], 18, fill=(22, 101, 52))
    d.text((px + 190, 72), 'Raspberry Pi Pico', fill='white', font=font(30, True), anchor='mm')
    fl = font(24, True, mono=True)
    fn = font(19)
    lk = {l[0]: l for l in links}
    for i, mp in enumerate(mod_pins):
        y = top + i * P + P / 2
        hx = mx + mod_w - 26
        d.ellipse([hx - 12, y - 12, hx + 12, y + 12], fill=(214, 178, 72), outline=(120, 90, 20), width=2)
        d.text((hx - 26, y), mp, fill='white', font=fl, anchor='rm')
        if mp not in lk:
            d.text((hx + 30, y), 'não ligar', fill=(150, 155, 165), font=fn, anchor='lm')
            continue
        _, pin, grp, note = lk[mp]
        col = G[grp][0]
        d.line([hx + 12, y, px - 14, y], fill=col, width=7)
        d.ellipse([px - 26, y - 12, px - 2, y + 12], fill=(214, 178, 72), outline=(120, 90, 20), width=2)
        lab = pin if pin in ('GND',) or pin.startswith('GP') or '3V3' in pin or pin == 'VBUS' else pin
        num = phys(pin) if (pin in LEFT or pin in RIGHT) else None
        d.text((px + 14, y), lab, fill='white', font=fl, anchor='lm')
        if num:
            d.text((px + 366, y), f'pino {num}', fill=(190, 220, 200), font=fn, anchor='rm')
        if note:
            tw = d.textlength(note, font=fn)
            mid = (hx + px) / 2
            d.rounded_rectangle([mid - tw / 2 - 10, y - 30, mid + tw / 2 + 10, y - 6], 6, fill='white')
            d.text((mid, y - 18), note, fill=col, font=fn, anchor='mm')
    if extra:
        extra(d, W, H)
    im.save(path)
    return path, im.size


def encoder_bare(path):
    """EC11 sem placa: vista por baixo, 3 pinos de um lado (A C B) e 2 do botão do outro."""
    W, H = 1500, 560
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    cx, cy = 330, 280
    d.rounded_rectangle([cx - 150, cy - 140, cx + 150, cy + 140], 16, fill=(70, 74, 84))
    d.ellipse([cx - 70, cy - 70, cx + 70, cy + 70], fill=(150, 154, 162))
    d.text((cx, cy), 'EC11', fill='white', font=font(30, True), anchor='mm')
    d.text((cx, cy + 105), 'vista por baixo', fill=(220, 222, 228), font=font(20), anchor='mm')
    fl = font(24, True, mono=True)
    pins_a = [('A', cx - 70, 'GP2', 'enc'), ('C', cx, 'GND', 'gnd'), ('B', cx + 70, 'GP3', 'enc')]
    pins_s = [('SW1', cx - 50, 'GP4', 'enc'), ('SW2', cx + 50, 'GND', 'gnd')]
    px = 1060
    d.rounded_rectangle([px, 40, px + 380, H - 40], 18, fill=(22, 101, 52))
    d.text((px + 190, 72), 'Raspberry Pi Pico', fill='white', font=font(30, True), anchor='mm')
    targets = {'GP2': 150, 'GP3': 220, 'GP4': 330, 'GND': 420}
    for pin, ty in targets.items():
        d.ellipse([px - 26, ty - 12, px - 2, ty + 12], fill=(214, 178, 72), outline=(120, 90, 20), width=2)
        d.text((px + 14, ty), pin, fill='white', font=fl, anchor='lm')
        d.text((px + 366, ty), f'pino {phys(pin)}', fill=(190, 220, 200), font=font(19), anchor='rm')
    for k, (lab, x, pin, g) in enumerate(pins_a):     # pinos de cima: sobem e viram para a direita
        y = cy - 140
        d.rectangle([x - 7, y - 40, x + 7, y], fill=(214, 178, 72))
        d.text((x + 16, y - 26), lab, fill=(29, 36, 51), font=fl, anchor='lm')
        yy = 30 + (2 - k) * 22
        mid_x = 640 + k * 70
        d.line([(x, y - 40), (x, yy), (mid_x, yy), (mid_x, targets[pin]), (px - 26, targets[pin])],
               fill=G[g][0], width=7, joint='curve')
    for k, (lab, x, pin, g) in enumerate(pins_s):     # pinos do botão: descem e viram para a direita
        y = cy + 140
        d.rectangle([x - 7, y, x + 7, y + 40], fill=(214, 178, 72))
        d.text((x + 16, y + 26), lab, fill=(29, 36, 51), font=fl, anchor='lm')
        yy = H - 30 - (1 - k) * 22
        mid_x = 880 + k * 70
        d.line([(x, y + 40), (x, yy), (mid_x, yy), (mid_x, targets[pin]), (px - 26, targets[pin])],
               fill=G[g][0], width=7, joint='curve')
    im.save(path)
    return path, im.size


def power_diagram(path):
    """Árvore de alimentação e terra."""
    W, H = 1500, 640
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    red, blk = G['pwr'][0], G['gnd'][0]
    fb, fs = font(26, True), font(20)

    def box(x, y, w, h, t, sub=None, fill=(40, 44, 54)):
        d.rounded_rectangle([x, y, x + w, y + h], 14, fill=fill)
        d.text((x + w / 2, y + (h / 2 - (12 if sub else 0))), t, fill='white', font=fb, anchor='mm')
        if sub:
            d.text((x + w / 2, y + h / 2 + 20), sub, fill=(190, 196, 208), font=fs, anchor='mm')

    box(40, 250, 240, 120, 'USB do PC', '5 V, 500 mA', (90, 96, 110))
    d.rounded_rectangle([420, 190, 720, 410], 14, fill=(22, 101, 52))
    d.text((570, 220), 'Pico', fill='white', font=fb, anchor='mm')
    d.text((570, 265), 'VBUS (pino 40)', fill='white', font=fs, anchor='mm')
    d.text((570, 305), 'regulador 3,3 V', fill=(190, 220, 200), font=fs, anchor='mm')
    d.text((570, 345), '3V3(OUT) (pino 36)', fill='white', font=fs, anchor='mm')
    d.line([280, 310, 420, 310], fill=red, width=8)
    d.text((350, 290), '5 V', fill=red, font=fb, anchor='mm')
    loads = [('DAC UDA1334A', 'VIN  (5 V)', 70, 'VBUS'), ('Display ILI9341', 'VCC e LED  (3,3 V)', 250, '3V3'),
             ('Touch XPT2046', 'no mesmo módulo da tela', 390, None), ('Encoder', 'só o + do módulo KY-040', 520, '3V3')]
    for name, sub, y, src in loads:
        box(1040, y, 400, 96 if src else 76, name, sub)
        if src == 'VBUS':
            d.line([720, 265, 880, 265, 880, y + 48, 1040, y + 48], fill=red, width=7, joint='curve')
            d.text((960, y + 30), '5 V', fill=red, font=fs, anchor='mm')
        elif src == '3V3':
            d.line([720, 345, 900, 345, 900, y + 48, 1040, y + 48], fill=(234, 120, 40), width=7, joint='curve')
            d.text((970, y + 30), '3,3 V', fill=(234, 120, 40), font=fs, anchor='mm')
    d.line([570, 410, 570, 600, 1000, 600], fill=blk, width=7)
    d.text((600, 570), 'GND comum a todos os módulos', fill=blk, font=fb, anchor='lm')
    im.save(path)
    return path, im.size


def overview(path):
    """Visão geral: módulos em volta do Pico, um cabo colorido por grupo."""
    W, H = 1600, 1050
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    fb, fs, fm = font(30, True), font(20), font(19, False, True)
    # Pico
    px, py, pw, ph = 640, 300, 320, 520
    d.rounded_rectangle([px, py, px + pw, py + ph], 18, fill=(22, 101, 52))
    d.rounded_rectangle([px + pw / 2 - 50, py - 14, px + pw / 2 + 50, py + 40], 8, fill=(190, 194, 200))
    d.rectangle([px + pw / 2 - 60, py + 200, px + pw / 2 + 60, py + 320], fill=(28, 28, 30))
    d.text((px + pw / 2, py + 260), 'RP2040', fill='white', font=font(24, True), anchor='mm')
    d.text((px + pw / 2, py + 470), 'Raspberry Pi Pico', fill='white', font=font(22, True), anchor='mm')

    def module(x, y, w, h, title, sub, fill):
        d.rounded_rectangle([x, y, x + w, y + h], 18, fill=fill)
        d.text((x + w / 2, y + 40), title, fill='white', font=fb, anchor='mm')
        for k, line in enumerate(sub):
            d.text((x + w / 2, y + 82 + k * 28), line, fill=(200, 205, 215), font=fs, anchor='mm')

    # tela (com a interface de verdade)
    shot = os.path.join(HERE, 'emu', 'shots', 'osc1.ppm')
    module(40, 40, 520, 470, 'Display 3,2" ILI9341', ['SPI1 + touch XPT2046'], (40, 44, 54))
    if os.path.exists(shot):
        sc = Image.open(shot).convert('RGB').resize((400, 300), Image.LANCZOS)
        im.paste(sc, (100, 150))
    module(1060, 120, 500, 240, 'DAC UDA1334A', ['I2S, saída P2 própria', 'VIN 5 V'], (40, 44, 54))
    module(1060, 640, 500, 220, 'Encoder EC11', ['girar = cutoff', 'botão = PLAY / pânico'], (40, 44, 54))
    module(40, 760, 520, 200, 'USB', ['alimentação 5 V + MIDI', '(o próprio conector do Pico)'], (90, 96, 110))

    def bundle(pts, grp, label, lpos):
        col = G[grp][0]
        d.line(pts, fill=col, width=14, joint='curve')
        tw = d.textlength(label, font=fm)
        x, y = lpos
        d.rounded_rectangle([x - tw / 2 - 12, y - 18, x + tw / 2 + 12, y + 18], 9, fill=col)
        d.text((x, y), label, fill='white', font=fm, anchor='mm')

    bundle([(560, 330), (600, 330), (600, 600), (640, 600)], 'tft', 'GP10-15', (600, 470))
    bundle([(560, 400), (580, 400), (580, 880), (1000, 880), (1000, 700), (960, 700)], 'touch', 'GP12 GP17 GP21', (800, 880))
    bundle([(1060, 240), (1010, 240), (1010, 470), (960, 470)], 'dac', 'GP6 GP7 GP9', (1010, 380))
    bundle([(1060, 760), (1030, 760), (1030, 560), (960, 560)], 'enc', 'GP2 GP3 GP4', (1110, 590))
    bundle([(560, 860), (800, 860), (800, 820)], 'pwr', 'cabo USB', (680, 830))
    d.text((W / 2, H - 30), 'Cada cor = um grupo de fios. Os detalhes pino a pino estão nas páginas seguintes.',
           fill=(91, 100, 117), font=fs, anchor='mm')
    im.save(path)
    return path, im.size


# ---------------------------------------------------------------- PDF
st_body = ParagraphStyle('body', fontName='UI', fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=5)
st_small = ParagraphStyle('small', parent=st_body, fontSize=8.6, leading=12, textColor=SUB)
st_h1 = ParagraphStyle('h1', fontName='UIB', fontSize=21, leading=26, textColor=INK, spaceAfter=4)
st_kick = ParagraphStyle('k', fontName='UIB', fontSize=9, leading=12, textColor=ACC, spaceAfter=2)
st_h2 = ParagraphStyle('h2', fontName='UIB', fontSize=13, leading=17, textColor=INK, spaceBefore=10, spaceAfter=4)
st_cell = ParagraphStyle('c', parent=st_body, fontSize=9, leading=12, spaceAfter=0)
st_tip = ParagraphStyle('tip', parent=st_body, fontSize=9.2, leading=13, spaceAfter=0)
CW = A4[0] - 36 * mm


def img(ps, width):
    p, (w, h) = ps
    return RLImage(p, width=width, height=width * h / w)


def section(k, t, intro=None):
    out = [Paragraph(k, st_kick), Paragraph(t, st_h1)]
    if intro:
        out.append(Paragraph(intro, st_body))
    out.append(Spacer(1, 4))
    return out


def tip(text, label='DICA'):
    t = Table([[Paragraph(f'<font name="UIB" color="#e0562b">{label}</font>&nbsp;&nbsp;{text}', st_tip)]], colWidths=[CW])
    t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), PALE), ('LEFTPADDING', (0, 0), (-1, -1), 9),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 9), ('TOPPADDING', (0, 0), (-1, -1), 6),
                           ('BOTTOMPADDING', (0, 0), (-1, -1), 6), ('LINEBEFORE', (0, 0), (0, -1), 2.5, ACC)]))
    return KeepTogether([Spacer(1, 3), t, Spacer(1, 6)])


def table(rows, widths, swatch_col=None):
    data = []
    for r, row in enumerate(rows):
        cells = []
        for c, v in enumerate(row):
            if r > 0 and c == swatch_col:
                cells.append('')
            else:
                cells.append(Paragraph(str(v), ParagraphStyle('tc', parent=st_cell, fontName='UIB' if r == 0 else 'UI',
                                                              textColor=colors.white if r == 0 else INK)))
        data.append(cells)
    t = Table(data, colWidths=widths, repeatRows=1)
    style = [('VALIGN', (0, 0), (-1, -1), 'MIDDLE'), ('GRID', (0, 0), (-1, -1), 0.5, LINE),
             ('TOPPADDING', (0, 0), (-1, -1), 3.5), ('BOTTOMPADDING', (0, 0), (-1, -1), 3.5),
             ('LEFTPADDING', (0, 0), (-1, -1), 6), ('BACKGROUND', (0, 0), (-1, 0), INK)]
    for r in range(1, len(rows)):
        if r % 2 == 0:
            style.append(('BACKGROUND', (0, r), (-1, r), PALE))
        if swatch_col is not None:
            rgb = G[rows[r][swatch_col]][0]
            style.append(('BACKGROUND', (swatch_col, r), (swatch_col, r), colors.Color(*[v / 255 for v in rgb])))
    t.setStyle(TableStyle(style))
    return t


def on_page(c, doc):
    c.saveState()
    c.setStrokeColor(LINE)
    c.setLineWidth(0.6)
    c.line(18 * mm, 14 * mm, A4[0] - 18 * mm, 14 * mm)
    c.setFont('UI', 8)
    c.setFillColor(SUB)
    c.drawString(18 * mm, 9.5 * mm, 'Tobri Synth · Esquema de ligação · firmware v6')
    c.drawRightString(A4[0] - 18 * mm, 9.5 * mm, str(doc.page))
    c.restoreState()


def build():
    s = []
    s += section('ESQUEMA DE LIGAÇÃO', 'Tobri Synth no Raspberry Pi Pico',
                 'Como ligar a tela touch de 3,2", o DAC UDA1334A e o encoder no Pico (RP2040). Os pinos são os que '
                 'o firmware v6 usa; se mudar algum aqui, mude também as constantes no começo do código.')
    s.append(img(overview(os.path.join(FIG, 'overview.png')), CW))
    s.append(Spacer(1, 6))
    s.append(table([['Cor', 'Grupo', 'Pinos do Pico'],
                    ['tft', 'Display (SPI1)', 'GP13 CS, GP14 DC, GP15 RESET'],
                    ['spi', 'SPI1 compartilhado (tela + touch)', 'GP10 SCK, GP11 MOSI'],
                    ['touch', 'Touch XPT2046', 'GP12 MISO, GP17 T_CS, GP21 T_IRQ'],
                    ['dac', 'DAC I2S', 'GP6 BCLK, GP7 LRCLK, GP9 DIN'],
                    ['enc', 'Encoder', 'GP2 CLK, GP3 DT, GP4 SW'],
                    ['pwr', 'Alimentação', 'VBUS (5 V) para o DAC, 3V3(OUT) para tela e encoder'],
                    ['gnd', 'Terra', 'qualquer GND do Pico, comum a todos']], [12 * mm, 62 * mm, CW - 74 * mm], swatch_col=0))
    s.append(PageBreak())

    s += section('1 · MAPA DE PINOS', 'Pinagem do Pico usada no projeto',
                 'Pico visto de cima, com o conector USB para cima. O número embaixo de cada pino é a posição física '
                 '(1 a 40). Os marcados "livre" estão disponíveis para expansões, como as entradas Eurorack.')
    s.append(img(pico_pinout(os.path.join(FIG, 'pinout.png')), CW))
    s.append(PageBreak())

    s += section('2 · TABELA COMPLETA', 'Ligação fio a fio')
    rows = [['Cor', 'Módulo', 'Pino do módulo', 'Pico', 'Pino físico']]
    conn = [
        ('pwr', 'Display', 'VCC', '3V3(OUT)'), ('gnd', 'Display', 'GND', 'GND'),
        ('tft', 'Display', 'CS', 'GP13'), ('tft', 'Display', 'RESET', 'GP15'), ('tft', 'Display', 'DC / RS', 'GP14'),
        ('spi', 'Display', 'SDI (MOSI)', 'GP11'), ('spi', 'Display', 'SCK', 'GP10'), ('pwr', 'Display', 'LED', '3V3(OUT)'),
        ('touch', 'Display', 'SDO (MISO)', '(não ligar)'),
        ('spi', 'Touch', 'T_CLK', 'GP10'), ('touch', 'Touch', 'T_CS', 'GP17'), ('spi', 'Touch', 'T_DIN', 'GP11'),
        ('touch', 'Touch', 'T_DO', 'GP12'), ('touch', 'Touch', 'T_IRQ', 'GP21'),
        ('pwr', 'DAC UDA1334A', 'VIN', 'VBUS'), ('gnd', 'DAC UDA1334A', 'GND', 'GND'),
        ('dac', 'DAC UDA1334A', 'BCLK', 'GP6'), ('dac', 'DAC UDA1334A', 'WSEL (LRCLK)', 'GP7'),
        ('dac', 'DAC UDA1334A', 'DIN', 'GP9'),
        ('enc', 'Encoder', 'CLK / A', 'GP2'), ('enc', 'Encoder', 'DT / B', 'GP3'), ('enc', 'Encoder', 'SW', 'GP4'),
        ('gnd', 'Encoder', 'GND / C / SW2', 'GND'), ('pwr', 'Encoder', '+ (só no KY-040)', '3V3(OUT)'),
    ]
    for g, m, mp, pp in conn:
        num = str(phys(pp)) if pp in LEFT or pp in RIGHT else ('3, 8, 13, 18, 23, 28, 33 ou 38' if pp == 'GND' else '-')
        if pp == 'GND':
            num = 'qualquer GND (3, 8, 13, 18, 23, 28, 33, 38)'
        rows.append([g, m, mp, pp, num])
    s.append(table(rows, [12 * mm, 32 * mm, 38 * mm, 28 * mm, CW - 110 * mm], swatch_col=0))
    s.append(tip('O LRCLK <b>tem</b> que ficar no pino seguinte ao BCLK (GP6 &gt; GP7): a biblioteca I2S do core RP2040 '
                 'usa os dois em sequência. Por isso o código só define BCLK e DATA.', 'IMPORTANTE'))
    s.append(PageBreak())

    s += section('3 · DISPLAY', 'Tela 3,2" ILI9341 + touch XPT2046',
                 'O módulo tem duas fileiras de pinos: uma para a tela e outra (T_...) para o touch. Os dois dividem '
                 'o mesmo barramento SPI1: SCK e MOSI vão para os mesmos pinos do Pico.')
    s.append(img(module_diagram(os.path.join(FIG, 'tft.png'), 'Display 3,2"',
                                ['VCC', 'GND', 'CS', 'RESET', 'DC/RS', 'SDI', 'SCK', 'LED', 'SDO', 'T_CLK', 'T_CS',
                                 'T_DIN', 'T_DO', 'T_IRQ'],
                                [('VCC', '3V3(OUT)', 'pwr', '3,3 V'), ('GND', 'GND', 'gnd', None),
                                 ('CS', 'GP13', 'tft', None), ('RESET', 'GP15', 'tft', None), ('DC/RS', 'GP14', 'tft', None),
                                 ('SDI', 'GP11', 'spi', 'MOSI'), ('SCK', 'GP10', 'spi', 'SCK'),
                                 ('LED', '3V3(OUT)', 'pwr', 'backlight'),
                                 ('T_CLK', 'GP10', 'spi', 'junto com SCK'), ('T_CS', 'GP17', 'touch', None),
                                 ('T_DIN', 'GP11', 'spi', 'junto com SDI'), ('T_DO', 'GP12', 'touch', 'MISO'),
                                 ('T_IRQ', 'GP21', 'touch', None)],
                                mod_note='ILI9341 · SPI'), CW))
    s.append(Spacer(1, 4))
    s.append(Paragraph('<b>Por que o SDO (MISO) da tela fica solto?</b> O firmware só escreve na tela, nunca lê. '
                       'Muitos módulos ILI9341 não liberam a linha MISO quando o CS está desligado, e isso atrapalha a '
                       'leitura do touch, que usa o mesmo GP12. Deixar o SDO desligado evita o problema.', st_body))
    s.append(Paragraph('<b>Backlight (LED):</b> ligado direto no 3,3 V a tela fica sempre acesa. A maioria dos módulos '
                       'já tem resistor; se o seu não tiver, coloque um de 10 a 33 Ω em série.', st_body))
    s.append(Paragraph('<b>VCC:</b> o módulo também aceita 5 V se tiver regulador (procure o jumper J1 aberto e um '
                       'regulador perto dos pinos). Em 3,3 V funciona com ou sem regulador, então é a escolha segura.',
                       st_body))
    s.append(PageBreak())

    s += section('4 · ÁUDIO', 'DAC UDA1334A (I2S)',
                 'O som sai pela própria saída P2 (3,5 mm) da placa do DAC. São só 3 fios de sinal mais alimentação.')
    s.append(img(module_diagram(os.path.join(FIG, 'dac.png'), 'UDA1334A',
                                ['VIN', 'GND', 'WSEL', 'DIN', 'BCLK', 'SF0/SF1', 'MUTE'],
                                [('VIN', 'VBUS', 'pwr', '5 V'), ('GND', 'GND', 'gnd', None),
                                 ('WSEL', 'GP7', 'dac', 'LRCLK'), ('DIN', 'GP9', 'dac', 'dados'),
                                 ('BCLK', 'GP6', 'dac', 'clock de bit')],
                                mod_note='saída: P2 da placa', mod_color=(30, 64, 120)), CW))
    s.append(Spacer(1, 4))
    s.append(Paragraph('<b>SF0, SF1, MUTE, PLL e DEEM</b>: deixe soltos. Os padrões da placa já são formato I2S e som '
                       'ligado. <b>VIN</b> aceita 3 a 5 V; ligar no VBUS (5 V) tira carga do regulador de 3,3 V do Pico '
                       'e dá mais folga ao DAC. Se for alimentar o Pico pelo VSYS em vez do USB, use 3V3(OUT).', st_body))
    s.append(tip('Fios de I2S curtos (até ~10 cm) e um GND do DAC ligado perto dos sinais (pino 8 ou 13 do Pico) '
                 'evitam chiado e estalos. Um capacitor de 10 µF + 100 nF entre VIN e GND, junto da placa, ajuda.'))

    s += section('5 · CONTROLE', 'Encoder EC11 (ou módulo KY-040)',
                 'O firmware liga os resistores de pull-up internos do Pico, então o encoder sem placa funciona sem '
                 'nenhum componente extra.')
    s.append(Paragraph('Encoder sem placa (EC11 puro)', st_h2))
    s.append(img(encoder_bare(os.path.join(FIG, 'enc_bare.png')), CW))
    s.append(Paragraph('Na fileira de 3 pinos, o do <b>meio (C)</b> é o comum e vai no GND. Na fileira de 2 pinos fica '
                       'o botão: um lado no GP4 e o outro no GND.', st_body))
    s.append(CondPageBreak(90 * mm))
    s.append(Paragraph('Módulo KY-040 (com placa)', st_h2))
    s.append(img(module_diagram(os.path.join(FIG, 'enc_ky.png'), 'KY-040',
                                ['CLK', 'DT', 'SW', '+', 'GND'],
                                [('CLK', 'GP2', 'enc', None), ('DT', 'GP3', 'enc', None), ('SW', 'GP4', 'enc', 'botão'),
                                 ('+', '3V3(OUT)', 'pwr', 'nunca 5 V'), ('GND', 'GND', 'gnd', None)],
                                mod_note='encoder com placa', mod_color=(120, 60, 20)), CW))
    s.append(tip('O + do KY-040 vai no <b>3,3 V</b>, nunca no 5 V: os pull-ups da placa levariam 5 V para os pinos do '
                 'Pico, que só aceitam 3,3 V. Se o cutoff andar ao contrário, troque CLK e DT (ou mude '
                 '<font name="Mono">ENC_INVERT</font> no código).', 'ATENÇÃO'))
    s.append(CondPageBreak(120 * mm))

    s += section('6 · ALIMENTAÇÃO', 'Energia e terra')
    s.append(img(power_diagram(os.path.join(FIG, 'power.png')), CW))
    s.append(Spacer(1, 4))
    s.append(table([['Consumo aproximado', 'Corrente'],
                    ['Pico (dois núcleos rodando)', '~ 30 mA'],
                    ['Display 3,2" com backlight', '~ 60-90 mA'],
                    ['DAC UDA1334A', '~ 10-20 mA'],
                    ['Total', '~ 100-140 mA (cabe com folga nos 500 mA do USB)']], [90 * mm, CW - 90 * mm]))
    s.append(Spacer(1, 6))
    s.append(Paragraph('Todos os GND precisam estar ligados entre si. O Pico tem 8 pinos GND (3, 8, 13, 18, 23, 28, 33, '
                       '38), todos iguais: use o mais perto de cada módulo.', st_body))

    s.append(CondPageBreak(150 * mm))
    s += section('7 · CONFERÊNCIA', 'Antes de ligar pela primeira vez')
    s.append(table([['', 'Verificação'],
                    ['1', 'Com o multímetro em continuidade, confira que 3V3 e VBUS <b>não</b> estão em curto com GND.'],
                    ['2', 'Confira que nenhum fio de 5 V (VBUS) chega em pino GP.'],
                    ['3', 'GP6 e GP7 vão no BCLK e no WSEL do DAC, nessa ordem.'],
                    ['4', 'SCK (GP10) e MOSI (GP11) vão tanto na tela (SCK/SDI) quanto no touch (T_CLK/T_DIN).'],
                    ['5', 'SDO da tela está solto; T_DO do touch está no GP12.'],
                    ['6', 'Na IDE: placa <b>Raspberry Pi Pico</b>, <b>USB Stack: Adafruit TinyUSB</b>.'],
                    ['7', 'Ao ligar: a tela acende na aba ENV. Toque nas abas; se o toque estiver deslocado, faça a '
                          'calibração (PRE p2 &gt; TOUCH CAL).']], [10 * mm, CW - 10 * mm]))
    s.append(Spacer(1, 6))
    s.append(table([['Sintoma', 'Provável causa'],
                    ['Tela branca', 'CS/DC/RESET trocados, SCK/SDI invertidos ou VCC faltando.'],
                    ['Tela funciona, toque não', 'T_CS (GP17) ou T_DO (GP12) solto, ou o SDO da tela ligado no GP12.'],
                    ['Toque espelhado ou fora do lugar', 'Faça a calibração do touch.'],
                    ['Sem som, tela OK', 'BCLK e WSEL trocados, DIN no pino errado, DAC sem VIN ou GND.'],
                    ['Chiado/zumbido', 'Fios de I2S longos ou GND do DAC longe; ligue o fone direto, sem passar pelo PC.'],
                    ['Encoder pula ou não muda', 'C (comum) do encoder não está no GND, ou CLK/DT soltos.']],
                   [55 * mm, CW - 55 * mm]))
    return s


doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=20 * mm,
                      title='Tobri Synth - Esquema de ligação', author='Tobri Synth')
doc.addPageTemplates([PageTemplate(id='body', frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height)],
                                   onPage=on_page)])
doc.build(build())
print('ok', OUT)
