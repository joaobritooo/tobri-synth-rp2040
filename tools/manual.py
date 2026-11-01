# Gera o manual ilustrado do Tobri Synth (PDF) a partir das capturas do emulador.
import os, sys
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, CondPageBreak, KeepTogether, NextPageTemplate)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, 'emu', 'shots')
FIG = os.path.join(HERE, 'fig')
OUT = sys.argv[1]
os.makedirs(FIG, exist_ok=True)

F = 'C:/Windows/Fonts/'
pdfmetrics.registerFont(TTFont('UI', F + 'segoeui.ttf'))
pdfmetrics.registerFont(TTFont('UIB', F + 'segoeuib.ttf'))
pdfmetrics.registerFont(TTFont('UII', F + 'segoeuii.ttf'))
pdfmetrics.registerFont(TTFont('Mono', F + 'consola.ttf'))
from reportlab.pdfbase.pdfmetrics import registerFontFamily
registerFontFamily('UI', normal='UI', bold='UIB', italic='UII', boldItalic='UIB')

INK = colors.HexColor('#1d2433')
SUB = colors.HexColor('#5b6475')
ACC = colors.HexColor('#e0562b')
ACC_RGB = (224, 86, 43)
PALE = colors.HexColor('#f3f4f7')
LINE = colors.HexColor('#d9dce3')

# ---------------------------------------------------------------- figuras
pil_font = ImageFont.truetype(F + 'segoeuib.ttf', 22)
pil_small = ImageFont.truetype(F + 'segoeuib.ttf', 30)


def load_shot(name):
    return Image.open(os.path.join(SHOTS, name + '.ppm')).convert('RGB')


def screen(name, callouts=(), scale=2, inline=False):
    """Captura da tela com moldura e marcadores numerados na margem direita."""
    im = load_shot(name).resize((320 * scale, 240 * scale), Image.NEAREST)
    pad, bez, margin = 14, 16, 70 if (callouts and not inline) else 0
    W = 320 * scale + 2 * bez + 2 * pad + margin
    H = 240 * scale + 2 * bez + 2 * pad
    c = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(c)
    d.rounded_rectangle([pad, pad, pad + 320 * scale + 2 * bez, pad + 240 * scale + 2 * bez], 18, fill=(38, 42, 52))
    ox, oy = pad + bez, pad + bez
    c.paste(im, (ox, oy))
    used = []
    if inline:                               # número direto sobre o alvo (telas com muitos botões)
        for i, co in enumerate(callouts):
            px, py = ox + co[0] * scale, oy + co[1] * scale
            d.ellipse([px - 15, py - 15, px + 15, py + 15], fill=ACC_RGB, outline='white', width=3)
            t = str(i + 1)
            f = pil_font if len(t) == 1 else ImageFont.truetype(F + 'segoeuib.ttf', 18)
            tw = d.textlength(t, font=f)
            d.text((px - tw / 2, py - (15 if len(t) == 1 else 12)), t, fill='white', font=f)
        callouts = ()
    for i, co in enumerate(callouts):
        tx, ty = co[0], co[1]
        if tx == 160:                        # linha inteira: aponta para a borda, sem riscar a tela
            tx = 318
        ly = co[2] if len(co) > 2 else ty
        px, py = ox + tx * scale, oy + ty * scale
        cy = oy + ly * scale
        for u in used:                       # evita marcadores encavalados
            if abs(cy - u) < 40:
                cy = u + 40
        used.append(cy)
        cx = W - margin / 2 - 4
        d.line([px, py, cx - 20, cy], fill=ACC_RGB, width=3)
        d.ellipse([px - 6, py - 6, px + 6, py + 6], fill=ACC_RGB, outline='white', width=2)
        d.ellipse([cx - 20, cy - 20, cx + 20, cy + 20], fill=ACC_RGB)
        t = str(i + 1)
        tw = d.textlength(t, font=pil_font)
        d.text((cx - tw / 2, cy - 15), t, fill='white', font=pil_font)
    path = os.path.join(FIG, name + ('_a' if callouts else '') + '.png')
    c.save(path)
    return path, c.size


def panel_figure():
    """Vista frontal do painel 20 HP com a tela ligada (desenho em escala, 5 px/mm)."""
    s = 5.0
    PW, PH = 101.3, 128.5
    W, H = int(PW * s) + 220, int(PH * s) + 20
    c = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(c)
    x0, y0 = 10, 10
    Y = lambda ymm: y0 + (PH - ymm) * s           # y do SCAD (de baixo) -> imagem
    X = lambda xmm: x0 + xmm * s
    d.rounded_rectangle([x0, y0, X(PW), Y(0)], 10, fill=(52, 58, 72), outline=(30, 34, 42), width=3)
    for mx in (7.5, 7.5 + 17 * 5.08):
        for my in (3, PH - 3):
            d.rounded_rectangle([X(mx) - 13, Y(my) - 8, X(mx) + 13, Y(my) + 8], 8, fill='white')
    # tela
    ww, wh = 66.4, 50.2
    sx0, sy0 = X(PW / 2 - ww / 2), Y(89 + wh / 2)
    shot = load_shot('osc1').resize((int(ww * s), int(wh * s)), Image.LANCZOS)
    d.rounded_rectangle([sx0 - 8, sy0 - 8, sx0 + ww * s + 8, sy0 + wh * s + 8], 8, fill=(20, 22, 28))
    c.paste(shot, (int(sx0), int(sy0)))
    # encoder
    ex, ey = X(PW / 2), Y(46)
    import math
    for k in range(11):
        a = math.radians(-135 + 27 * k - 90)
        r1, r2 = 12.5 * s, 14.5 * s
        d.line([ex + r1 * math.cos(a), ey + r1 * math.sin(a), ex + r2 * math.cos(a), ey + r2 * math.sin(a)], fill=(235, 160, 90), width=3)
    d.ellipse([ex - 10 * s, ey - 10 * s, ex + 10 * s, ey + 10 * s], fill=(25, 27, 33), outline=(90, 95, 110), width=3)
    d.line([ex, ey - 9 * s, ex, ey - 4 * s], fill='white', width=4)
    # P2 e USB
    ox_, oy_ = X(PW / 2 - 16), Y(21.1)
    d.ellipse([ox_ - 5 * s, oy_ - 5 * s, ox_ + 5 * s, oy_ + 5 * s], fill=(235, 160, 90))
    d.ellipse([ox_ - 3.2 * s, oy_ - 3.2 * s, ox_ + 3.2 * s, oy_ + 3.2 * s], fill=(15, 15, 18))
    ux, uy = X(PW / 2 + 16), Y(21)
    d.rounded_rectangle([ux - 4.9 * s, uy - 2 * s, ux + 4.9 * s, uy + 2 * s], 5, fill=(15, 15, 18))
    lf = ImageFont.truetype(F + 'segoeuib.ttf', 15)
    for txt, x, y in (('TOBRI', PW / 2, PH - 6.5), ('OUT', PW / 2 - 16, 12.6), ('USB MIDI', PW / 2 + 16, 14.5), ('PUSH', PW / 2, 32)):
        tw = d.textlength(txt, font=lf)
        d.text((X(x) - tw / 2, Y(y) - 9), txt, fill=(235, 160, 90), font=lf)
    # marcadores
    pts = [(X(PW / 2 + 30), Y(100)), (ex + 10 * s, ey), (ox_ - 5 * s, oy_), (ux + 5 * s, uy)]
    ys = [Y(100), ey, oy_ - 25, uy + 25]
    for i, ((px, py), ly) in enumerate(zip(pts, ys)):
        cx = W - 40
        d.line([px, py, cx - 20, ly], fill=ACC_RGB, width=3)
        d.ellipse([px - 6, py - 6, px + 6, py + 6], fill=ACC_RGB, outline='white', width=2)
        d.ellipse([cx - 20, ly - 20, cx + 20, ly + 20], fill=ACC_RGB)
        t = str(i + 1)
        tw = d.textlength(t, font=pil_font)
        d.text((cx - tw / 2, ly - 15), t, fill='white', font=pil_font)
    path = os.path.join(FIG, 'panel.png')
    c.save(path)
    return path, c.size


