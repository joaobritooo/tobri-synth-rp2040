// ============================================================================
// TOBRI SYNTH — módulo Eurorack 3U para impressão 3D (Ender 3 V2)
// Peças: painel frontal (com torres do display e bosses), prateleira traseira
// (Pico + DAC UDA1334A) e knob do encoder.
//
// Display: 3.2" IPS SPI ILI9341 (lcdwiki MSP3222/MSP3223, desenho "LCM OUTLINE"
// V1.0 de 2023-05-24), montado na horizontal como no firmware (setRotation(1)).
// DAC: UDA1334A (CJMCU-1334) usando o próprio P2 da placa, que encosta no
// verso do painel e aparece pelo furo "OUT".
//
// Coordenadas de projeto: X = largura (vista de frente), Y = altura,
// face frontal do painel em Z = PANEL_T; tudo que fica atrás do painel em Z < 0.
// PART = "all" mostra a montagem com as placas "fantasma" para conferir folgas.
// Para exportar: PART = "panel" / "shelf" / "knob", F6, Export STL.
// As peças já saem na orientação de impressão (sem suporte).
// ============================================================================

/* [Peça] */
PART = "all"; // [all, panel, shelf, knob]

/* [Eurorack] */
HP          = 20;                 // 20 HP: o display de 90,5 mm cabe com ~5 mm de cada lado
PANEL_H     = 128.5;              // 3U
PANEL_W     = HP * 5.08 - 0.3;    // 101.3 mm (folga para o vizinho)
PANEL_T     = 2.5;                // 2.0 é o padrão; 2.5 dá mais rigidez impresso
RAIL_CLEAR  = 10.5;               // faixa atrás do painel ocupada pelos trilhos (cima e baixo)
MOUNT_D     = 3.3;                // furo M3
MOUNT_SLOT  = 2.0;                // furo oblongo: folga horizontal para alinhar no rack
MOUNT_Y     = 3.0;                // centro do furo a partir da borda (padrão Doepfer)
MOUNT_X0    = 7.5;

/* [Display 3.2" IPS (MSP3223)] */
// medidas do desenho oficial (lá está na vertical); aqui o display fica na horizontal
LCD_PCB      = [90.50, 55.04];    // PCB
LCD_HOLE_SP  = [84.50, 49.04];    // centros dos 4 furos Ø3.2
LCD_STACK    = 4.75;              // face da PCB até a frente do vidro: 0,5 + 2,4 + 0,5 + 1,35
LCD_VA       = [65.40, 49.20];    // área útil do touch (TP LENS VA)
LCD_VA_SHIFT = 2.96;              // o centro da área útil fica 2,96 mm LONGE do lado dos pinos
LCD_LENS     = [77.90, 55.24];    // vidro do touch (contorno)
LCD_HEADER_RIGHT = true;          // pinos do display à direita (vista de frente).
                                  // Se a imagem sair de cabeça para baixo, use setRotation(3)
                                  // no firmware: o painel não muda.
LCD_Y        = 89;                // altura do centro da área útil no painel
WINDOW_MARGIN = 0.5;              // folga da janela em volta da área útil
LCD_CHAMFER  = 1.6;               // chanfro frontal: o dedo alcança as bordas do touch

/* [DAC UDA1334A: MEÇA A SUA PLACA] */
DAC_PCB       = [25.5, 36.0];     // largura (lado do P2) x comprimento
DAC_HOLE_INSET = 2.8;             // centro dos furos dos cantos até as bordas
DAC_HOLE_PILOT = 2.2;             // furo-guia para parafuso M2.5 (use 1.8 para M2)
DAC_JACK_X    = 12.75;            // centro do P2 medido da borda esquerda (vista de frente)
DAC_JACK_H    = 3.0;              // centro do P2 acima da face de cima da PCB
DAC_JACK_OUT  = 0.5;              // quanto o P2 passa da borda da PCB
DAC_STANDOFF  = 3.0;
JACK_HOLE     = 6.5;              // furo do plugue P2
JACK_RECESS   = [10, 1.3];        // rebaixo frontal (diâm., prof.): o plugue entra mais fundo

