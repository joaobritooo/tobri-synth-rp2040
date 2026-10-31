// ============================================================================
// TOBRI SYNTH — painel Eurorack 3U (20 HP) + knob do encoder, juntos na mesa
// da Ender 3 V2 (220 x 220 mm). Arquivo independente: abra, F6, Export STL.
//
// Display: 3.2" IPS SPI ILI9341 (lcdwiki MSP3223), na horizontal.
// As duas peças já estão na orientação de impressão: frente do painel e topo
// do knob na mesa (sem suporte).
// ============================================================================

/* [Mesa] */
BED        = [220, 220];
SHOW_BED   = false;               // true = mostra a mesa só para conferir (não exporte assim)
PANEL_AT   = [22, 46];            // canto do painel na mesa
KNOB_AT    = [165, 110];          // centro do knob na mesa

/* [Eurorack] */
HP          = 20;
PANEL_H     = 128.5;              // 3U
PANEL_W     = HP * 5.08 - 0.3;    // 101.3 mm
PANEL_T     = 2.5;
MOUNT_D     = 3.3;                // furo M3
MOUNT_SLOT  = 2.0;                // furo oblongo
MOUNT_Y     = 3.0;
MOUNT_X0    = 7.5;

/* [Display 3.2" IPS (MSP3223)] */
LCD_HOLE_SP  = [84.50, 49.04];    // centros dos 4 furos Ø3.2
LCD_STACK    = 4.75;              // face da PCB até a frente do vidro do touch
LCD_VA       = [65.40, 49.20];    // área útil do touch
LCD_VA_SHIFT = 2.96;              // área útil fica 2,96 mm longe do lado dos pinos
LCD_HEADER_RIGHT = true;          // pinos do display à direita (vista de frente)
LCD_Y        = 89;                // altura do centro da janela
WINDOW_MARGIN = 0.5;
LCD_CHAMFER  = 1.6;

/* [Furos da frente] */
ENC_POS   = [PANEL_W / 2, 46];
ENC_HOLE  = 7.3;                  // EC11 (bucha M7)
OUT_POS   = [PANEL_W / 2 - 16, 21.1];   // P2 do DAC
JACK_HOLE = 6.5;
JACK_RECESS = [10, 1.3];          // rebaixo frontal para o plugue entrar fundo
USB_POS   = [PANEL_W / 2 + 16, 21];
USB_CUT   = [9.8, 4.0];

/* [Atrás do painel] */
SHELF_BOSSES = true;              // bosses para a prateleira da Pico/DAC (desligue se não usar)
BOSS_POS  = [[14, 17.5], [PANEL_W - 14, 17.5]];
BOSS_D    = 7;
BOSS_H    = 6;
RIBS      = true;                 // nervuras laterais de reforço

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

LCD_S     = LCD_HEADER_RIGHT ? 1 : -1;
LCD_PCB_C = [PANEL_W / 2 + LCD_S * LCD_VA_SHIFT, LCD_Y];
LCD_WIN   = LCD_VA + [2, 2] * WINDOW_MARGIN;

module rrect(size, r) {
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
// PAINEL (modelado com a frente para cima; na saída é virado para a mesa)
// ---------------------------------------------------------------------------
module panel() {
    difference() {
        union() {
            cube([PANEL_W, PANEL_H, PANEL_T]);
            // torres do display: o vidro do touch encosta no verso do painel
            for (p = lcd_holes()) translate([p[0], p[1], -LCD_STACK]) cylinder(d = 5.6, h = LCD_STACK + eps);
            if (SHELF_BOSSES)
                for (p = BOSS_POS) translate([p[0], p[1], -BOSS_H]) cylinder(d = BOSS_D, h = BOSS_H + eps);
            if (RIBS)
                for (x = [3, PANEL_W - 5]) translate([x, 16, -2]) cube([2, PANEL_H - 10.5 - 17, 2 + eps]);
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
        // furos-guia M3 (autoatarraxante)
        for (p = lcd_holes()) translate([p[0], p[1], -LCD_STACK - 1]) cylinder(d = 2.5, h = LCD_STACK + 1 + PANEL_T - 0.8);
        if (SHELF_BOSSES)
            for (p = BOSS_POS) translate([p[0], p[1], -BOSS_H - 1]) cylinder(d = 2.5, h = BOSS_H + 1 + PANEL_T - 0.8);
        // encoder, P2 e USB
        translate([ENC_POS[0], ENC_POS[1], -1]) cylinder(d = ENC_HOLE, h = PANEL_T + 2);
        translate([OUT_POS[0], OUT_POS[1], -1]) cylinder(d = JACK_HOLE, h = PANEL_T + 2);
        translate([OUT_POS[0], OUT_POS[1], PANEL_T - JACK_RECESS[1]]) cylinder(d = JACK_RECESS[0], h = JACK_RECESS[1] + 1);
        translate([USB_POS[0], USB_POS[1], -1]) linear_extrude(PANEL_T + 2) rrect(USB_CUT, 1.2);
        // textos gravados
        label("TOBRI", [PANEL_W / 2, PANEL_H - 6.5], 5);
        label("OUT", [OUT_POS[0], OUT_POS[1] - 8.5], 2.8);
        label("USB MIDI", [USB_POS[0], USB_POS[1] - 6.5], 2.8);
        label("PUSH", [ENC_POS[0], ENC_POS[1] - 14], 2.8);
        // marcas em volta do knob
        for (a = [-135 : 27 : 135])
            translate([ENC_POS[0], ENC_POS[1], 0]) rotate([0, 0, a]) translate([0, KNOB_D / 2 + 1.8, PANEL_T - ENGRAVE])
                linear_extrude(ENGRAVE + eps) square([0.8, 2], center = true);
    }
}

// ---------------------------------------------------------------------------
// KNOB (topo na mesa)
// ---------------------------------------------------------------------------
module knob() {
    difference() {
        union() {
            cylinder(d1 = KNOB_D - 2, d2 = KNOB_D, h = 1.2);
            translate([0, 0, 1.2]) cylinder(d = KNOB_D, h = KNOB_H - 1.2);
        }
        for (a = [0 : 360 / 30 : 359])
            rotate([0, 0, a]) translate([KNOB_D / 2 + 0.3, 0, 3]) cylinder(d = 1.6, h = KNOB_H, $fn = 12);
        translate([0, KNOB_D / 4, -eps]) linear_extrude(0.8) square([1.4, KNOB_D / 2 - 2], center = true);
        translate([0, 0, KNOB_H - SHAFT_DEPTH]) linear_extrude(SHAFT_DEPTH + 1) intersection() {
            circle(d = SHAFT_D);
            translate([0, SHAFT_FLAT - SHAFT_D]) square([SHAFT_D, SHAFT_D], center = true);
        }
        translate([0, 0, KNOB_H - 2]) cylinder(d = 12.5, h = 3);   // rebaixo para a porca
    }
}

// ---------------------------------------------------------------------------
// MESA
// ---------------------------------------------------------------------------
translate([PANEL_AT[0] + PANEL_W, PANEL_AT[1], PANEL_T]) rotate([0, 180, 0]) panel();   // frente na mesa
translate([KNOB_AT[0], KNOB_AT[1], 0]) knob();
if (SHOW_BED) %translate([0, 0, -1]) cube([BED[0], BED[1], 1]);
