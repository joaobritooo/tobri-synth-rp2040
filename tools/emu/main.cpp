// Runs the real firmware UI code on the PC and dumps screenshots (PPM) for the manual.
#include "sketch_pp.cpp"

unsigned long emuMs = 1;
SerialStub Serial;
SPIClass SPI1;
EEPROMStub EEPROM;
USBDeviceStub USBDevice;
bool emuTouch = false;
int emuTX = 0, emuTY = 0;

TS_Point XPT2046_Touchscreen::getPoint() {
    TS_Point p;
    p.x = (int16_t)(touchCal.x0 + (long)emuTX * (touchCal.x1 - touchCal.x0) / 320);
    p.y = (int16_t)(touchCal.y0 + (long)emuTY * (touchCal.y1 - touchCal.y0) / 240);
    p.z = 1000;
    return p;
}

static void run(int ms) {
    for (int i = 0; i < ms; i++) { emuMs++; loop(); loop1(); }
}

static void touch(int x, int y) { emuTX = x; emuTY = y; emuTouch = true; }
static void release() { emuTouch = false; run(150); }
static void tap(int x, int y) { touch(x, y); run(40); release(); }
static void drag(int x0, int y0, int x1, int y1, int steps = 20) {
    touch(x0, y0); run(30);
    for (int i = 1; i <= steps; i++) { touch(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); run(10); }
}

static void shotRaw(const char *name);
static void shot(const char *name) {
    run(120);
    shotRaw(name);
}
static void shotRaw(const char *name) {
    char path[128];
    snprintf(path, sizeof path, "shots/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    uint16_t *b = tft.getBuffer();
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = b[i];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("shot %s\n", name);
}

static void page(Page p, int sub) { subPg[p] = (uint8_t)sub; switchPage(p); }

int main() {
    setup();
    setup1();
    run(200);

    // uma nota segurando para o osciloscópio e o medidor terem o que mostrar
    onNoteOn(1, 57, 100);
    run(300);

    paramLfoDepth = 0.45f; lfoShape = 1;
    paramO2Lvl = 0.5f;
    run(50);
    shot("boot_env");
    page(PAGE_OSC, 0); shot("osc1");
    page(PAGE_OSC, 1); shot("osc2");
    page(PAGE_OSC, 2); shot("osc3");
    page(PAGE_OSC, 3); shot("osc4");
    page(PAGE_OSC, 4); shot("osc5");
    page(PAGE_OSC, 5); shot("osc6");
    page(PAGE_ENV, 0); shot("env");
    {
        float fa = paramFiltAtt, fd = paramFiltDec, pa = paramPitchAmt, pd = paramPitchDecMs;
        paramFiltAtt = 0.05f; paramFiltDec = 0.6f; paramPitchAmt = 0.3f; paramPitchDecMs = 120.0f;
        page(PAGE_ENV, 1); shot("env2");
        page(PAGE_ENV, 2); shot("env3");
        paramFiltAtt = fa; paramFiltDec = fd; paramPitchAmt = pa; paramPitchDecMs = pd;
    }
    page(PAGE_FLT, 0); shot("flt1");
    page(PAGE_FLT, 1); shot("flt2");
    page(PAGE_MOD, 0); shot("mod1");
    page(PAGE_MOD, 1); shot("mod2");
    page(PAGE_MOD, 2); shot("mod3");
    onNoteOff(1, 57, 0);
    run(600);
    page(PAGE_SEQ, 0); shot("seq1");
    page(PAGE_SEQ, 1); shot("seq2");
    page(PAGE_SEQ, 2); shot("seq3");
    for (int i = 0; i < 4; i++) { fxSubPage = i; page(PAGE_FX, 0); char n[8]; snprintf(n, sizeof n, "fx%d", i + 1); shot(n); }
    page(PAGE_PRE, 0); shot("pre1");
    page(PAGE_PRE, 1); shot("pre2");

    // estados extras para o manual
    page(PAGE_SEQ, 0); seqCmd = 1; run(1100); shot("seq1_play");
    tap(250, 48); shot("seq1_undo");
    seqCmd = 2; run(100);
    mmAmt[0] = 0.6f; mmAmt[6] = -0.35f; mmSel = 0; page(PAGE_MOD, 1); shot("mod4b");
    page(PAGE_FLT, 0);
    touch(150, 133); run(60);
    for (int i = 0; i < 10; i++) { touch(150 + i * 2, 133 + i * 6); run(15); }
    shot("flt_fine");
    release();
    page(PAGE_PRE, 0); tap(40, 148); shot("pre_save");
    page(PAGE_PRE, 0); startNameEdit(); shot("pre_name");
    page(PAGE_OSC, 5); tap(25, 15); shot("menu_osc");
    tap(300, 200);
    drawCalScreen(0, nullptr); shotRaw("cal");
    return 0;
}