/* [Frente: encoder e USB] */
ENC_POS   = [PANEL_W / 2, 46];
ENC_HOLE  = 7.3;                  // EC11 com bucha M7
OUT_X     = PANEL_W / 2 - 16;     // posição do P2 do DAC (a placa se ajusta a ela)
USB_POS   = [PANEL_W / 2 + 16, 21];
USB_CUT   = [9.8, 4.0];           // receptáculo USB-C (use um extensor USB-C de painel)
USB_SCREW_SP = 0;                 // > 0: furos M3 para conector de painel com flange (ex.: 20)

/* [Prateleira traseira] */
SHELF_Y     = 11;                 // base da prateleira (acima do trilho)
SHELF_T     = 2.5;
SHELF_GAP   = 0.3;                // folga entre a prateleira e o verso do painel
SHELF_DEPTH = 44;                 // profundidade atrás do painel ~ 44 mm (+ fios)
FLANGE_H    = 11;
FLANGE_HALF = 7;
BOSS_POS    = [[14, 17.5], [PANEL_W - 14, 17.5]];
BOSS_D      = 7;
BOSS_H      = 6;

/* [Pico] */
PICO_SIZE   = [51, 21];
PICO_HOLES  = [47, 11.4];
PICO_X      = 48;                 // borda esquerda da Pico no painel
PICO_DEPTH  = 20;                 // distância do verso do painel até a Pico
STANDOFF_H  = 5;                  // solde a Pico com headers para CIMA ou fios direto

/* [Knob] */
KNOB_D      = 20;
KNOB_H      = 15;
SHAFT_D     = 6.15;               // eixo D do EC11 (6 mm, chato a 4.5 mm)
SHAFT_FLAT  = 4.6;
SHAFT_DEPTH = 11;

/* [Texto] */
FONT = "Liberation Sans:style=Bold";
ENGRAVE = 0.6;

$fn = 48;
eps = 0.01;

// ---------------------------------------------------------------------------
// posições derivadas
// ---------------------------------------------------------------------------
LCD_S     = LCD_HEADER_RIGHT ? 1 : -1;
LCD_PCB_C = [PANEL_W / 2 + LCD_S * LCD_VA_SHIFT, LCD_Y];   // centro da PCB (área útil centrada no painel)
LCD_WIN   = LCD_VA + [2, 2] * WINDOW_MARGIN;

SHELF_X0 = 2;
SHELF_W  = PANEL_W - 2 * SHELF_X0;
DAC_X    = OUT_X - DAC_JACK_X;                                    // borda esquerda do DAC
OUT_Y    = SHELF_Y + SHELF_T + DAC_STANDOFF + 1.6 + DAC_JACK_H;    // altura do P2

// ---------------------------------------------------------------------------
// utilitários
// ---------------------------------------------------------------------------
module rrect(size, r) {           // retângulo 2D com cantos arredondados, centrado
    offset(r = r) square([size[0] - 2 * r, size[1] - 2 * r], center = true);
}

module label(txt, pos, sz = 3.2) {
    translate([pos[0], pos[1], PANEL_T - ENGRAVE])
        linear_extrude(ENGRAVE + eps)
            text(txt, size = sz, font = FONT, halign = "center", valign = "center");
}

function lcd_holes() = [for (sx = [-1, 1], sy = [-1, 1])
    [LCD_PCB_C[0] + sx * LCD_HOLE_SP[0] / 2, LCD_PCB_C[1] + sy * LCD_HOLE_SP[1] / 2]];

function mount_holes() = [for (x = [MOUNT_X0, MOUNT_X0 + (HP - 3) * 5.08], y = [MOUNT_Y, PANEL_H - MOUNT_Y]) [x, y]];