def encoder_figure():
    """Quatro gestos do encoder lado a lado."""
    import math
    W, H = 1400, 330
    c = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(c)
    tf = ImageFont.truetype(F + 'segoeuib.ttf', 30)
    sf = ImageFont.truetype(F + 'segoeui.ttf', 24)
    items = [('GIRAR', 'cutoff do filtro'), ('SEGURAR + GIRAR', 'ressonância'),
             ('CLIQUE', 'PLAY / STOP do SEQ'), ('SEGURAR 0,8 s', 'pânico (cala tudo)')]
    for i, (t, s_) in enumerate(items):
        cx, cy = 175 + i * 350, 130
        d.ellipse([cx - 70, cy - 70, cx + 70, cy + 70], fill=(30, 33, 40))
        d.ellipse([cx - 58, cy - 58, cx + 58, cy + 58], outline=(70, 75, 90), width=3)
        d.line([cx, cy - 64, cx, cy - 34], fill='white', width=6)
        if i in (0, 1):
            d.arc([cx - 95, cy - 95, cx + 95, cy + 95], 200, 340, fill=ACC_RGB, width=7)
            a = math.radians(340)
            hx, hy = cx + 95 * math.cos(a), cy + 95 * math.sin(a)
            d.polygon([(hx + 14, hy - 4), (hx - 10, hy - 14), (hx - 4, hy + 12)], fill=ACC_RGB)
        if i in (1, 2, 3):
            d.ellipse([cx - 22, cy - 22, cx + 22, cy + 22], fill=ACC_RGB)
            d.line([cx, cy - 120, cx, cy - 78], fill=ACC_RGB, width=7)
            d.polygon([(cx - 14, cy - 86), (cx + 14, cy - 86), (cx, cy - 70)], fill=ACC_RGB)
        if i == 3:
            d.arc([cx - 40, cy - 40, cx + 40, cy + 40], -90, 200, fill='white', width=5)
        tw = d.textlength(t, font=tf)
        d.text((cx - tw / 2, 225), t, fill=(29, 36, 51), font=tf)
        tw = d.textlength(s_, font=sf)
        d.text((cx - tw / 2, 268), s_, fill=(91, 100, 117), font=sf)
    path = os.path.join(FIG, 'encoder.png')
    c.save(path)
    return path, c.size


def gesture_figure():
    """Ícones dos gestos de toque."""
    W, H = 1400, 300
    c = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(c)
    tf = ImageFont.truetype(F + 'segoeuib.ttf', 26)
    sf = ImageFont.truetype(F + 'segoeui.ttf', 21)
    items = [('TOCAR', 'botões e células'), ('ARRASTAR', 'sliders e gráficos'), ('AFASTAR O DEDO', 'ajuste FINE'),
             ('TOQUE DUPLO', 'slider volta ao padrão'), ('SWIPE', 'troca a subpágina')]
    gray, dark = (215, 219, 228), (29, 36, 51)
    for i, (t, s_) in enumerate(items):
        cx, cy = 140 + i * 280, 110
        d.rounded_rectangle([cx - 110, cy - 75, cx + 110, cy + 75], 14, outline=gray, width=4)
        if i == 0:
            d.rounded_rectangle([cx - 55, cy - 22, cx + 55, cy + 22], 10, outline=dark, width=4)
            d.ellipse([cx - 16, cy - 16, cx + 16, cy + 16], fill=ACC_RGB)
        elif i in (1, 2, 3):
            by = cy + (30 if i == 2 else 0)
            d.rounded_rectangle([cx - 90, by - 12, cx + 90, by + 12], 12, outline=dark, width=4)
            d.rounded_rectangle([cx - 90, by - 12, cx + 10, by + 12], 12, fill=dark)
            if i == 1:
                d.ellipse([cx - 4, by - 16, cx + 28, by + 16], fill=ACC_RGB)
                d.line([cx + 34, by, cx + 75, by], fill=ACC_RGB, width=5)
                d.polygon([(cx + 75, by - 10), (cx + 90, by), (cx + 75, by + 10)], fill=ACC_RGB)
            elif i == 2:
                d.line([cx + 12, by - 18, cx + 12, by - 60], fill=ACC_RGB, width=5)
                d.ellipse([cx - 4, by - 84, cx + 28, by - 52], fill=ACC_RGB)
                d.text((cx + 34, by - 82), 'FINE', fill=ACC_RGB, font=sf)
            else:
                d.ellipse([cx - 4, by - 16, cx + 28, by + 16], fill=ACC_RGB)
                d.text((cx - 6, by - 60), '2x', fill=ACC_RGB, font=tf)
        else:
            d.ellipse([cx + 30, cy - 16, cx + 62, cy + 16], fill=ACC_RGB)
            d.line([cx - 70, cy, cx + 24, cy], fill=ACC_RGB, width=5)
            d.polygon([(cx - 70, cy - 12), (cx - 88, cy), (cx - 70, cy + 12)], fill=ACC_RGB)
        tw = d.textlength(t, font=tf)
        d.text((cx - tw / 2, 200), t, fill=dark, font=tf)
        tw = d.textlength(s_, font=sf)
        d.text((cx - tw / 2, 240), s_, fill=(91, 100, 117), font=sf)
    path = os.path.join(FIG, 'gestures.png')
    c.save(path)
    return path, c.size