// ---------------------------------------------------------------------------
// PAINEL FRONTAL
// ---------------------------------------------------------------------------
module panel() {
    difference() {
        union() {
            cube([PANEL_W, PANEL_H, PANEL_T]);
            // torres do display (Ø5.6: cabem entre o furo e o vidro do touch)
            for (p = lcd_holes()) translate([p[0], p[1], -LCD_STACK]) cylinder(d = 5.6, h = LCD_STACK + eps);
            // bosses da prateleira
            for (p = BOSS_POS) translate([p[0], p[1], -BOSS_H]) cylinder(d = BOSS_D, h = BOSS_H + eps);
            // nervuras de reforço nas laterais (o painel não flexiona ao tocar a tela)
            for (x = [3, PANEL_W - 5]) translate([x, 16, -2]) cube([2, PANEL_H - RAIL_CLEAR - 17, 2 + eps]);
        }
        // furos de fixação oblongos
        for (p = mount_holes())
            translate([p[0], p[1], -1]) linear_extrude(PANEL_T + 2)
                hull() for (dx = [-MOUNT_SLOT / 2, MOUNT_SLOT / 2]) translate([dx, 0]) circle(d = MOUNT_D);
        // janela do display com chanfro na frente
        translate([PANEL_W / 2, LCD_Y, 0]) {
            translate([0, 0, -1]) linear_extrude(PANEL_T + 2) rrect(LCD_WIN, 1);
            hull() {
                translate([0, 0, PANEL_T - LCD_CHAMFER]) linear_extrude(eps) rrect(LCD_WIN, 1);
                translate([0, 0, PANEL_T]) linear_extrude(1) rrect(LCD_WIN + [2, 2] * LCD_CHAMFER, 1 + LCD_CHAMFER);
            }
        }
        // furos-guia M3 (autoatarraxante) nas torres e nos bosses
        for (p = lcd_holes()) translate([p[0], p[1], -LCD_STACK - 1]) cylinder(d = 2.5, h = LCD_STACK + 1 + PANEL_T - 0.8);
        for (p = BOSS_POS)     translate([p[0], p[1], -BOSS_H - 1])    cylinder(d = 2.5, h = BOSS_H + 1 + PANEL_T - 0.8);
        // encoder
        translate([ENC_POS[0], ENC_POS[1], -1]) cylinder(d = ENC_HOLE, h = PANEL_T + 2);
        // saída P2 do DAC, com rebaixo na frente para o plugue entrar fundo
        translate([OUT_X, OUT_Y, -1]) cylinder(d = JACK_HOLE, h = PANEL_T + 2);
        translate([OUT_X, OUT_Y, PANEL_T - JACK_RECESS[1]]) cylinder(d = JACK_RECESS[0], h = JACK_RECESS[1] + 1);
        // USB
        translate([USB_POS[0], USB_POS[1], -1]) linear_extrude(PANEL_T + 2) rrect(USB_CUT, 1.2);
        if (USB_SCREW_SP > 0)
            for (s = [-1, 1]) translate([USB_POS[0] + s * USB_SCREW_SP / 2, USB_POS[1], -1]) cylinder(d = 3.2, h = PANEL_T + 2);
        // textos gravados
        label("TOBRI", [PANEL_W / 2, PANEL_H - 6.5], 5);
        label("OUT", [OUT_X, OUT_Y - 8.5], 2.8);
        label("USB MIDI", [USB_POS[0], USB_POS[1] - 6.5], 2.8);
        label("PUSH", [ENC_POS[0], ENC_POS[1] - 14], 2.8);
        // marcas em volta do knob
        for (a = [-135 : 27 : 135])
            translate([ENC_POS[0], ENC_POS[1], 0]) rotate([0, 0, a]) translate([0, KNOB_D / 2 + 1.8, PANEL_T - ENGRAVE])
                linear_extrude(ENGRAVE + eps) square([0.8, 2], center = true);
    }
}

// ---------------------------------------------------------------------------
// PRATELEIRA (coordenadas de impressão: placa deitada no XY; y = 0 é a borda
// que encosta no verso do painel; as flanges ficam em pé atrás dos bosses)
// ---------------------------------------------------------------------------
FL_Y   = BOSS_H - SHELF_GAP;                 // face da flange que encosta nos bosses
DAC_Y0 = DAC_JACK_OUT - SHELF_GAP + 0.2;     // borda da PCB do DAC (frente do P2 fica 0,2 mm do painel)

function dac_holes() = [for (hx = [DAC_HOLE_INSET, DAC_PCB[0] - DAC_HOLE_INSET],
                             hy = [DAC_HOLE_INSET, DAC_PCB[1] - DAC_HOLE_INSET])
    [DAC_X - SHELF_X0 + hx, DAC_Y0 + hy]];

function pico_holes() = [for (hx = [-1, 1], hy = [-1, 1])
    [PICO_X - SHELF_X0 + PICO_SIZE[0] / 2 + hx * PICO_HOLES[0] / 2,
     PICO_DEPTH - SHELF_GAP + PICO_SIZE[1] / 2 + hy * PICO_HOLES[1] / 2]];

module shelf() {
    difference() {
        union() {
            cube([SHELF_W, SHELF_DEPTH, SHELF_T]);                          // placa
            for (b = BOSS_POS) {
                bx = b[0] - SHELF_X0;
                translate([bx - FLANGE_HALF, FL_Y, 0]) cube([2 * FLANGE_HALF, SHELF_T, FLANGE_H]);   // flange
                // mão-francesa no lado de fora de cada flange
                gx = (bx < SHELF_W / 2) ? bx - FLANGE_HALF : bx + FLANGE_HALF - 2;
                translate([gx, FL_Y, 0]) rotate([90, 0, 90]) linear_extrude(2)
                    polygon([[0, 0], [10, 0], [0, FLANGE_H]]);
            }
            for (p = pico_holes()) translate([p[0], p[1], 0]) cylinder(d = 4.5, h = SHELF_T + STANDOFF_H);
            for (p = dac_holes())  translate([p[0], p[1], 0]) cylinder(d = 5.0, h = SHELF_T + DAC_STANDOFF);
        }
        // furos M3 das flanges (os parafusos entram nos bosses do painel)
        for (b = BOSS_POS)
            translate([b[0] - SHELF_X0, FL_Y - 1, b[1] - SHELF_Y]) rotate([-90, 0, 0]) cylinder(d = 3.4, h = SHELF_T + 2);
        // furos-guia da Pico (M2) e do DAC
        for (p = pico_holes()) translate([p[0], p[1], SHELF_T + 1]) cylinder(d = 1.8, h = STANDOFF_H + 1);
        for (p = dac_holes())  translate([p[0], p[1], SHELF_T + 0.6]) cylinder(d = DAC_HOLE_PILOT, h = DAC_STANDOFF + 1);
        // rasgos para abraçadeiras (chicote de fios)
        for (x = [10, SHELF_W / 2, SHELF_W - 10])
            translate([x, SHELF_DEPTH - 3.5, -1]) linear_extrude(SHELF_T + 2) square([4, 2.5], center = true);
        // alívio sob a Pico e sob o DAC
        translate([PICO_X - SHELF_X0 + PICO_SIZE[0] / 2, PICO_DEPTH - SHELF_GAP + PICO_SIZE[1] / 2, -1])
            linear_extrude(SHELF_T + 2) rrect([PICO_SIZE[0] - 26, PICO_SIZE[1] - 12], 2);
        translate([DAC_X - SHELF_X0 + DAC_PCB[0] / 2, DAC_Y0 + DAC_PCB[1] / 2, -1])
            linear_extrude(SHELF_T + 2) rrect([DAC_PCB[0] - 12, DAC_PCB[1] - 14], 2);
    }
}

// prateleira na posição de montagem (atrás do painel, presa nos bosses)
module shelf_mounted() {
    translate([SHELF_X0, SHELF_Y, -SHELF_GAP])
        multmatrix([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]]) shelf();
}