# ---------------------------------------------------------------- estilos
st_body = ParagraphStyle('body', fontName='UI', fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=5)
st_small = ParagraphStyle('small', parent=st_body, fontSize=8.6, leading=12, textColor=SUB)
st_leg = ParagraphStyle('leg', parent=st_body, fontSize=9.2, leading=12.6, spaceAfter=0, leftIndent=0)
st_h1 = ParagraphStyle('h1', fontName='UIB', fontSize=21, leading=26, textColor=INK, spaceBefore=0, spaceAfter=4)
st_kicker = ParagraphStyle('kick', fontName='UIB', fontSize=9, leading=12, textColor=ACC, spaceAfter=2)
st_h2 = ParagraphStyle('h2', fontName='UIB', fontSize=13, leading=17, textColor=INK, spaceBefore=10, spaceAfter=4)
st_h3 = ParagraphStyle('h3', fontName='UIB', fontSize=10.5, leading=14, textColor=INK, spaceBefore=6, spaceAfter=2)
st_cap = ParagraphStyle('cap', parent=st_small, alignment=TA_CENTER, spaceBefore=2)
st_tip = ParagraphStyle('tip', parent=st_body, fontSize=9.2, leading=13, spaceAfter=0)

CONTENT_W = A4[0] - 36 * mm


def img(path_size, width):
    path, (w, h) = path_size
    return RLImage(path, width=width, height=width * h / w)


def legend(items):
    rows = []
    for i, txt in enumerate(items):
        num = Paragraph(f'<font color="white"><b>{i + 1}</b></font>', ParagraphStyle('n', parent=st_leg, alignment=TA_CENTER, fontName='UIB'))
        rows.append([num, Paragraph(txt, st_leg)])
    t = Table(rows, colWidths=[6.2 * mm, None])
    style = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 0),
             ('RIGHTPADDING', (0, 0), (0, -1), 0), ('LEFTPADDING', (1, 0), (1, -1), 6),
             ('TOPPADDING', (0, 0), (-1, -1), 2.2), ('BOTTOMPADDING', (0, 0), (-1, -1), 2.2)]
    for i in range(len(rows)):
        style.append(('BACKGROUND', (0, i), (0, i), ACC))
    t.setStyle(TableStyle(style))
    return t


def shot_block(name, callouts, items, title=None, note=None, img_w=88 * mm, inline=False):
    fig = img(screen(name, callouts, inline=inline), img_w)
    right = []
    if title:
        right.append(Paragraph(title, st_h3))
    right.append(legend(items))
    if note:
        right.append(Spacer(1, 4))
        right.append(Paragraph(note, st_small))
    t = Table([[fig, right]], colWidths=[img_w + 4 * mm, CONTENT_W - img_w - 4 * mm])
    t.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 0),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 0), ('TOPPADDING', (0, 0), (-1, -1), 0),
                           ('BOTTOMPADDING', (0, 0), (-1, -1), 8)]))
    return KeepTogether([t])


def tip(text, label='DICA'):
    p = Paragraph(f'<font name="UIB" color="#e0562b">{label}</font>&nbsp;&nbsp;{text}', st_tip)
    t = Table([[p]], colWidths=[CONTENT_W])
    t.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), PALE), ('LEFTPADDING', (0, 0), (-1, -1), 9),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 9), ('TOPPADDING', (0, 0), (-1, -1), 6),
                           ('BOTTOMPADDING', (0, 0), (-1, -1), 6), ('LINEBEFORE', (0, 0), (0, -1), 2.5, ACC)]))
    return KeepTogether([Spacer(1, 3), t, Spacer(1, 6)])


def section(kicker, title, intro=None):
    out = [Paragraph(kicker, st_kicker), Paragraph(title, st_h1)]
    if intro:
        out.append(Paragraph(intro, st_body))
    out.append(Spacer(1, 4))
    return out