// ---------------------------------------------------------------------------
// KNOB (impresso de cabeça para baixo: topo na mesa)
// ---------------------------------------------------------------------------
module knob() {
    difference() {
        union() {
            cylinder(d1 = KNOB_D - 2, d2 = KNOB_D, h = 1.2);                  // borda do topo chanfrada
            translate([0, 0, 1.2]) cylinder(d = KNOB_D, h = KNOB_H - 1.2);
        }
        // estrias
        for (a = [0 : 360 / 30 : 359])
            rotate([0, 0, a]) translate([KNOB_D / 2 + 0.3, 0, 3]) cylinder(d = 1.6, h = KNOB_H, $fn = 12);
        // indicador no topo
        translate([0, KNOB_D / 4, -eps]) linear_extrude(0.8) square([1.4, KNOB_D / 2 - 2], center = true);
        // eixo D
        translate([0, 0, KNOB_H - SHAFT_DEPTH]) linear_extrude(SHAFT_DEPTH + 1) intersection() {
            circle(d = SHAFT_D);
            translate([0, SHAFT_FLAT - SHAFT_D]) square([SHAFT_D, SHAFT_D], center = true);
        }
        // rebaixo para a porca do encoder
        translate([0, 0, KNOB_H - 2]) cylinder(d = 12.5, h = 3);
    }
}

// ---------------------------------------------------------------------------
// FANTASMAS (só na montagem) — conferência de folgas
// ---------------------------------------------------------------------------
module ghosts() {
    // display: vidro do touch encostado no painel, PCB atrás, pinos + conectores para trás
    color("black", 0.8) translate([LCD_PCB_C[0] - LCD_LENS[0] / 2, LCD_Y - LCD_LENS[1] / 2, -LCD_STACK])
        cube([LCD_LENS[0], LCD_LENS[1], LCD_STACK]);
    color("crimson", 0.6) translate([LCD_PCB_C[0] - LCD_PCB[0] / 2, LCD_Y - LCD_PCB[1] / 2, -LCD_STACK - 1.6])
        cube([LCD_PCB[0], LCD_PCB[1], 1.6]);
    color("gold", 0.8) translate([LCD_PCB_C[0] + LCD_S * (LCD_PCB[0] / 2 - 2) - 1.3, LCD_Y - 17, -LCD_STACK - 1.6 - 8.4 - 14])
        cube([2.6, 34, 8.4 + 14]);
    // encoder EC11 + knob
    color("dimgray", 0.8) translate([ENC_POS[0] - 6, ENC_POS[1] - 6.5, -12]) cube([12, 13, 12]);
    color("white") translate([ENC_POS[0], ENC_POS[1], PANEL_T + 1 + KNOB_H]) mirror([0, 0, 1]) knob();
    // extensor USB-C de painel (corpo atrás do painel)
    color("silver", 0.8) translate([USB_POS[0] - 6, USB_POS[1] - 4, -12]) cube([12, 8, 12]);
    // DAC (PCB + P2)
    dac_z0 = -SHELF_GAP - DAC_Y0;
    dac_y  = SHELF_Y + SHELF_T + DAC_STANDOFF;
    color("purple", 0.7) translate([DAC_X, dac_y, dac_z0 - DAC_PCB[1]]) cube([DAC_PCB[0], 1.6, DAC_PCB[1]]);
    color("black", 0.9) translate([OUT_X - 3, dac_y + 1.6, dac_z0 - 12 + DAC_JACK_OUT]) cube([6, 6, 12]);
    // Pico
    color("green", 0.7) translate([PICO_X, SHELF_Y + SHELF_T + STANDOFF_H, -PICO_DEPTH - PICO_SIZE[1]])
        cube([PICO_SIZE[0], 1.0 + 3, PICO_SIZE[1]]);
    // zona dos trilhos (nada pode entrar aqui atrás do painel)
    color("orange", 0.15) for (y = [0, PANEL_H - RAIL_CLEAR]) translate([0, y, -60]) cube([PANEL_W, RAIL_CLEAR, 60]);
}

// ---------------------------------------------------------------------------
// SAÍDA
// ---------------------------------------------------------------------------
if (PART == "panel") {
    translate([PANEL_W, 0, PANEL_T]) rotate([0, 180, 0]) panel();   // face frontal na mesa
} else if (PART == "shelf") {
    shelf();
} else if (PART == "knob") {
    knob();
} else {
    color("whitesmoke") panel();
    color("lightsteelblue") shelf_mounted();
    ghosts();
}