def table(rows, widths, header=True):
    data = [[Paragraph(str(c), ParagraphStyle('tc', parent=st_leg, fontName='UIB' if (header and r == 0) else 'UI',
                                              textColor=colors.white if (header and r == 0) else INK))
             for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('GRID', (0, 0), (-1, -1), 0.5, LINE),
             ('TOPPADDING', (0, 0), (-1, -1), 3.5), ('BOTTOMPADDING', (0, 0), (-1, -1), 3.5),
             ('LEFTPADDING', (0, 0), (-1, -1), 6)]
    if header:
        style.append(('BACKGROUND', (0, 0), (-1, 0), INK))
    for r in range(1 if header else 0, len(rows)):
        if r % 2 == 0:
            style.append(('BACKGROUND', (0, r), (-1, r), PALE))
    t.setStyle(TableStyle(style))
    return t


# ---------------------------------------------------------------- páginas
def on_page(c, doc):
    c.saveState()
    c.setStrokeColor(LINE)
    c.setLineWidth(0.6)
    c.line(18 * mm, 14 * mm, A4[0] - 18 * mm, 14 * mm)
    c.setFont('UI', 8)
    c.setFillColor(SUB)
    c.drawString(18 * mm, 9.5 * mm, 'Tobri Synth · Manual de uso · firmware v11')
    c.drawRightString(A4[0] - 18 * mm, 9.5 * mm, str(doc.page))
    c.restoreState()


def on_cover(c, doc):
    c.saveState()
    c.setFillColor(INK)
    c.rect(0, A4[1] - 118 * mm, A4[0], 118 * mm, fill=1, stroke=0)
    c.setFillColor(ACC)
    c.rect(0, A4[1] - 120 * mm, A4[0], 2 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont('UIB', 38)
    c.drawString(20 * mm, A4[1] - 45 * mm, 'Tobri Synth')
    c.setFont('UI', 16)
    c.setFillColor(colors.HexColor('#c9cfdb'))
    c.drawString(20 * mm, A4[1] - 56 * mm, 'Manual de uso ilustrado')
    c.setFont('UI', 10.5)
    y = A4[1] - 74 * mm
    for line in ('Sintetizador polifônico de 4 vozes com RP2040, tela touch de 3,2"',
                 'USB MIDI e encoder · firmware v11',
                 '7 abas, 23 telas: osciladores, wavetable, FM, envelopes, filtro, LFO,',
                 'matriz de modulação, sequenciador de 16 passos, 4 efeitos e 8 presets'):
        c.drawString(20 * mm, y, line)
        y -= 6 * mm
    c.restoreState()


def build():
    story = []
    W = CONTENT_W

    # capa
    story.append(Spacer(1, 108 * mm))
    story.append(img(panel_figure(), 120 * mm))
    story.append(Spacer(1, 4 * mm))
    story.append(legend(['<b>Tela touch</b>: todos os parâmetros, em 7 abas.',
                         '<b>Encoder</b>: gire para o cutoff; segure e gire para a ressonância; clique = PLAY/STOP.',
                         '<b>OUT (P2)</b>: saída de áudio estéreo do DAC. Fone ou entrada de linha.',
                         '<b>USB MIDI</b>: alimentação + MIDI. Aparece no computador como "RP2040 Synth".']))
    story.append(NextPageTemplate('body'))
    story.append(PageBreak())

    # ----- 1. primeiros passos
    story += section('1 · COMEÇANDO', 'Primeiros passos')
    steps = [
        ['1', '<b>Ligue o cabo USB</b> no computador ou num carregador. Aparece a abertura (números caindo e o nome TOBRI SYNTH) e, depois de uns 3 s, a aba <b>ENV</b>.'],
        ['2', '<b>Conecte a saída OUT (P2)</b> num fone, caixa ativa ou mesa. Comece com o volume baixo.'],
        ['3', '<b>Toque notas</b>: por um teclado/controlador USB MIDI ligado no computador (o synth aparece '
              'como dispositivo MIDI "RP2040 Synth"), ou sem nada externo: abra a aba <b>SEQ</b> e toque <b>PLAY</b> '
              '(ou dê um clique no encoder).'],
        ['4', '<b>Escolha um som</b> na aba <b>PRE</b>: toque num dos 8 slots de minimal / deep tech '
              '(ROLLER, DUB CHORD, HYPNO PLCK, RUMBLE, BLIP, DEEP PAD, ACID TOOL, METAL PERC). Cada um já carrega '
              'um groove no sequenciador: toque <b>PLAY</b> na aba SEQ para ouvir.'],
        ['5', '<b>Gire o encoder</b> enquanto toca: ele abre e fecha o filtro em qualquer tela.'],
    ]
    story.append(legend([s[1] for s in steps]))
    story.append(Spacer(1, 6))
    story.append(shot_block('splash', [(160, 100), (160, 150)], [
        'Abertura: números caindo no estilo Matrix e o nome sendo "digitado".',
        'Crédito do projeto. Dura uns 3 s; o áudio já está pronto durante a abertura.'],
        title='Tela de abertura', note='Para calibrar o touch na partida, mantenha o dedo na tela até o fim da abertura.'))
    story.append(Spacer(1, 6))
    story.append(tip('As notas MIDI aceitas vão de <b>24 a 108</b> (C1 a C8), em qualquer canal. '
                     'Mensagens fora dessa faixa aparecem na barra de status mas não tocam.'))

    story.append(Paragraph('Como a tela é organizada', st_h2))
    story.append(shot_block('boot_env', [(160, 9), (22, 30), (160, 120), (70, 226), (240, 226), (150, 236), (305, 236, 236)], [
        '<b>Abas</b>: OSC, ENV, FLT, MOD, SEQ, FX e PRE. Toque para abrir.',
        '<b>Pontinhos</b> sob a aba ativa: quantas páginas ela tem e em qual você está.',
        '<b>Área da página</b>: gráficos, botões e sliders.',
        '<b>Valor do encoder</b>: "Cutoff (knob)" ou "Reso (knob)" quando o botão está apertado.',
        '<b>Monitor MIDI</b>: última mensagem recebida (canal, nota, velocity), contador e uso de CPU.',
        '<b>Medidor de saída</b>: nível do áudio de -48 a 0 dB.',
        '<b>CLIP</b>: acende 1 s quando a saída passa do limite. Baixe o VOLUME, o DRIVE ou os MIX.',
    ], note='Para trocar de página dentro de uma aba, <b>toque de novo na aba que já está aberta</b>: '
            'abre o menu com o nome de cada página. Também dá para fazer <b>swipe</b>.'))

    story.append(shot_block('menu_osc', [(25, 15), (90, 50), (90, 190)], [
        'Toque na aba que já está aberta (aqui, OSC).',
        'O menu lista as páginas daquela aba. A atual fica destacada.',
        'Toque num nome para ir direto. Tocar fora do menu só fecha.'],
        title='Menu de páginas',
        note='Funciona em todas as abas com mais de uma página, inclusive FX (DELAY, REVERB, CHORUS, CRUSH).'))

    story.append(KeepTogether([Paragraph('Gestos de toque', st_h2), img(gesture_figure(), W)]))
    story.append(Spacer(1, 4))
    story.append(table([
        ['Gesto', 'O que faz'],
        ['Tocar', 'Aciona botões, escolhe células, formas de onda e slots. Arrastar por cima de botões não aciona nada.'],
        ['Arrastar slider', 'O slider "gruda" no dedo: dá para arrastar até fora da barra sem soltar o controle.'],
        ['Afastar o dedo (FINE)', 'Durante o arraste, afaste o dedo da barra na vertical: aparece <b>FINE 1/2, 1/4...</b> '
                                  'e o movimento fica mais preciso quanto mais longe.'],
        ['Toque duplo no slider', 'Volta o parâmetro ao valor padrão de fábrica.'],
        ['Tocar na aba aberta', 'Abre o menu com os nomes das páginas da aba.'],
        ['Swipe horizontal', 'Arraste rápido para a esquerda (próxima página) ou para a direita (anterior), '
                             'começando fora de sliders e gráficos. Na aba FX troca DLY/REV/CHR/BIT.'],
        ['Botões "TAP 2X"', 'SAVE, FACTORY e CLR pedem confirmação: o botão vira <b>SURE?</b> por 2 s; toque de novo.'],
    ], [42 * mm, W - 42 * mm]))

    story.append(Paragraph('Encoder (knob)', st_h2))
    story.append(img(encoder_figure(), W))
    story.append(Spacer(1, 2))
    story.append(Paragraph('O encoder funciona em qualquer aba e tem aceleração: devagar ele muda 0,5% por clique, '
                           'rápido muda até 2,5%. O pânico (segurar parado por 0,8 s) solta todas as notas e para o '
                           'sequenciador. Use se alguma nota ficar presa. Um aperto que acontece <b>enquanto o eixo gira</b> é ignorado '
                           '(é ruído do contato do botão), então girar nunca dá PLAY/STOP.', st_body))
    story.append(CondPageBreak(110 * mm))

    # ----- 2. OSC
    story += section('2 · ABA OSC', 'Osciladores', 'A aba OSC tem <b>6 páginas</b>: WAVE, PULSE / SUB, FM, OSC 2, UNISON e WAVETABLE. Toque de novo em OSC '
                     'para escolher no menu.')
    story.append(shot_block('osc1', [(300, 62), (300, 102), (300, 145), (200, 200)], [
        '<b>Formas de onda</b>: SAW (serra), SQR (quadrada), TRI, SIN, PLS (pulso), SSW (supersaw), NSE (ruído) e '
        '<b>WT</b> (wavetable, ver página 6).',
        'A forma escolhida fica destacada.',
        '<b>Osciloscópio</b>: mostra a forma de onda real que está saindo.',
        '<b>VOLUME</b> geral.'], title='Página 1 · Forma de onda'))
    story.append(shot_block('osc2', [(160, 52), (160, 88), (160, 140), (160, 172), (160, 205)], [
        '<b>SUB SQR / SUB SIN</b>: forma do sub-oscilador (uma oitava abaixo).',
        'Gráfico da largura de pulso e do sub.',
        '<b>PULSE WIDTH</b>: largura do pulso (ondas SQR e PLS).',
        '<b>PWM LFO AMOUNT</b>: quanto o LFO mexe na largura (som de "coro").',
        '<b>SUB LEVEL</b>: volume do sub-oscilador.'], title='Página 2 · Pulso e sub'))
    story.append(shot_block('osc3', [(60, 48), (160, 85), (160, 117), (160, 150), (160, 182), (160, 213)], [
        '<b>SNAP 0.5</b>: liga o "ímã" do RATIO em passos de 0,5 (intervalos harmônicos).',
        '<b>FM AMOUNT</b>: profundidade da FM (0 = desligada).',
        '<b>FM RATIO</b>: relação de frequência do modulador (x0,5 a x12).',
        '<b>FM DECAY</b>: quanto tempo o brilho da FM dura depois do ataque.',
        '<b>FM ENV FOLLOW</b>: quanto a FM segue o envelope.',
        '<b>FM FEEDBACK</b>: realimentação do modulador (mais áspero).'], title='Página 3 · FM',
        note='Ratios inteiros (x1, x2, x3...) soam afinados; quebrados (x1,4) soam metálicos, bons para sinos.'))
    story.append(shot_block('osc4', [(160, 50), (80, 78), (240, 78), (160, 110), (160, 142), (160, 172)], [
        '<b>Forma do OSC2</b>: SAW, SQR, TRI ou SIN.',
        '<b>HARD SYNC</b>: o OSC2 reinicia junto com o OSC1. Varie os SEMITONES para o timbre "rasgado".',
        '<b>RING MOD</b>: multiplica OSC1 x OSC2. Timbres metálicos e de sino.',
        '<b>OSC2 LEVEL</b>: volume do segundo oscilador (0 = desligado).',
        '<b>OSC2 SEMITONES</b>: +12 = oitava acima, +7 = quinta.',
        '<b>OSC2 FINE TUNE</b>: desafinação fina (som mais encorpado).'], title='Página 4 · OSC2'))
    story.append(shot_block('osc5', [(160, 52), (160, 90), (160, 140)], [
        '<b>UNISON</b>: OFF, x2, x3 ou x5 cópias desafinadas de cada voz.',
        '<b>UNISON SPREAD</b>: quanto as cópias se afastam.',
        'Funciona em SAW/SQR/PLS/TRI/SIN. Mais cópias = mais CPU: confira o medidor em MOD p3 (VOICE/GLIDE).'],
        title='Página 5 · Unison'))
    story.append(shot_block('osc6', [(160, 52), (90, 105), (240, 105), (160, 163), (160, 195)], [
        '<b>Bancos</b>: ANALOG, VOCAL, ORGAN e SYNC. Tocar num banco já seleciona a onda WT.',
        'Os 8 quadros do banco em perspectiva.',
        'O quadro que está tocando agora (com o morph).',
        '<b>WT POSITION</b>: posição na tabela (varre os 8 quadros suavemente).',
        '<b>POSITION LFO AMOUNT</b>: o LFO varre a tabela sozinho.'], title='Página 6 · Wavetable',
        note='A posição também pode ser modulada pela linha <b>PWM/WT</b> da matriz (MOD p2).'))
    story.append(CondPageBreak(110 * mm))

    # ----- 3. ENV
    story += section('3 · ABA ENV', 'Envelopes',
                     'Os três envelopes ficam juntos: <b>AMP ENV</b> (volume), <b>FILTER ENV</b> (filtro) e '
                     '<b>PITCH ENV</b> (afinação).')
    story.append(shot_block('env', [(16, 62), (22, 75), (80, 102), (160, 137), (160, 163)], [
        '<b>Ataque</b>: arraste o ponto para a direita para a nota entrar devagar.',
        '<b>Decay / Sustain</b>: horizontal = decay, vertical = nível de sustain.',
        '<b>Release</b>: quanto a nota demora a sumir depois de soltar a tecla.',
        'Ataque e decay em segundos.',
        'Sustain em % e release em segundos.'],
        title='Página 1 · AMP ENV', note='Arraste diretamente os círculos do gráfico.'))
    story.append(shot_block('env2', [(160, 64), (160, 110), (160, 142), (160, 174), (160, 205)], [
        'Forma do envelope do filtro: sobe no ataque e cai no decay. A altura é o AMOUNT.',
        '<b>FILTER ENV AMOUNT</b>: quanto o envelope abre o cutoff a cada nota.',
        '<b>FILTER ENV ATTACK</b>: tempo de subida.',
        '<b>FILTER ENV DECAY</b>: tempo de queda (curto = "plick", longo = "uau").',
        '<b>VELOCITY &gt; FILTER</b>: notas mais fortes abrem mais o filtro.'], title='Página 2 · FILTER ENV'))
    story.append(shot_block('env3', [(160, 75), (160, 132), (160, 164), (160, 197)], [
        'Curva do envelope de pitch.',
        '<b>PITCH ENV AMOUNT</b>: em quantos semitons a nota começa acima antes de voltar ao tom.',
        '<b>PITCH ENV DECAY</b>: duração dessa queda.',
        'Dica de uso: kicks, zaps e toms com decay curto.'], title='Página 3 · PITCH ENV'))
    story.append(CondPageBreak(110 * mm))

    # ----- 4. FLT
    story += section('4 · ABA FLT', 'Filtro')
    story.append(shot_block('flt1', [(160, 60), (229, 82), (160, 140), (160, 172), (160, 203)], [
        'Curva de resposta do filtro (tipo e inclinação no título).',
        '<b>Arraste o ponto</b>: horizontal = cutoff, vertical = ressonância.',
        '<b>CUTOFF FREQ</b> (o mesmo do encoder).',
        '<b>RESSONÂNCIA</b>: realça a frequência de corte (até auto-oscilar).',
        '<b>DRIVE</b>: saturação antes do filtro.'], title='Página 1 · FILTER'))
    story.append(shot_block('flt2', [(160, 50), (160, 80), (160, 114), (160, 160)], [
        '<b>LP / BP / HP</b>: passa-baixa, passa-banda, passa-alta.',
        '<b>12 dB / 24 dB</b>: inclinação (24 dB é mais fechado, estilo Moog).',
        '<b>WAVEFOLDER</b>: dobra a onda (timbre "West Coast"). Combine com SIN ou TRI.',
        'Lembretes rápidos de cada opção.'], title='Página 2 · MODE / FOLD',
        note='O envelope do filtro fica na aba ENV, página FILTER ENV.'))
    story.append(shot_block('flt_fine', [(95, 127), (160, 140)], [
        'O rótulo <b>FINE 1/4</b> aparece enquanto o dedo está longe da barra.',
        'O valor anda 4x mais devagar que o dedo: bom para acertar o cutoff no ponto exato.'],
        title='Ajuste FINE em ação'))
    story.append(CondPageBreak(110 * mm))

    # ----- 5. MOD
    story += section('5 · ABA MOD', 'Modulação', 'A aba MOD tem <b>3 páginas</b>: LFO, MATRIX e VOICE/GLIDE.')
    story.append(shot_block('mod1', [(130, 50), (285, 50), (160, 92), (160, 140), (160, 172), (160, 203)], [
        '<b>Formas do LFO</b>: SIN, TRI, SAW, SQR e S&amp;H (aleatório em degraus).',
        '<b>SYNC</b>: o LFO segue o BPM do sequenciador e reinicia no PLAY.',
        'Desenho da forma escolhida e da velocidade.',
        '<b>LFO RATE</b>: em Hz; com SYNC mostra a divisão (2 compassos a 1/32, com tercinas).',
        '<b>LFO DEPTH</b>: quanto o LFO mexe no cutoff.',
        '<b>DETUNE</b>: desafinação da supersaw.'], title='Página 1 · LFO'))
    story.append(shot_block('mod4b', [(95, 62), (220, 110), (160, 182), (160, 205)], [
        'Célula selecionada (LFO &gt; CUTOFF = +60).',
        '<b>Grade</b>: linhas = destino (CUTOFF, PITCH, PWM/WT, VOLUME); colunas = fonte (LFO, F.ENV, A.ENV, VEL).',
        '<b>Barra bipolar</b>: arraste para a direita (+) ou esquerda (-).',
        'Toque de novo na célula já selecionada para zerá-la.'], title='Página 2 · MATRIX',
        note='Exemplo da imagem: o LFO abre o filtro (+60) e o envelope de amplitude baixa um pouco o pitch (-35).'))
    story.append(shot_block('mod3', [(160, 52), (160, 95), (160, 130), (160, 163), (160, 198)], [
        '<b>POLY</b>: até 4 notas. <b>MONO</b>: 1 nota, envelope reinicia. '
        '<b>LEGATO</b>: notas ligadas deslizam sem reiniciar.',
        '<b>GLIDE TIME</b>: tempo do portamento (também vale para o SLIDE do sequenciador).',
        'Resumo dos modos.',
        '<b>Teste do encoder</b>: CLK e DT mudam ao girar e STEPS conta; SW vai a 0 só quando você aperta. '
        'NOISE conta apertos ignorados por terem vindo durante o giro.',
        '<b>CPU LOAD</b>: uso do processador de áudio. Acima de ~85% reduza unison ou efeitos.'],
        title='Página 3 · VOICE/GLIDE'))
    story.append(CondPageBreak(110 * mm))

    # ----- 5. SEQ
    story += section('6 · ABA SEQ', 'Sequenciador de 16 passos',
                     'Tem <b>4 padrões</b> (A a D), cada um com 16 passos. Cada passo guarda nota, oitava, accent, '
                     'slide, gate, probabilidade e ratchet.')
    story.append(shot_block('seq1', [(54, 39), (162, 39), (229, 39), (276, 39), (313, 39), (39, 70),
                                     (311, 144), (62, 166), (70, 196), (219, 196), (313, 196)], inline=True, items=[
        '<b>PLAY / STOP</b> (também pelo clique do encoder).',
        '<b>BPM</b>: - e + (segure para correr). Bloqueado em MIDI SYNC.',
        '<b>Padrão</b> em edição: toque para A, B, C, D.',
        '<b>RND</b>: gera um padrão aleatório.',
        '<b>CLR</b>: apaga o padrão (toque 2x). Depois de RND/CLR vira <b>UNDO</b> por 5 s.',
        '<b>Passos</b>: toque para editar e ouvir. "A" = accent, "S" = slide, "--" = pausa.',
        '<b>Teclado</b>: define a nota do passo selecionado (C a C da oitava seguinte).',
        '<b>OCT- / OCT+, ACCENT, SLIDE, GATE</b> do passo. GATE desligado = pausa.',
        '<b>PROB</b> (100/75/50/25%) e <b>RATCH</b> (x1 a x4 repetições dentro do passo).',
        '<b>REC</b>: grava notas do teclado MIDI no passo (veja Live Rec).',
        '<b>LEARN</b>: ensina os pads de um controlador (veja MIDI).'], title='Página 1 · Edição de passos'))
    story.append(shot_block('seq1_undo', [(148, 96), (313, 39)], inline=True, items=[
        'Durante o PLAY, o passo que está tocando fica <b>invertido</b> (preto).',
        'Depois de um RND, o botão CLR vira <b>UNDO</b>: toque em até 5 s para voltar ao padrão de antes. '
        'Vários RND seguidos voltam ao padrão original.'], title='Tocando + UNDO'))
    story.append(shot_block('seq2', [(160, 52), (160, 92), (160, 122), (160, 155), (160, 183)], [
        '<b>Direção</b>: FWD (frente), BWD (trás), PING (vai e volta), RND (aleatório).',
        '<b>SWING</b>: atrasa os passos pares (groove).',
        '<b>LIVE REC</b>: com o SEQ tocando, notas do teclado MIDI são gravadas no passo atual '
        '(velocity &gt;= 100 grava accent). Parado, grava no passo selecionado.',
        '<b>INT CLOCK / MIDI SYNC</b>: tempo interno ou clock MIDI externo (Start/Stop/Continue da DAW).',
        '<b>ACCENT SWEEP (303)</b>: cada accent dá um "chute" no filtro; accents seguidos se somam.'],
        title='Página 2 · Tocar'))
    story.append(shot_block('seq3', [(160, 52), (80, 85), (240, 85), (160, 122), (160, 152)], [
        '<b>A B C D</b>: padrão para editar e tocar (a troca acontece no fim do compasso).',
        '<b>CHAIN</b>: liga a sequência de padrões abaixo, um compasso cada.',
        '<b>COPY A&gt;B</b>: copia o padrão atual no próximo (toque 2x; depois vira UNDO).',
        '<b>Ordem do chain</b>: toque numa célula para trocar a letra.',
        '<b>LENGTH</b>: quantos padrões o chain usa (1 a 8).'], title='Página 3 · Padrões e chain'))
    story.append(CondPageBreak(110 * mm))

    # ----- 6. FX
    story += section('7 · ABA FX', 'Efeitos',
                     'Quatro efeitos em série: <b>BIT → CHORUS → DELAY → REVERB</b>. Escolha qual editar nos '
                     'botões do topo ou com swipe. Delay e reverb não gastam CPU com o MIX em 0.')
    fx = [('fx1', 'DLY · Delay ping-pong', ['<b>DLY REV CHR BIT</b>: qual efeito editar.',
                                             'Ecos: cheios = esquerda, contorno = direita.',
                                             '<b>DELAY TIME</b> em ms.', '<b>FEEDBACK</b>: quantas repetições.',
                                             '<b>DELAY MIX</b>: volume dos ecos.']),
          ('fx2', 'REV · Reverb', ['Efeito ativo.', 'Cauda estimada (RT60).', '<b>ROOM SIZE</b>: tamanho da sala.',
                                    '<b>DAMPING</b>: abafa os agudos da cauda.', '<b>REVERB MIX</b>.']),
          ('fx3', 'CHR · Chorus', ['Efeito ativo.', 'Movimento das duas cópias.', '<b>CHORUS RATE</b>.',
                                   '<b>CHORUS DEPTH</b>.', '<b>CHORUS MIX</b>: também abre o estéreo.']),
          ('fx4', 'BIT · Bitcrusher', ['Efeito ativo.', 'Onda já "esmagada".', '<b>BIT DEPTH</b>: 16 = limpo, 4 = sujo.',
                                       '<b>SAMPLE RATE DIVIDER</b>: 1/1 = limpo; maior = aliasing.', '<b>CRUSH MIX</b>.'])]
    rows = []
    for name, title, items in fx:
        block = [Paragraph(title, st_h3),
                 img(screen(name, [(160, 52), (160, 90), (160, 140), (160, 172), (160, 203)]), 80 * mm),
                 Spacer(1, 2), legend(items)]
        rows.append(block)
    t = Table([[rows[0], rows[1]], [rows[2], rows[3]]], colWidths=[W / 2, W / 2])
    t.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 0),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 8), ('BOTTOMPADDING', (0, 0), (-1, -1), 10)]))
    story.append(t)
    story.append(CondPageBreak(110 * mm))

    # ----- 7. PRE
    story += section('8 · ABA PRE', 'Presets, nomes e MIDI map')
    story.append(shot_block('pre1', [(82, 46), (74, 136), (150, 136), (226, 136), (312, 136), (150, 168), (306, 168), (200, 204)], inline=True, items=[
        '<b>8 slots</b>: toque para carregar. O slot atual fica preto.',
        '<b>SAVE</b>: grava o som atual no slot atual (toque 2x).',
        '<b>NAME</b>: abre o teclado para renomear o slot.',
        '<b>FACTORY</b>: volta o slot ao som de fábrica (toque 2x).',
        '<b>PTN ON/OFF</b>: se o preset também carrega o padrão do sequenciador.',
        '<b>RANDOM</b>: sorteia um som novo (veja abaixo).',
        '<b>UNDO / REDO</b>: volta ao som de antes do RANDOM; tocar de novo refaz. Riscado = nada para desfazer.',
        'Mensagens (carregado, gravado, tipo do som sorteado).'], title='Página 1 · Presets',
        note='Tudo é gravado: sliders, formas de onda, wavetable, FM, OSC2, matriz, LFO, accent, efeitos e o padrão.'))
    story.append(shot_block('pre_rnd', [(150, 168), (176, 208)], inline=True, items=[
        'Cada toque sorteia primeiro o <b>tipo</b> (BASS, LEAD, PAD, PLUCK ou KEYS) e depois forma de onda, '
        'envelopes, filtro, LFO, OSC2, FM, unison e efeitos dentro de faixas que funcionam para aquele tipo.',
        'O tipo sorteado aparece aqui. Volume, modo de voz e sequência não mudam.'], title='RANDOM inteligente',
        note='Gostou? Toque SAVE (2x) para guardar no slot atual. Carregar outro slot apaga o UNDO.'))
    story.append(shot_block('pre_save', [(74, 136), (226, 208)], inline=True, items=[
        'Primeiro toque: o botão vira <b>SURE?</b> por 2 s.',
        'Toque de novo para confirmar. Se esperar, nada é gravado.'], title='Gravando (confirmação)'))
    story.append(shot_block('pre_name', [(160, 48), (160, 130), (270, 190)], [
        'Nome (até 10 caracteres).',
        'Letras e números.',
        '<b>SPC</b> espaço, <b>DEL</b> apaga, <b>OK</b> grava, <b>ESC</b> cancela.'], title='Renomear slot'))
    story.append(shot_block('pre2', [(160, 90), (60, 190), (160, 190), (260, 190), (160, 207)], [
        '<b>16 parâmetros</b> com o CC MIDI que controla cada um.',
        '<b>DEFAULT MAP</b>: volta ao mapa de fábrica.',
        '<b>CLEAR ALL</b>: apaga todos os CCs.',
        '<b>TOUCH CAL</b>: calibração do touch (seção 10).',
        'Para mapear: toque no parâmetro e mexa o knob/slider do controlador.'], title='Página 2 · MIDI map'))
    story.append(CondPageBreak(110 * mm))

    # ----- 8. MIDI
    story += section('9 · MIDI', 'Usando com teclado, controlador e DAW')
    story.append(table([
        ['Mensagem', 'O que faz'],
        ['Note On/Off (24-108)', 'Toca notas em qualquer canal. Velocity afeta volume (e filtro, se VELOCITY &gt; FILTER).'],
        ['Control Change', 'Controla os parâmetros mapeados na PRE p2. Padrão: CC48 cutoff, 49 ressonância, '
                           '50 attack, 51 decay, 52 sustain, 53 release, 54 forma de onda, 55 volume.'],
        ['CC 120 / CC 123', 'All Sound Off / All Notes Off: solta tudo (igual ao pânico do encoder).'],
        ['Program Change 0-7', 'Carrega o preset 1 a 8.'],
        ['Clock, Start, Stop, Continue', 'Com <b>MIDI SYNC</b> (SEQ p2), o sequenciador segue a DAW (24 ppq, passo de 1/16).'],
    ], [48 * mm, W - 48 * mm]))
    story.append(Spacer(1, 8))
    story.append(Paragraph('Pads de um controlador como passos (LEARN)', st_h3))
    story.append(Paragraph('Com um controlador de pads (por exemplo um APC), você pode ligar e desligar os passos '
                           'pelos pads:', st_body))
    story.append(legend(['Na SEQ p1, toque <b>LEARN</b>. O botão mostra qual pad ele espera.',
                         'Bata nos pads na ordem: os 16 primeiros viram os passos 1 a 16; o <b>17º</b> vira PLAY/STOP.',
                         'Depois do 17º o LEARN fecha e o mapa é salvo. Toque LEARN de novo no meio para cancelar.',
                         'Bater num pad mapeado liga/desliga o gate daquele passo (as notas desses pads não tocam o synth).']))
    story.append(Spacer(1, 8))
    story.append(Paragraph('Gravando no sequenciador pelo teclado', st_h3))
    story.append(Paragraph('Ligue <b>REC</b> (SEQ p1) ou <b>LIVE REC</b> (SEQ p2). Com o SEQ <b>parado</b>, cada nota vai '
                           'para o passo selecionado. Com ele <b>tocando</b>, a nota cai no passo mais próximo do '
                           'momento em que você tocou. Toques fortes (velocity 100 ou mais) gravam accent.', st_body))

    # ----- 9. calibração
    story.append(Spacer(1, 6))
    story += section('10 · MANUTENÇÃO', 'Calibrar o touch')
    story.append(shot_block('cal', [(24, 24), (160, 75), (160, 96)], [
        'Toque bem no centro da cruz (usa ponta de caneta ou unha para mais precisão).',
        'Aparecem 3 cruzes: dois cantos e uma de conferência no centro.',
        'Se a conferência errar por mais de 12 px, o processo recomeça. No fim aparece CALIBRATION SAVED.'],
        title='Tela de calibração',
        note='Abra por <b>PRE p2 &gt; TOUCH CAL</b>. Se o toque estiver tão errado que não dá para chegar lá, '
             '<b>ligue o synth segurando o dedo na tela</b> por 1 s e solte quando aparecer RELEASE TO CALIBRATE. '
             'O áudio continua tocando durante a calibração.'))
    story.append(CondPageBreak(110 * mm))

    # ----- 10. presets de fábrica
    story += section('11 · PRESETS', 'Presets de fábrica: minimal / deep tech',
                     'Os 8 presets vêm com um groove em <b>Sol menor</b> no padrão A do sequenciador (com PTN ON), '
                     'BPM entre 120 e 126 e um pouco de swing. Gire o encoder durante o PLAY para abrir e fechar o filtro.')
    story.append(table([
        ['Slot', 'Som', 'O que é'],
        ['1 · ROLLER', 'Baixo rolado', 'Serra com sub senoidal, filtro 24 dB fechado, MONO com glide curto. Groove de '
                                        'semicolcheias que "pula" o tempo forte, para encaixar entre os kicks.'],
        ['2 · DUB CHORD', 'Stab de dub techno', 'Serra + OSC2 uma quinta acima, decay curto, delay de 3/16 com bastante '
                                                 'feedback, reverb grande e chorus. Poucas notas: o delay faz o resto.'],
        ['3 · HYPNO PLCK', 'Pluck hipnótico', 'Senoide com FM (ratio 3,5) e LFO em 1/4 no filtro, tocando um '
                                               'arpejo em Sol menor.'],
        ['4 · RUMBLE', 'Kick + rumble', 'Senoide com envelope de pitch (vira kick) e reverb escuro e longo. Quatro no '
                                         'chão; use como base ou com o kick da sua bateria.'],
        ['5 · BLIP', 'Blip minimal', 'Quadrada curta em passa-banda, um pouco de bitcrusher e delay de 1/16. Notas '
                                      'esparsas.'],
        ['6 · DEEP PAD', 'Pad escuro', 'Wavetable VOCAL varrida pelo LFO (2 compassos), OSC2 na quinta, chorus e reverb '
                                        'grande. Duas notas longas por padrão.'],
        ['7 · ACID TOOL', 'Acid profundo', '303 em 24 dB com ressonância alta, accent forte e slides (modo LEGATO).'],
        ['8 · METAL PERC', 'Percussão metálica', 'Quadrada com ring mod (OSC2 +19), passa-alta e bitcrusher leve: '
                                                  'um "hat" metálico nos contratempos, com ratchet.'],
    ], [30 * mm, 32 * mm, W - 62 * mm]))
    story.append(tip('Os grooves de fábrica tocam todas as notas sempre. Para variar a cada volta, ligue <b>PROB</b> '
                     'num passo (SEQ p1): passos com <b>%</b> na célula tocam só às vezes, de propósito.'))
    story.append(tip('Se você já gravou algo num slot, ele continua com o seu som. Para ver o preset novo, toque '
                     '<b>FACTORY</b> duas vezes nesse slot.', 'ATENÇÃO'))
    story.append(CondPageBreak(90 * mm))

    # ----- 11. receitas
    story += section('12 · RECEITAS', 'Sons rápidos para começar',
                     'Parta de qualquer preset e ajuste como abaixo.')
    recipes = [
        ['Som', 'Como fazer'],
        ['Baixo ácido (303)', 'OSC p1: SAW · MOD p3: MONO, GLIDE ~60 ms · FLT p1: CUTOFF 30%, RESSONÂNCIA 70% · '
                              'ENV p2: FILTER ENV AMOUNT 70% · SEQ p2: ACCENT SWEEP 60% · marque ACCENT e SLIDE em alguns passos. '
                              'Gire o encoder durante o PLAY.'],
        ['Pad largo', 'OSC p1: SSW · ENV: ataque e release longos · OSC p5: UNISON x3 · FX CHR: MIX 50% · FX REV: MIX 40%.'],
        ['Sino / metal', 'OSC p1: SIN · OSC p3: FM AMOUNT 50%, RATIO x3,5 com SNAP desligado, FM DECAY curto · '
                         'ou OSC p4: RING MOD com SEMITONES +7.'],
        ['Wobble', 'MOD p1: SIN + SYNC, RATE em 1/8 · LFO DEPTH 80% · FLT p2: 24 dB · RESSONÂNCIA 50%.'],
        ['Kick / zap', 'OSC p1: SIN · ENV p3: PITCH ENV AMOUNT alto, DECAY 40-80 ms · ENV p1: decay curto, sustain 0.'],
        ['Textura que evolui', 'OSC p6: banco VOCAL · POSITION LFO AMOUNT 50% · MOD p1: TRI devagar · FX DLY MIX 30%.'],
        ['West Coast', 'OSC p1: TRI · FLT p2: WAVEFOLDER 50-80% · matriz: A.ENV &gt; CUTOFF positivo.'],
    ]
    story.append(table(recipes, [36 * mm, W - 36 * mm]))
    story.append(Spacer(1, 10))

    story += section('13 · PROBLEMAS', 'Solução de problemas')
    story.append(table([
        ['Sintoma', 'O que fazer'],
        ['Erro ao compilar "Adafruit_USBD_MIDI does not name a type"', 'Arduino IDE: <b>Ferramentas &gt; USB Stack &gt; '
         'Adafruit TinyUSB</b>, com a placa <b>Raspberry Pi Pico</b> do core "Raspberry Pi Pico/RP2040/RP2350".'],
        ['Sem som', 'Confira o VOLUME (OSC p1), o OSC2/unison, se o medidor se mexe e o cabo no OUT. '
                    'Se o medidor se mexe e não sai som, o problema é o cabo/DAC.'],
        ['Som distorcido / CLIP aceso', 'Baixe VOLUME, DRIVE, WAVEFOLDER ou os MIX dos efeitos. Ressonância alta '
                                        'com 24 dB também satura.'],
        ['Nota presa', 'Segure o encoder parado por 0,8 s (pânico) ou mande CC 123.'],
        ['Toque fora do lugar', 'Calibre o touch (seção 10).'],
        ['Estalos / falhas no áudio', 'CPU alta: confira MOD p3 (VOICE/GLIDE). Reduza o UNISON ou desligue efeitos que não usa.'],
        ['Girar o encoder dá PLAY/STOP', 'Na v10 isso é filtrado. Se ainda acontecer, abra MOD &gt; VOICE/GLIDE: '
                                         'girando, só CLK e DT podem mudar. Se o SW muda junto, o fio do GP4 está num '
                                         'pino de giro (A ou B) ou o GND do encoder está solto; confira a ligação.'],
        ['O encoder gira ao contrário', 'No código, mude <font name="Mono">ENC_INVERT</font> e regrave.'],
        ['O SEQ não anda', 'Em MIDI SYNC ele espera o clock da DAW. Volte para INT CLOCK em SEQ p2.'],
    ], [52 * mm, W - 52 * mm]))
    return story


doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=20 * mm,
                      title='Tobri Synth - Manual de uso', author='Tobri Synth')
frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id='f')
doc.addPageTemplates([PageTemplate(id='cover', frames=[frame], onPage=on_cover),
                      PageTemplate(id='body', frames=[frame], onPage=on_page)])
doc.build(build())
print('ok', OUT)
