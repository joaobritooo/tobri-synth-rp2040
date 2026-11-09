/* ============================================================================
   SINTETIZADOR RP2040 — USB MIDI DEVICE + TELA TOUCH (Tobri Synth)
   Abas: OSC / ENV / FLT / MOD / SEQ / FX / PRE
   (toque de novo na aba ativa para alternar entre as páginas)
   FX: DLY / REV / CHR / BIT   |   PRE página 2: MIDI MAP (learn dos knobs)
   NOVO: FLT p2 = filtro 12/24 dB | SEQ p3 = padrões A-D + chain |
         SEQ p1 = PROB e RATCH por passo | MOD p4 = matriz de modulação 4x4
   v5 (toque): leitura filtrada com máquina de estados (pollTouch), slider
         "preso" ao dedo durante o arraste, redesenho só quando o valor muda,
         FINE (afaste o dedo da barra na vertical), calibração do touch na EEPROM
         (PRE p2 > TOUCH CAL, ou segure o dedo na tela ao ligar)
   WAVETABLE: onda "WT" (OSC p1) + OSC p6 = 4 bancos x 8 quadros com morph;
         posição modulada pelo LFO e pela linha PWM/WT da matriz (MOD p4)
   v6: LFO com 5 formas + SYNC ao BPM (MOD p1) | accent 303 (SEQ p2) | ring mod
         (OSC p4) | wavefolder (FLT p2) | swipe troca subpágina | toque duplo no
         slider = valor padrão | UNDO depois de RND/CLR/COPY | medidor de saída
   v7: toque na aba ativa abre o MENU das subpáginas (com nomes). ENV agora tem
         AMP / FILTER ENV / PITCH ENV; FLT = FILTER (cutoff, reso, drive) + MODE
         (tipo, inclinação, wavefolder); MOD = LFO / MATRIX / VOICES
   v12: encoder com giro em GP4/GP3 e botão em GP2 (como está ligado na placa)
   v11: presets de fábrica minimal / deep tech (ROLLER, DUB CHORD, HYPNO PLCK, RUMBLE,
         BLIP, DEEP PAD, ACID TOOL, METAL PERC), cada um com groove próprio em Sol menor
   v10: botão do encoder ignora "apertos" que acontecem enquanto o eixo gira (ruído do
         SW); teste do encoder ao vivo em MOD > VOICE/GLIDE
   v9: abertura com as cores do menu + transição (varredura e abas caindo)
   v8: PRE p1 > RANDOM sorteia um som com "cara" (BASS, LEAD, PAD, PLUCK, KEYS) dentro de
         faixas musicais; UNDO volta ao som anterior (e REDO refaz) | tela de abertura
         com números caindo estilo Matrix
   ENCODER: girar = CUTOFF, segurar+girar = RESSONÂNCIA, clique = PLAY/STOP do
         SEQ, segurar parado = PÂNICO (lido por interrupção, com aceleração)
   Áudio 100% em inteiros (sem float por amostra); os floats são convertidos
   pelo core da tela em publishParams().
   ============================================================================ */

#ifndef USE_TINYUSB
#error "Arduino IDE: Ferramentas > USB Stack > selecione 'Adafruit TinyUSB' (o USB MIDI precisa dele)"
#endif
#include "Adafruit_TinyUSB.h"
#include <MIDI.h>
#include <I2S.h>
#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <math.h>
#include <stdarg.h>

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ---------------- PINOS ----------------
const int I2S_BCLK_PIN = 6;
const int I2S_DATA_PIN = 9;
I2S i2s(OUTPUT);

// encoder: giro em GP4 (CLK/A) e GP3 (DT/B), botão em GP2 (igual à montagem do protótipo)
const int ENC_CLK = 4, ENC_DT = 3, ENC_SW = 2;

const int TFT_SCK = 10, TFT_MOSI = 11, TFT_CS = 13, TFT_DC = 14, TFT_RST = 15;
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI1, TFT_DC, TFT_CS, TFT_RST);

const int TOUCH_CS = 17, TOUCH_IRQ = 21, TOUCH_MISO = 12;
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

#define TS_MINX 315
#define TS_MAXX 3826
#define TS_MINY 310
#define TS_MAXY 3790

static bool core1_booting = true;

// ---------------- PALETA (Tema Limpo / Alto Contraste) ----------------
#define COL_BG      0xFFFF // Fundo Branco
#define COL_BORDER  0x0000 // Bordas e textos Pretos
#define COL_ACCENT  0x0000 // Elementos ativos Pretos
#define COL_ACCENT2 0x0000 // Linhas Pretas
#define COL_WARN    0x0000 // Avisos Pretos
#define COL_TEXT    0x0000 // Texto geral Preto
#define COL_MUTED   0x52AA // Cinza secundário
const int RADIUS = 6;

// ---------------- CANVAS (buffers anti-flicker) ----------------
const int PANEL_W = 304;
const int PANEL_H_MAX = 75;
GFXcanvas16 panelCanvas(PANEL_W, PANEL_H_MAX);
GFXcanvas16 sliderCanvas(PANEL_W, 30);

// ---------------- CONFIG DE AUDIO ----------------
const uint32_t SAMPLE_RATE   = 32000;
const int      MAX_VOICES     = 4;
const int      WAVETABLE_SIZE = 256;
const int32_t  ENV_ONE        = 1 << 24;     // envelopes em ponto fixo Q24
int16_t sineTable[WAVETABLE_SIZE];

enum Waveform  { WAVE_SAW = 0, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SINE, WAVE_PULSE, WAVE_SUPERSAW, WAVE_NOISE, WAVE_TABLE, WAVE_COUNT };
enum EnvStage  { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
enum Page      { PAGE_OSC = 0, PAGE_ENV = 1, PAGE_FLT = 2, PAGE_MOD = 3, PAGE_SEQ = 4, PAGE_FX = 5, PAGE_PRE = 6 };

struct Voice {
    bool    active = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    int32_t gainQ15 = 0;
    uint32_t phase = 0, phase2 = 0, phase3 = 0, phaseSub = 0;
    uint32_t phaseO2 = 0;              // OSC2: fase
    uint32_t uph[5] = {0, 0, 0, 0, 0}; // unison: fase de cada cópia
    uint32_t phaseFm = 0, pmOff = 0;   // FM: fase do modulador e desvio aplicado à portadora
    int32_t  fmEnv = 0, fmPrev = 0;    // FM: envelope do índice (Q24) e última saída do modulador (feedback)
    uint32_t phaseInc = 0;
    uint32_t incEff = 0;
    uint32_t inc2 = 0, inc3 = 0;
    uint32_t targetInc = 0;
    int32_t env = 0;
    EnvStage stage = ENV_IDLE;
    int32_t filtEnv = 0;
    EnvStage filtStage = ENV_IDLE;
    int32_t pitchEnv = 0;
    uint32_t age = 0;
};
Voice voices[MAX_VOICES];
uint32_t voiceCounter = 0;

Waveform currentWave  = WAVE_SAW;
float paramAttack  = 0.01f, paramDecay = 0.15f, paramSustain = 0.70f, paramRelease = 0.25f;
float paramCutoff  = 0.60f, paramReso = 0.20f, paramVolume = 0.80f;

float paramFiltAtt   = 0.01f;
float paramFiltDec   = 0.20f;
float paramFiltAmt   = 0.50f;

float paramLfoRate  = 2.0f;
float paramLfoDepth = 0.0f;
float paramDetune   = 0.50f;

float paramPwm        = 0.30f;
float paramPwmMod     = 0.00f;
float paramSubLevel   = 0.00f;
float paramPitchAmt   = 0.00f;
float paramPitchDecMs = 80.0f;
float paramVelFilt    = 0.00f;
float paramSwing      = 0.00f;
float paramDrive      = 0.00f;
float paramGlideMs    = 60.0f;

// FM (modulação de fase por 1 operador modulador, seno)
float paramFmAmt     = 0.00f;    // quantidade (índice máx. ~12,6 rad, curva quadrática)
float paramFmRatio   = 2.00f;    // razão modulador/portadora
float paramFmDecMs   = 200.0f;   // decaimento do envelope do índice
float paramFmEnv     = 0.50f;    // quanto o índice segue o envelope (0 = constante)
float paramFmFb      = 0.00f;    // feedback do modulador (deixa o timbre mais "serrilhado")
static volatile bool fmSnap = true;   // razão em passos de 0,5

// OSC2 (segundo oscilador, com hard sync) e UNISON
float paramO2Lvl  = 0.00f;     // nível (0 = desligado)
float paramO2Semi = 0.0f;      // -24..+24 semitons
float paramO2Det  = 0.0f;      // -100..+100 cents
float paramUniDet = 0.40f;     // abertura do unison
static volatile uint8_t o2Wave = 0;     // 0 saw, 1 quadrada, 2 triangular, 3 senoide
static volatile bool    o2Sync = false;
static volatile uint8_t uniSel = 0;     // 0 = off, 1 = 2 cópias, 2 = 3 cópias, 3 = 5 cópias
static volatile uint8_t subMode = 0;        // 0 = quadrada, 1 = senoide
static volatile uint8_t filtMode = 0;       // 0 = LP, 1 = BP, 2 = HP
static volatile uint8_t filtSlope = 0;      // 0 = 12 dB/oit, 1 = 24 dB/oit (2 SVF em cascata)
static volatile uint8_t voiceMode = 0;      // 0 = POLY, 1 = MONO, 2 = LEGATO
static volatile bool    monoResetReq = false;
static volatile uint8_t lastVelocity = 100;
static volatile uint8_t cpuPct = 0;

// Efeitos
float paramDelayTime = 250.0f;
float paramDelayFb   = 0.40f;
float paramDelayMix  = 0.25f;
float paramRevSize   = 0.60f;
float paramRevDamp   = 0.40f;
float paramRevMix    = 0.20f;
float paramChoRate   = 0.80f;
float paramChoDepth  = 0.50f;
float paramChoMix    = 0.00f;
float paramCrBits    = 16.0f;
float paramCrRate    = 1.0f;
float paramCrMix     = 0.00f;

// LFO: forma e sincronia com o BPM | accent 303 | ring mod | wavefolder
static volatile uint8_t lfoShape = 0;      // 0 seno, 1 triângulo, 2 dente de serra, 3 quadrada, 4 sample & hold
static volatile bool    lfoSync  = false;  // true: o LFO RATE vira divisão do BPM do sequenciador
float paramAccent = 0.40f;                 // accent 303: quanto cada passo com accent abre o filtro (acumula)
static volatile bool    o2Ring = false;    // OSC2 vira ring mod (OSC1 x OSC2)
float paramFold   = 0.00f;                 // wavefolder antes do filtro
static volatile int32_t pAccentQ15 = 13107, pFoldGainQ8 = 256;
static volatile bool    pFoldOn = false;
static volatile int32_t outPeak = 0;       // pico da saída (medidor na barra de baixo)
static volatile bool    outClip = false;

// divisões do LFO sincronizado (em notas inteiras: 1 = um compasso 4/4)
const float LFO_SYNC_WHOLE[9] = {2.0f, 1.0f, 0.5f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f, 0.03125f};
const char* LFO_SYNC_NAMES[9] = {"2 BARS", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/32"};

// Estado do filtro (ponto fixo Q8) e do LFO (fase de 32 bits)
static int32_t  svL = 0, svB = 0;
static int32_t  sv2L = 0, sv2B = 0;            // 2º estágio do filtro (modo 24 dB)
static int32_t  volTargetQ15 = 26214, volCurQ15 = 26214;
static uint32_t pitchModMul = 65536;           // multiplicador de pitch da matriz (Q16)
static uint32_t pitchModTbl[129];              // 2^((i-64)/64) em Q16 (±1 oitava)

// Matriz de modulação: índice = destino*4 + fonte
// fontes: 0 LFO, 1 FILT ENV, 2 AMP ENV, 3 VELOCITY | destinos: 0 CUTOFF, 1 PITCH, 2 PWM, 3 VOLUME
float mmAmt[16] = {0};
static volatile int32_t pMmQ15[16];
static volatile bool    pMmOn = false, pPitchModOn = false;
static uint32_t lfoPhase32 = 0;
static int32_t  lfoHold = 0;                   // valor segurado do LFO sample & hold
static int32_t  accentEnvQ15 = 0;              // carga do accent 303 (core de áudio)
int16_t lastPlayedSample = 0;

// ---- Parâmetros PUBLICADOS em ponto fixo ----
// O core da tela (publishParams) converte os floats; o core de áudio só lê inteiros.
static volatile int32_t  pCutBaseQ15 = 19661, pLfoHalfQ15 = 0, pFiltAmtQ15 = 16384, pVelFiltQ15 = 0;
static volatile int32_t  pQTargetQ14 = 12000;
static volatile uint32_t pLfoInc32 = 268435;
static volatile int32_t  pPwmBaseQ16 = 19661, pPwmModQ16 = 0;
static volatile bool     pDriveOn = false;
static volatile int32_t  pDriveGainQ8 = 256, pDriveCompQ15 = 32768;
static volatile int32_t  pSelfWetQ15 = 0;
static volatile bool     pSeedOn = false;
static volatile int32_t  pSpreadQ16 = 0;
static volatile int32_t  pMainGainQ15 = 32768, pSubGainQ15 = 0;
static volatile int32_t  pSwingQ8 = 0;
static volatile int      pGlideShift = 11;
static volatile int32_t  pPitchDecInc = 1;
static volatile bool     pPitchOn = false;
static volatile bool     pFmOn = false;
static volatile bool     pO2On = false, pO2Sync = false;
static volatile int32_t  pO2MulQ8 = 256, pO2MainQ15 = 32768, pO2GainQ15 = 0;
static volatile int      pUniN = 1;
static volatile int32_t  pUniGainQ15 = 32768;
static volatile int32_t  pUniSpreadQ16[5] = {0, 0, 0, 0, 0};
static volatile int32_t  pFmIdxQ12 = 0, pFmRatioQ8 = 512, pFmEnvQ15 = 16384, pFmFbQ12 = 0, pFmDecInc = 1;

// Osciloscópio
const int SCOPE_LEN = 512;
static int16_t scopeBuf[SCOPE_LEN];
static volatile uint16_t scopeW = 0;

// ---------------- WIDGETS / ESTRUTURAS ----------------
// fmt: 0 = %, 1 = Hz, 2 = ms, 3 = semitons, 4 = segundos em ms, 5 = bits, 6 = divisor 1/n
struct TouchSlider { int x, y, w, h; float *value; float minV, maxV; const char *label; uint8_t fmt; float last; float def; };   // def: preenchido no boot
struct Btn         { int x, y, w, h; const char *label; };
// probIdx: 0=100% 1=75% 2=50% 3=25%  |  ratchIdx: 0..3 = 1x..4x (zero = padrão)
struct SeqStep     { uint8_t semi; int8_t oct; bool gate; bool accent; bool slide; uint8_t probIdx; uint8_t ratchIdx; };
struct AdsrPts     { int xa, xd, xs, xr, yTop, ySus, yBot; };

// Preset gravado na flash (160 bytes)
struct PresetData {
    uint8_t  magic, wave, hasPattern, reserved;
    uint16_t bpm, crc;
    float attack, decay, sustain, release;
    float cutoff, reso, filtAmt, filtAtt, filtDec;
    float lfoRate, lfoDepth, detune, volume;
    float dlyTime, dlyFb, dlyMix, revSize, revDamp, revMix;
    float pwm, pwmMod, subLevel, pitchAmt, pitchDec, velFilt, swing;
    uint8_t semi[16];
    int8_t  oct[16];
    uint8_t flags[16];
};

// cópia do som para o UNDO/REDO do RANDOM (PRE p1)
struct SoundSnap { float f[48]; uint8_t b[12]; float mm[16]; uint8_t wave; };
uint8_t rndUndoState = 0;            // 0 = nada para desfazer, 1 = UNDO disponível, 2 = REDO disponível

// Preset de fábrica
struct FactoryPreset {
    const char *name; uint8_t wave;
    float a, d, s, r;
    float cut, res, fAmt, fAtt, fDec;
    float lfoR, lfoD, det, vol;
    float dT, dF, dM;
    float rS, rD, rM;
    float pwm, pwmMod, sub, pAmt, pDecMs, vel;
    float drive; uint8_t fmode; uint8_t vmode; float glide;
};

const int PRESET_COUNT = 8;
volatile int presetReq = -1;
bool presetLoadPattern = true;
int  presetCur = 0;
bool slotIsUser[PRESET_COUNT] = {false, false, false, false, false, false, false, false};
char presetMsg[40] = "Tap a slot to load";

// Aba OSC
TouchSlider sliderVolume   = {8, 176, 304, 30, &paramVolume,   0.0f, 1.0f,  "VOLUME",      0, -999.0f};
TouchSlider sliderPwm      = {8, 118, 304, 30, &paramPwm,      0.05f, 0.95f, "PULSE WIDTH", 0, -999.0f};
TouchSlider sliderPwmMod   = {8, 150, 304, 30, &paramPwmMod,   0.0f, 1.0f,  "PWM LFO AMOUNT", 0, -999.0f};
TouchSlider sliderSubLvl   = {8, 182, 304, 30, &paramSubLevel, 0.0f, 1.0f,  "SUB LEVEL",   0, -999.0f};
// Aba FLT
TouchSlider sliderCutoff   = {8, 118, 304, 30, &paramCutoff,   0.05f, 1.0f, "CUTOFF FREQ", 0, -999.0f};
TouchSlider sliderReso     = {8, 150, 304, 30, &paramReso,     0.0f, 1.0f,  "RESSONANCIA", 0, -999.0f};
TouchSlider sliderFiltAmt  = {8, 96,  304, 30, &paramFiltAmt,  0.0f, 1.0f,  "FILTER ENV AMOUNT", 0, -999.0f};
TouchSlider sliderDrive    = {8, 182, 304, 30, &paramDrive,    0.0f, 1.0f,  "DRIVE",       0, -999.0f};
TouchSlider sliderFAtt     = {8, 128, 304, 30, &paramFiltAtt,  0.001f, 1.0f, "FILTER ENV ATTACK", 4, -999.0f};
TouchSlider sliderFDec     = {8, 160, 304, 30, &paramFiltDec,  0.01f, 2.0f, "FILTER ENV DECAY",  4, -999.0f};
TouchSlider sliderFold     = {8, 100, 304, 30, &paramFold,     0.0f, 1.0f,  "WAVEFOLDER",  0, -999.0f};
// Aba MOD
TouchSlider sliderLfoRate  = {8, 118, 304, 30, &paramLfoRate,  0.1f, 20.0f, "LFO RATE",    1, -999.0f};
TouchSlider sliderLfoDepth = {8, 150, 304, 30, &paramLfoDepth, 0.0f, 1.0f,  "LFO DEPTH",   0, -999.0f};
TouchSlider sliderDetune   = {8, 182, 304, 30, &paramDetune,   0.0f, 1.0f,  "DETUNE",      0, -999.0f};
TouchSlider sliderPitchAmt = {8, 118, 304, 30, &paramPitchAmt, 0.0f, 1.0f,  "PITCH ENV AMOUNT", 3, -999.0f};
TouchSlider sliderPitchDec = {8, 150, 304, 30, &paramPitchDecMs, 5.0f, 800.0f, "PITCH ENV DECAY", 2, -999.0f};
TouchSlider sliderVelFilt  = {8, 192, 304, 28, &paramVelFilt,  0.0f, 1.0f,  "VELOCITY > FILTER", 0, -999.0f};
TouchSlider sliderGlide    = {8, 72,  304, 30, &paramGlideMs,  1.0f, 500.0f, "GLIDE TIME",  2, -999.0f};
// Aba MOD página 4 (matriz): o ponteiro/label mudam conforme a célula selecionada
char mmLabel[24] = "LFO > CUTOFF";
TouchSlider sliderMm       = {8, 162, 304, 30, &mmAmt[0], -1.0f, 1.0f, mmLabel, 7, -999.0f};
// Aba OSC página 3 (FM)
TouchSlider sliderFmAmt    = {8, 62,  304, 30, &paramFmAmt,    0.0f, 1.0f,    "FM AMOUNT",      0, -999.0f};
TouchSlider sliderFmRatio  = {8, 94,  304, 30, &paramFmRatio,  0.5f, 12.0f,   "FM RATIO",       8, -999.0f};
TouchSlider sliderFmDec    = {8, 126, 304, 30, &paramFmDecMs,  5.0f, 2000.0f, "FM DECAY",       2, -999.0f};
TouchSlider sliderFmEnv    = {8, 158, 304, 30, &paramFmEnv,    0.0f, 1.0f,    "FM ENV FOLLOW",  0, -999.0f};
TouchSlider sliderFmFb     = {8, 190, 304, 30, &paramFmFb,     0.0f, 1.0f,    "FM FEEDBACK",    0, -999.0f};
// Aba OSC páginas 4 (OSC2) e 5 (UNISON)
TouchSlider sliderO2Lvl    = {8, 92,  304, 30, &paramO2Lvl,    0.0f, 1.0f,    "OSC2 LEVEL (0 = OFF)", 0, -999.0f};
TouchSlider sliderO2Semi   = {8, 124, 304, 30, &paramO2Semi,   -24.0f, 24.0f, "OSC2 SEMITONES",        9, -999.0f};
TouchSlider sliderO2Det    = {8, 156, 304, 30, &paramO2Det,    -100.0f, 100.0f, "OSC2 FINE TUNE",      10, -999.0f};
TouchSlider sliderUniDet   = {8, 70,  304, 30, &paramUniDet,   0.0f, 1.0f,    "UNISON SPREAD",         0, -999.0f};
// Aba SEQ
TouchSlider sliderSwing    = {8, 70,  304, 30, &paramSwing,    0.0f, 1.0f,  "SWING",       0, -999.0f};
TouchSlider sliderAccent   = {8, 170, 304, 30, &paramAccent,   0.0f, 1.0f,  "ACCENT SWEEP (303)", 0, -999.0f};
// Aba FX
TouchSlider sliderDlyTime  = {8, 118, 304, 30, &paramDelayTime, 40.0f, 500.0f, "DELAY TIME", 2, -999.0f};
TouchSlider sliderDlyFb    = {8, 150, 304, 30, &paramDelayFb,   0.0f, 0.90f,   "FEEDBACK",   0, -999.0f};
TouchSlider sliderDlyMix   = {8, 182, 304, 30, &paramDelayMix,  0.0f, 1.0f,    "DELAY MIX",  0, -999.0f};
TouchSlider sliderRevSize  = {8, 118, 304, 30, &paramRevSize,   0.0f, 1.0f,    "ROOM SIZE",  0, -999.0f};
TouchSlider sliderRevDamp  = {8, 150, 304, 30, &paramRevDamp,   0.0f, 1.0f,    "DAMPING",    0, -999.0f};
TouchSlider sliderRevMix   = {8, 182, 304, 30, &paramRevMix,    0.0f, 1.0f,    "REVERB MIX", 0, -999.0f};
TouchSlider sliderChoRate  = {8, 118, 304, 30, &paramChoRate,   0.1f, 5.0f,    "CHORUS RATE", 1, -999.0f};
TouchSlider sliderChoDepth = {8, 150, 304, 30, &paramChoDepth,  0.0f, 1.0f,    "CHORUS DEPTH", 0, -999.0f};
TouchSlider sliderChoMix   = {8, 182, 304, 30, &paramChoMix,    0.0f, 1.0f,    "CHORUS MIX", 0, -999.0f};
TouchSlider sliderCrBits   = {8, 118, 304, 30, &paramCrBits,    4.0f, 16.0f,   "BIT DEPTH", 5, -999.0f};
TouchSlider sliderCrRate   = {8, 150, 304, 30, &paramCrRate,    1.0f, 24.0f,   "SAMPLE RATE DIVIDER", 6, -999.0f};
TouchSlider sliderCrMix    = {8, 182, 304, 30, &paramCrMix,     0.0f, 1.0f,    "CRUSH MIX",  0, -999.0f};

// índice da divisão do LFO sincronizado (fica depois das structs: o Arduino põe os
// protótipos antes da primeira função do arquivo)
int lfoSyncIdx() { return constrain((int)((paramLfoRate - 0.1f) / 19.9f * 8.0f + 0.5f), 0, 8); }

const uint8_t SYNTH_KEY_LOW  = 24;
const uint8_t SYNTH_KEY_HIGH = 108;
const uint8_t CC_KNOB[8] = {48, 49, 50, 51, 52, 53, 54, 55};
int knobValue[8] = {90, 25, 15, 60, 90, 60, 0, 100};

char lastMidiMsg[40] = "aguardando midi...";
static volatile uint32_t midiRxCount = 0;

// ============================================================================
// OSCILADOR / ENVELOPE / AUDIO  (tudo em inteiros)
// ============================================================================
float noteToFreq(uint8_t note) { return 440.0f * powf(2.0f, (note - 69) / 12.0f); }
uint32_t freqToPhaseInc(float freq) { return (uint32_t)(freq * (4294967296.0 / (double)SAMPLE_RATE)); }

static uint32_t noteIncTable[128];
static uint32_t pitchMulTbl[2][129];            // 2^(oitavas * i/128) em Q16 (duas tabelas, troca atômica)
static volatile uint8_t pitchTblSel = 0;
static uint16_t fTableQ14[1025];                // coeficiente f do filtro (Q14) por posição do cutoff

void initNoteTable() {
    for (int i = 0; i < 128; i++) noteIncTable[i] = freqToPhaseInc(noteToFreq((uint8_t)i));
    for (int t = 0; t < 2; t++) for (int i = 0; i < 129; i++) pitchMulTbl[t][i] = 65536u;
}

// f = 2*sin(pi*fc/fs), fc = 120 + m^2 * 8000 (limitado a 0,45*fs), f <= 1,15
void initFilterTable() {
    for (int i = 0; i <= 1024; i++) {
        float m = (float)i / 1024.0f;
        float fc = 120.0f + m * m * 8000.0f;
        if (fc > (float)SAMPLE_RATE * 0.45f) fc = (float)SAMPLE_RATE * 0.45f;
        float f = 2.0f * sinf((float)M_PI * fc / (float)SAMPLE_RATE);
        if (f > 1.15f) f = 1.15f;
        fTableQ14[i] = (uint16_t)(f * 16384.0f + 0.5f);
    }
}

void initPitchModTable() {
    for (int i = 0; i < 129; i++) pitchModTbl[i] = (uint32_t)(exp2f((float)(i - 64) / 64.0f) * 65536.0f);
}

static uint32_t rngState = 0x1234ABCDu;
static uint32_t pulseWQ32 = 1288490188u;

// ---------------- PolyBLEP em ponto fixo (anti-aliasing) ----------------
static inline __attribute__((always_inline)) int16_t sat16(int32_t v) {
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static inline __attribute__((always_inline)) uint32_t ratioQ15(uint32_t a, uint32_t b) {
    int s = __builtin_clz(b);
    uint32_t bb = b << s;
    uint32_t aa = a << s;
    return ((aa >> 16) << 15) / (bb >> 16);
}

static int32_t __not_in_flash_func(blepQ15)(uint32_t ph, uint32_t inc) {
    if (inc == 0) return 0;
    if (ph < inc) {
        int32_t x = (int32_t)ratioQ15(ph, inc);
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    uint32_t d = 0u - ph;
    if (d < inc) {
        int32_t y = (int32_t)ratioQ15(d, inc);
        int32_t u = 32768 - y;
        return (u * u) >> 15;
    }
    return 0;
}

// seno com interpolação linear (usado pelo modulador FM e pela onda senoidal)
static inline __attribute__((always_inline)) int32_t sineInterp(uint32_t ph) {
    uint32_t i = ph >> 24;
    int32_t a = sineTable[i], b = sineTable[(i + 1) & 255];
    return a + (((b - a) * (int32_t)((ph >> 16) & 255)) >> 8);
}

// ---------------- WAVETABLE (4 bancos x 8 quadros, morph entre quadros) ----------------
// Quadros gerados por síntese aditiva no boot (wtInit), todos com fase seno: assim o
// crossfade entre quadros vizinhos não cancela harmônicos. Dois níveis de mipmap:
// até 40 harmônicos (sem aliasing até ~400 Hz) e até 10 (para notas mais agudas).
const int      WT_BANKS = 4, WT_FRAMES = 8, WT_LEN = 256;
const int      WT_HARM[2] = {40, 10};
const uint32_t WT_MIP_INC = 53687091u;          // incremento de fase de 400 Hz
const char*    WT_BANK_NAMES[WT_BANKS] = {"ANALOG", "VOCAL", "ORGAN", "SYNC"};
static int16_t wtData[2][WT_BANKS][WT_FRAMES][WT_LEN];   // 32 KB na RAM
static volatile uint8_t wtBank = 0;
float paramWtPos = 0.0f;         // posição base na tabela (0..1)
float paramWtLfo = 0.0f;         // quanto o LFO varre a posição
static volatile int32_t pWtPosQ16 = 0, pWtLfoQ15 = 0;
static int32_t wtPosTarget = 0, wtPosCur = 0;   // Q16: alvo (tick de controle) e suavizado (por amostra)
static int     wtFrame = 0;                     // quadro inferior da interpolação (0..6)
static int32_t wtFracQ14 = 0;

// amplitude do harmônico h (1..40) no quadro f de cada banco
static float wtHarmAmp(int bank, int f, int h) {
    const bool odd = (h & 1) != 0;
    const float saw = 1.0f / h;
    const float sqr = odd ? saw : 0.0f;
    const float tri = odd ? (((h >> 1) & 1) ? -1.0f : 1.0f) / (float)(h * h) : 0.0f;
    switch (bank) {
        case 0: {                    // ANALOG: seno > tri > saw > quadrada > pulsos estreitos
            switch (f) {
                case 0:  return (h == 1) ? 1.0f : 0.0f;
                case 1:  return tri;
                case 2:  return 0.5f * tri + 0.5f * saw;
                case 3:  return saw;
                case 4:  return 0.5f * saw + 0.5f * sqr;
                case 5:  return sqr;
                case 6:  return sinf((float)M_PI * h * 0.25f) * saw;
                default: return sinf((float)M_PI * h * 0.10f) * saw;
            }
        }
        case 1: {                    // VOCAL: dois formantes que sobem com o quadro
            float c1 = 1.5f * powf(1.45f, (float)f), c2 = c1 * 2.7f;
            float g1 = (h - c1) / (0.25f * c1 + 1.0f), g2 = (h - c2) / (0.25f * c2 + 1.0f);
            return saw * (0.15f + 3.0f * expf(-g1 * g1) + 1.5f * expf(-g2 * g2));
        }
        case 2: {                    // ORGAN: registros de drawbar (harmônicos 1,2,3,4,5,6,8)
            static const float REG[8][7] = {
                {1, 0, 0, 0, 0, 0, 0},       {1, .8f, 0, 0, 0, 0, 0},
                {1, .8f, .6f, 0, 0, 0, 0},   {1, .8f, .6f, .8f, 0, 0, 0},
                {1, .6f, .8f, .6f, .5f, 0, 0}, {1, .8f, .8f, .8f, .6f, .6f, 0},
                {1, 1, 1, 1, .8f, .8f, .8f}, {.6f, .4f, 1, .3f, 1, .3f, 1}};
            static const int8_t SLOT[9] = {-1, 0, 1, 2, 3, 4, 5, -1, 6};
            return (h <= 8 && SLOT[h] >= 0) ? REG[f][SLOT[h]] * 0.5f : 0.0f;
        }
        default: {                   // SYNC: saw com ressonância no último harmônico (som de hard sync)
            static const int N[8] = {1, 2, 3, 5, 8, 13, 21, 40};
            if (h > N[f]) return 0.0f;
            return (h == N[f] && f > 0) ? saw * 2.5f : saw;
        }
    }
}

void wtInit() {
    int32_t amp[41];
    int32_t acc[WT_LEN];
    for (int m = 0; m < 2; m++) {
        for (int b = 0; b < WT_BANKS; b++) {
            for (int f = 0; f < WT_FRAMES; f++) {
                // amplitudes escaladas para o maior harmônico = 32767 (evita estouro na soma)
                float fa[41], amax = 1e-6f;
                for (int h = 1; h <= WT_HARM[m]; h++) { fa[h] = wtHarmAmp(b, f, h); if (fabsf(fa[h]) > amax) amax = fabsf(fa[h]); }
                for (int h = 1; h <= WT_HARM[m]; h++) amp[h] = (int32_t)(fa[h] / amax * 32767.0f);
                int32_t peak = 1;
                for (int i = 0; i < WT_LEN; i++) {
                    int32_t s = 0;
                    for (int h = 1; h <= WT_HARM[m]; h++) {
                        if (amp[h]) s += (amp[h] * sineTable[(i * h) & 255]) >> 8;
                    }
                    acc[i] = s;
                    int32_t a = (s < 0) ? -s : s;
                    if (a > peak) peak = a;
                }
                for (int i = 0; i < WT_LEN; i++)   // normaliza cada quadro para pico ~30000
                    wtData[m][b][f][i] = (int16_t)(((int64_t)acc[i] * 30000) / peak);
            }
        }
    }
}

static inline __attribute__((always_inline)) int32_t wtLerp(const int16_t *t, uint32_t ph) {
    uint32_t i = ph >> 24;
    int32_t a = t[i], b = t[(i + 1) & 255];
    return a + (((b - a) * (int32_t)((ph >> 16) & 255)) >> 8);
}

// uma cópia do oscilador principal (usada pelo unison e pelo oscSample)
static int16_t __not_in_flash_func(oscSampleAt)(uint32_t ph, uint32_t inc) {
    switch (currentWave) {
        case WAVE_TABLE: {
            const int16_t *f0 = wtData[(inc > WT_MIP_INC) ? 1 : 0][wtBank & 3][wtFrame];
            int32_t a = wtLerp(f0, ph);
            int32_t b = wtLerp(f0 + WT_LEN, ph);          // quadro seguinte (wtFrame <= 6)
            return (int16_t)(a + (((b - a) * wtFracQ14) >> 14));
        }
        case WAVE_SAW: {
            int32_t s = (int32_t)(ph >> 16) - 32768;
            return sat16(s - blepQ15(ph, inc));
        }
        case WAVE_SQUARE: {
            int32_t s = (ph & 0x80000000UL) ? 32767 : -32768;
            s += blepQ15((uint32_t)(ph + 0x80000000u), inc) - blepQ15(ph, inc);
            return sat16(s);
        }
        case WAVE_PULSE: {
            int32_t s = (ph < pulseWQ32) ? 32767 : -32768;
            s += blepQ15(ph, inc) - blepQ15((uint32_t)(ph - pulseWQ32), inc);
            return sat16(s);
        }
        case WAVE_TRIANGLE: {
            int32_t saw = (int32_t)(ph >> 16) - 32768;
            int32_t a = (saw < 0) ? -saw : saw;
            int32_t t = a * 2 - 32768;
            if (t > 32767) t = 32767;
            return (int16_t)t;
        }
        case WAVE_NOISE: {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return (int16_t)(rngState >> 16);
        }
        case WAVE_SINE:
        default: return (int16_t)sineInterp(ph);
    }
}

static int16_t __not_in_flash_func(oscSample)(Voice &v) {
    const uint32_t inc = v.incEff;
    const uint32_t pm  = v.pmOff;          // desvio de fase do modulador FM (0 = FM desligado)
    if (currentWave == WAVE_SUPERSAW) {
        const uint32_t ph = v.phase + pm;
        int32_t s1 = (int32_t)(ph >> 16) - 32768 - blepQ15(ph, inc);
        int32_t s2 = (int32_t)((v.phase2 + pm) >> 16) - 32768 - blepQ15((v.phase2 + pm), v.inc2);
        int32_t s3 = (int32_t)((v.phase3 + pm) >> 16) - 32768 - blepQ15((v.phase3 + pm), v.inc3);
        return sat16((s1 + s2 + s3) / 3);
    }
    return oscSampleAt(v.phase + pm, inc);
}

// OSC2: 0 = saw, 1 = quadrada, 2 = triangular, 3 = senoide
static int32_t __not_in_flash_func(osc2Sample)(uint32_t ph, uint32_t inc, uint8_t w) {
    switch (w) {
        case 0: {
            int32_t s = (int32_t)(ph >> 16) - 32768;
            return sat16(s - blepQ15(ph, inc));
        }
        case 1: {
            int32_t s = (ph & 0x80000000UL) ? 32767 : -32768;
            s += blepQ15((uint32_t)(ph + 0x80000000u), inc) - blepQ15(ph, inc);
            return sat16(s);
        }
        case 2: {
            int32_t saw = (int32_t)(ph >> 16) - 32768;
            int32_t a = (saw < 0) ? -saw : saw;
            int32_t t = a * 2 - 32768;
            return (t > 32767) ? 32767 : t;
        }
        default: return sineInterp(ph);
    }
}

static int32_t __not_in_flash_func(subSample)(uint32_t ph, uint32_t inc) {
    if (subMode) return sineTable[ph >> 24];
    int32_t s = (ph & 0x80000000UL) ? 32767 : -32768;
    s += blepQ15((uint32_t)(ph + 0x80000000u), inc) - blepQ15(ph, inc);
    return s;
}

// Incrementos dos envelopes (Q24 por amostra) — escritos pelo core da tela
static volatile int32_t envAttInc = 1, envDecInc = 1, envRelInc = 1, envFAttInc = 1, envFDecInc = 1;
static volatile int32_t envSusQ = 0;
static volatile int32_t volQ15 = 0;

static int32_t envIncFor(float seconds) {
    float s = (seconds < 0.001f) ? 0.001f : seconds;
    int32_t inc = (int32_t)((float)ENV_ONE / (s * (float)SAMPLE_RATE));
    return (inc < 1) ? 1 : inc;
}

void updateEnvCoefs() {
    envAttInc  = envIncFor(paramAttack);
    envDecInc  = envIncFor(paramDecay);
    envRelInc  = envIncFor(paramRelease);
    envFAttInc = envIncFor(paramFiltAtt);
    envFDecInc = envIncFor(paramFiltDec);
    float sus = (paramSustain < 0.0f) ? 0.0f : ((paramSustain > 1.0f) ? 1.0f : paramSustain);
    envSusQ = (int32_t)(sus * (float)ENV_ONE);
    float vol = (paramVolume < 0.0f) ? 0.0f : ((paramVolume > 1.0f) ? 1.0f : paramVolume);
    volQ15 = (int32_t)(vol * 32768.0f);
}

// Envelope de amplitude: 1 passo por amostra
static inline __attribute__((always_inline)) void ampEnvStep(Voice &v) {
    switch (v.stage) {
        case ENV_ATTACK:  v.env += envAttInc; if (v.env >= ENV_ONE) { v.env = ENV_ONE; v.stage = ENV_DECAY; } break;
        case ENV_DECAY:   v.env -= envDecInc; if (v.env <= envSusQ) { v.env = envSusQ; v.stage = ENV_SUSTAIN; } break;
        case ENV_SUSTAIN: v.env = envSusQ; break;
        case ENV_RELEASE: v.env -= envRelInc; if (v.env <= 0) { v.env = 0; v.stage = ENV_IDLE; v.active = false; } break;
        default: break;
    }
}

// Envelope do filtro: 32 passos de uma vez, uma vez por tick de controle (1 kHz)
static inline __attribute__((always_inline)) void filtEnvStep32(Voice &v) {
    switch (v.filtStage) {
        case ENV_ATTACK:  v.filtEnv += envFAttInc * 32; if (v.filtEnv >= ENV_ONE) { v.filtEnv = ENV_ONE; v.filtStage = ENV_DECAY; } break;
        case ENV_DECAY:   v.filtEnv -= envFDecInc * 32; if (v.filtEnv <= 0) { v.filtEnv = 0; v.filtStage = ENV_IDLE; } break;
        case ENV_RELEASE: v.filtEnv -= envFDecInc * 32; if (v.filtEnv <= 0) { v.filtEnv = 0; v.filtStage = ENV_IDLE; } break;
        default: break;
    }
}

// Escolhe a voz para uma nota nova: livre > em release (a mais baixa) > a mais antiga.
// Uma voz "roubada" continua do nível e da fase em que estava, para não dar estalo.
void __not_in_flash_func(synthNoteOn)(uint8_t note, uint8_t vel) {
    int idx = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) { idx = i; break; }
    }
    bool stolen = false;
    if (idx < 0) {
        int32_t best = 0x7FFFFFFF;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].stage == ENV_RELEASE && voices[i].env < best) { best = voices[i].env; idx = i; }
        }
        if (idx < 0) {
            uint32_t oldestAge = 0xFFFFFFFFUL;
            for (int i = 0; i < MAX_VOICES; i++) {
                if (voices[i].age < oldestAge) { oldestAge = voices[i].age; idx = i; }
            }
        }
        stolen = true;
    }
    Voice &v = voices[idx];
    bool keep = stolen && (v.env > 0);
    v.active = true; v.note = note; v.velocity = vel;
    v.gainQ15 = ((int32_t)vel * 32768) / 127;
    if (!keep) {
        v.phase = 0; v.phase2 = 0; v.phase3 = 0; v.phaseSub = 0;
        v.env = 0; v.filtEnv = 0;
    }
    v.phaseInc = noteIncTable[note & 127];
    v.incEff = v.phaseInc;
    v.inc2 = v.phaseInc; v.inc3 = v.phaseInc;
    v.targetInc = v.phaseInc;
    v.stage = ENV_ATTACK;
    v.filtStage = ENV_ATTACK;
    v.pitchEnv = keep ? 0 : ENV_ONE;
    v.fmEnv = ENV_ONE;                       // o índice FM recomeça no máximo a cada nota
    if (!keep) {
        v.phaseFm = 0; v.fmPrev = 0; v.pmOff = 0; v.phaseO2 = 0;
        for (int k = 0; k < 5; k++) {          // fases iniciais diferentes por cópia do unison
            rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
            v.uph[k] = rngState;
        }
    }
    v.age = ++voiceCounter;
    lastVelocity = vel;
}

void __not_in_flash_func(synthNoteOff)(uint8_t note) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == note && voices[i].stage != ENV_RELEASE) {
            voices[i].stage = ENV_RELEASE;
            voices[i].filtStage = ENV_RELEASE;
        }
    }
}

void __not_in_flash_func(synthAllNotesOff)() {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].stage != ENV_RELEASE) {
            voices[i].stage = ENV_RELEASE;
            voices[i].filtStage = ENV_RELEASE;
        }
    }
}

void __not_in_flash_func(synthGlide)(uint8_t oldNote, uint8_t newNote, uint8_t vel) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == oldNote && voices[i].stage != ENV_RELEASE) {
            voices[i].note = newNote;
            voices[i].velocity = vel;
            voices[i].gainQ15 = ((int32_t)vel * 32768) / 127;
            voices[i].targetInc = noteIncTable[newNote & 127];
            lastVelocity = vel;
            return;
        }
    }
    synthNoteOn(newNote, vel);
}

// ---------------- MODO DE VOZ (POLY / MONO / LEGATO) ----------------
static uint8_t monoStack[8];
static int     monoCount = 0;
static int     monoVoiceNote = -1;

static void monoRemove(uint8_t note) {
    for (int i = 0; i < monoCount; i++) {
        if (monoStack[i] == note) {
            for (int j = i; j < monoCount - 1; j++) monoStack[j] = monoStack[j + 1];
            monoCount--;
            return;
        }
    }
}

static void monoNoteOn(uint8_t note, uint8_t vel) {
    monoRemove(note);
    bool wasHeld = (monoCount > 0);
    if (monoCount < 8) {
        monoStack[monoCount++] = note;
    } else {
        for (int j = 0; j < 7; j++) monoStack[j] = monoStack[j + 1];
        monoStack[7] = note;
    }
    if (voiceMode == 2 && wasHeld && monoVoiceNote >= 0) {
        synthGlide((uint8_t)monoVoiceNote, note, vel);
    } else {
        if (monoVoiceNote >= 0) synthNoteOff((uint8_t)monoVoiceNote);
        synthNoteOn(note, vel);
    }
    monoVoiceNote = note;
}

static void monoNoteOff(uint8_t note) {
    bool wasSounding = (note == monoVoiceNote);
    monoRemove(note);
    if (monoCount == 0) {
        synthNoteOff(note);
        if (monoVoiceNote >= 0 && monoVoiceNote != note) synthNoteOff((uint8_t)monoVoiceNote);
        monoVoiceNote = -1;
        return;
    }
    if (wasSounding) {
        uint8_t back = monoStack[monoCount - 1];
        uint8_t vel = lastVelocity;
        if (voiceMode == 2) {
            synthGlide(note, back, vel);
        } else {
            synthNoteOff(note);
            synthNoteOn(back, vel);
        }
        monoVoiceNote = back;
    }
}

static void keyOn(uint8_t note, uint8_t vel) {
    if (voiceMode == 0) synthNoteOn(note, vel);
    else                monoNoteOn(note, vel);
}

static void keyOff(uint8_t note) {
    if (voiceMode == 0) synthNoteOff(note);
    else                monoNoteOff(note);
}

// ============================================================================
// SEQUENCIADOR (estilo TB-303) — clock interno ou MIDI clock externo
// ============================================================================
const int     SEQ_STEPS     = 16;
const uint8_t SEQ_BASE_NOTE = 36;  // C2

const int PATTERN_COUNT = 4;   // padrões A, B, C, D

SeqStep seqBank[PATTERN_COUNT][SEQ_STEPS] = {
  {
    {0,  0, true,  true,  false}, {0,  0, true,  false, false},
    {12, 0, true,  false, false}, {0,  0, false, false, false},
    {3,  0, true,  true,  false}, {0,  0, true,  false, false},
    {7,  0, true,  false, true }, {10, 0, true,  false, false},
    {0,  0, true,  true,  false}, {0,  0, false, false, false},
    {12, 0, true,  false, false}, {0,  0, true,  false, false},
    {5,  0, true,  true,  false}, {3,  0, true,  false, true },
    {7,  0, true,  false, false}, {0,  0, true,  false, false}
  }
};
SeqStep *seq = seqBank[0];     // padrão que está sendo EDITADO na tela

void seqInitBanks() {          // B, C e D começam como cópia do A
    for (int p = 1; p < PATTERN_COUNT; p++) memcpy(seqBank[p], seqBank[0], sizeof(seqBank[0]));
}

volatile uint8_t seqEditPat = 0;     // padrão mostrado/editado
volatile uint8_t seqCuePat  = 0;     // padrão que toca (chain desligada)
volatile uint8_t seqPlayPat = 0;     // padrão do passo que está tocando
volatile bool    seqChainOn = false;
uint8_t          seqChain[8] = {0, 1, 2, 3, 0, 1, 2, 3};
volatile uint8_t seqChainLen = 4;
volatile uint8_t seqChainPos = 0;      // posição tocando agora (para a tela)
volatile uint8_t seqChainNextPos = 0;  // posição do próximo compasso
static uint8_t   seqNextPat = 0;
static uint8_t   seqBarCount = 0;
// ratchet / gate
static uint8_t   seqRatN = 1, seqRatLeft = 0, seqStepVel = 80;
static int32_t   seqSubLen = 0, seqRatNextAt = 0, seqGateEndAt = 0;

int seqBpm = 120;
volatile uint32_t seqStepLen = 4000;
volatile uint8_t  seqCmd = 0;
volatile bool     seqRunning = false;
volatile int      seqPlayStep = -1;
volatile int      seqAuditionReq = -1;
volatile uint8_t  seqMode = 0;
volatile bool     seqRecArm = false;
volatile bool     seqSyncExt = false;
volatile bool     settingsDirty = false;

volatile int      seqEditStep = 0;
volatile uint16_t seqRedrawMask = 0;
volatile bool     seqEditorDirty = false;

uint32_t seqCounter = 0;
bool     seqGateOpen = false;
bool     seqNoteHeld = false;
int      seqHeldNote = 0;
int      seqAuditionNote = 0;
uint32_t seqAuditionCount = 0;
static int32_t seqCurStepLen = 4000, seqCurGateLen = 2400;
static int     seqNextStepVal = 0, seqDir = 1;
static bool    seqParity = false;
static bool     seqStepPending = false;
static int      extPulse = 0;
static uint32_t extSamples = 0;
static int32_t  extStepLen = 4000;

int __not_in_flash_func(seqNoteOf)(const SeqStep &st) {
    int n = (int)SEQ_BASE_NOTE + st.oct * 12 + st.semi;
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    return n;
}

// nota do passo i do padrão EDITADO (usado pela tela)
int __not_in_flash_func(seqStepNote)(int i) { return seqNoteOf(seq[i]); }

void seqUpdateTiming() {
    seqStepLen = (uint32_t)((float)SAMPLE_RATE * 60.0f / ((float)seqBpm * 4.0f));
}

void __not_in_flash_func(seqTriggerStep)(int s) {
    SeqStep &st = seqBank[seqPlayPat & 3][s];
    seqRatLeft = 0; seqRatN = 1;
    bool play = st.gate;
    if (play && st.probIdx > 0) {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        // chance 100/75/50/25% -> toca se r < 128/96/64/32
        if ((int)((rngState >> 8) & 127) >= 128 - (int)(st.probIdx & 3) * 32) play = false;
    }
    if (!play) {
        if (seqNoteHeld) { synthNoteOff(seqHeldNote); seqNoteHeld = false; }
        seqGateOpen = false;
        return;
    }
    int note = seqNoteOf(st);
    uint8_t vel = st.accent ? 127 : 80;
    if (st.accent) {                         // accent 303: cada accent carrega o "capacitor" do filtro
        accentEnvQ15 += pAccentQ15;
        if (accentEnvQ15 > 49152) accentEnvQ15 = 49152;
    }
    if (seqNoteHeld) synthGlide(seqHeldNote, note, vel);
    else             synthNoteOn(note, vel);
    seqHeldNote = note;
    seqNoteHeld = true;
    seqGateOpen = true;
    seqStepVel = vel;
    seqRatN = (uint8_t)((st.ratchIdx & 3) + 1);
    if (seqRatN > 1) {
        seqSubLen = seqCurStepLen / seqRatN;
        seqRatLeft = (uint8_t)(seqRatN - 1);
        seqRatNextAt = seqSubLen;
        seqGateEndAt = (seqSubLen * 6) / 10;
    } else {
        seqGateEndAt = seqCurGateLen;
    }
}

// dispara as repetições (ratchet) dentro do passo
static inline void __not_in_flash_func(seqRatchetTick)() {
    if (seqRatLeft > 0 && (int32_t)seqCounter >= seqRatNextAt) {
        int32_t start = seqRatNextAt;
        seqRatLeft--;
        seqRatNextAt += seqSubLen;
        if (seqNoteHeld) synthNoteOff(seqHeldNote);
        synthNoteOn(seqHeldNote, (uint8_t)((seqStepVel * 7) >> 3));
        seqNoteHeld = true;
        seqGateOpen = true;
        seqGateEndAt = start + (seqSubLen * 6) / 10;
    }
}

void __not_in_flash_func(seqGateEnd)(int s) {
    seqGateOpen = false;
    if (seqRatLeft == 0) {   // só a última repetição pode deslizar (slide) para o próximo passo
        int next = seqNextStepVal;
        if (seqBank[seqPlayPat & 3][s].slide && seqBank[seqNextPat & 3][next].gate) return;
    }
    synthNoteOff(seqHeldNote);
    seqNoteHeld = false;
}

void __not_in_flash_func(seqStopInternal)() {
    seqRunning = false;
    seqRatLeft = 0;
    if (seqNoteHeld) { synthNoteOff(seqHeldNote); seqNoteHeld = false; }
    seqGateOpen = false;
    seqPlayStep = -1;
}

static int __not_in_flash_func(seqComputeNext)(int cur) {
    switch (seqMode) {
        case 1: return (cur + 15) & 15;
        case 2: {
            int n = cur + seqDir;
            if (n > 15) { seqDir = -1; n = 14; }
            else if (n < 0) { seqDir = 1; n = 1; }
            return n;
        }
        case 3: {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return (int)((rngState >> 8) & 15);
        }
        default: return (cur + 1) & 15;
    }
}

static void __not_in_flash_func(seqPrepareStart)() {
    seqPlayStep = -1; seqCounter = 0; seqParity = false; seqDir = 1;
    if (lfoSync) lfoPhase32 = 0;          // LFO sincronizado começa junto com o sequenciador
    if (seqMode == 1)      seqNextStepVal = 15;
    else if (seqMode == 3) seqNextStepVal = (int)((rngState >> 8) & 15);
    else                   seqNextStepVal = 0;
    extPulse = 0; seqStepPending = false; extSamples = 0; extStepLen = (int32_t)seqStepLen;
    seqRatLeft = 0; seqGateEndAt = 0;
    seqBarCount = 0; seqChainPos = 0; seqChainNextPos = 0;
    seqNextPat = seqChainOn ? (uint8_t)(seqChain[0] & 3) : (uint8_t)(seqCuePat & 3);
    seqPlayPat = seqNextPat;
}

// escolhe o padrão do próximo compasso (chain ou padrão selecionado)
static uint8_t __not_in_flash_func(seqPickNextPat)() {
    if (seqChainOn) {
        uint8_t n = seqChainLen;
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        uint8_t p = (uint8_t)(seqChainNextPos + 1);
        if (p >= n) p = 0;
        seqChainNextPos = p;
        return (uint8_t)(seqChain[p] & 3);
    }
    return (uint8_t)(seqCuePat & 3);
}

static void __not_in_flash_func(seqBeginStep)() {
    seqPlayStep = seqNextStepVal;
    seqPlayPat = seqNextPat;
    if (seqBarCount == 0) seqChainPos = seqChainNextPos;
    seqTriggerStep(seqPlayStep);
    seqNextStepVal = seqComputeNext(seqPlayStep);
    if (++seqBarCount >= SEQ_STEPS) { seqBarCount = 0; seqNextPat = seqPickNextPat(); }
}

void __not_in_flash_func(seqTick)() {
    if (seqAuditionReq >= 0) {
        seqAuditionNote = seqAuditionReq;
        seqAuditionReq = -1;
        synthNoteOn((uint8_t)seqAuditionNote, 100);
        seqAuditionCount = SAMPLE_RATE / 6;
    }
    if (seqAuditionCount > 0) {
        if (--seqAuditionCount == 0) synthNoteOff((uint8_t)seqAuditionNote);
    }

    if (seqCmd) {
        uint8_t c = seqCmd; seqCmd = 0;
        if (c == 1) { seqPrepareStart(); seqRunning = true; }
        else        { seqStopInternal(); }
    }
    if (!seqRunning) return;

    if (seqSyncExt) {
        if (seqStepPending) {
            seqStepPending = false;
            if (extSamples > 400 && extSamples < 60000) {
                extStepLen = (extStepLen * 3 + (int32_t)extSamples) >> 2;
                seqBpm = constrain((int)(480000 / extStepLen), 40, 300);
            }
            extSamples = 0;
            seqCounter = 0;
            seqCurStepLen = extStepLen;
            seqCurGateLen = (extStepLen * 6) / 10;
            seqBeginStep();
        } else {
            seqRatchetTick();
            if (seqGateOpen && (int32_t)seqCounter >= seqGateEndAt) seqGateEnd(seqPlayStep);
        }
        seqCounter++;
        extSamples++;
        return;
    }

    if (seqCounter == 0) {
        int32_t off =(int32_t)(((uint32_t)seqStepLen * (uint32_t)pSwingQ8) >> 8);
        seqCurStepLen = seqParity ? ((int32_t)seqStepLen - off) : ((int32_t)seqStepLen + off);
        if (seqCurStepLen < 8) seqCurStepLen = 8;
        seqParity = !seqParity;
        seqCurGateLen = (seqCurStepLen * 6) / 10;
        seqBeginStep();
    } else {
        seqRatchetTick();
        if (seqGateOpen && (int32_t)seqCounter >= seqGateEndAt) seqGateEnd(seqPlayStep);
    }
    seqCounter++;
    if ((int32_t)seqCounter >= seqCurStepLen) seqCounter = 0;
}

void seqRecordNote(uint8_t note, uint8_t vel) {
    int target;
    if (seqRunning) {
        int cur = seqPlayStep; if (cur < 0) cur = 0;
        target = ((int32_t)seqCounter > seqCurStepLen / 2) ? seqNextStepVal : cur;
    } else {
        target = seqEditStep;
    }
    target &= 15;

    int n = (int)note - (int)SEQ_BASE_NOTE;
    int oct = (n >= 0) ? (n / 12) : -((-n + 11) / 12);
    if (oct < -1) oct = -1;
    if (oct > 2)  oct = 2;
    int semi = n - oct * 12;
    if (semi < 0) semi = 0;
    if (semi > 12) semi = 12;

    seq[target].semi = (uint8_t)semi;
    seq[target].oct = (int8_t)oct;
    seq[target].gate = true;
    seq[target].accent = (vel >= 100);

    uint16_t mask = (uint16_t)(1u << target);
    if (!seqRunning) {
        int old = seqEditStep;
        int nxt = (target + 1) & 15;
        seqEditStep = nxt;
        mask |= (uint16_t)((1u << old) | (1u << nxt));
        seqEditorDirty = true;
    } else if (target == seqEditStep) {
        seqEditorDirty = true;
    }
    seqRedrawMask |= mask;
}

// ============================================================================
// PADS DO APC -> PASSOS DO SEQUENCIADOR
// ============================================================================
const int     PAD_COUNT    = 17;
const int     PAD_MAP_ADDR = 1400;
const uint8_t PAD_MAGIC    = 0x5C;

uint8_t padCh[PAD_COUNT];
uint8_t padNote[PAD_COUNT];
volatile int8_t padLearnIdx = -1;
volatile bool   padMapDirty = false;

bool padsMapped() {
    for (int i = 0; i < PAD_COUNT; i++) if (padCh[i] != 0) return true;
    return false;
}

int padFind(byte ch, byte note) {
    for (int i = 0; i < PAD_COUNT; i++) {
        if (padCh[i] != 0 && padCh[i] == ch && padNote[i] == note) return i;
    }
    return -1;
}

void padLearnCapture(byte ch, byte note) {
    int idx = padLearnIdx;
    if (idx < 0 || idx >= PAD_COUNT) return;
    for (int i = 0; i < PAD_COUNT; i++) {
        if (padCh[i] == ch && padNote[i] == note) padCh[i] = 0;
    }
    padCh[idx] = ch;
    padNote[idx] = note;
    if (idx + 1 >= PAD_COUNT) { padLearnIdx = -1; padMapDirty = true; }
    else                      { padLearnIdx = idx + 1; }
}

void padPressed(int idx) {
    if (idx == 16) {
        seqCmd = seqRunning ? 2 : 1;
        return;
    }
    int old = seqEditStep;
    seq[idx].gate = !seq[idx].gate;
    seqEditStep = idx;
    seqRedrawMask |= (uint16_t)((1u << old) | (1u << idx));
    seqEditorDirty = true;
    if (!seqRunning && seq[idx].gate) seqAuditionReq = seqStepNote(idx);
}

// ============================================================================
// EFEITOS: BITCRUSHER -> CHORUS -> DELAY PING-PONG -> REVERB (tudo em inteiros)
// Delay e reverb não processam nada quando o MIX está em 0 (e começam limpos ao ligar).
// ============================================================================
#define ENABLE_FX 1   // mude para 0 para testar o som sem os efeitos

static inline __attribute__((always_inline)) int16_t clamp16(int32_t v) {
    return (v > 32767) ? (int16_t)32767 : ((v < -32768) ? (int16_t)-32768 : (int16_t)v);
}

static inline __attribute__((always_inline)) int32_t softLimit(int32_t x) {
    const int32_t T = 24000;
    if (x > T)       { x = T + ((x - T) >> 2);  if (x > 32767)  x = 32767; }
    else if (x < -T) { x = -T + ((x + T) >> 2); if (x < -32768) x = -32768; }
    return x;
}

const int DELAY_MAX = 16000;
static int16_t delayBufL[DELAY_MAX];
static int16_t delayBufR[DELAY_MAX];
static int delayWrite = 0;
static int delayCur = 8000;
static int delayTarget = 8000;
static uint32_t delaySlewCount = 0;
static bool dlyWasOn = false, revWasOn = false;

// reverb: 8 filtros comb compartilhados (L lê o ponto de escrita, R lê um 2º ponto) + 4 allpass por lado
static int COMB_LEN[8] = {810, 862, 927, 984, 1032, 1082, 1130, 1173};
static int COMB_TAP[8] = {271, 288, 309, 328, 344, 361, 377, 391};
static int AP_LEN[4]   = {403, 320, 247, 163};
const int STEREO_SPREAD = 17;
static int16_t combBuf[8][1190];
static int     combIdx[8];
static int32_t combStore[8];
static int16_t apBufL[4][420];
static int16_t apBufR[4][420];
static int     apIdxL[4], apIdxR[4];

static int32_t fxDelayFb = 0, fxDelayMix = 0;
static int32_t fxRevFb = 0, fxRevDamp = 0, fxRevMix = 0;

// chorus (1024 amostras = 32 ms) e bitcrusher
static int16_t  choBuf[1024];
static int      choW = 0;
static uint32_t choPhase = 0, choInc = 0;
static int32_t  choDepthQ8 = 0, choMixQ15 = 0;
static int32_t  crMixQ15 = 0, crHold = 0, crHalf = 0;
static int      crShift = 0, crFactor = 1, crCount = 0;

void fxUpdateParams() {
    fxDelayFb  = (int32_t)(constrain(paramDelayFb, 0.0f, 0.92f) * 32768.0f);
    fxDelayMix = (int32_t)(constrain(paramDelayMix, 0.0f, 0.999f) * 32768.0f);
    delayTarget = constrain((int)(paramDelayTime * 0.001f * (float)SAMPLE_RATE), 1, DELAY_MAX - 1);
    fxRevFb    = (int32_t)((0.70f + constrain(paramRevSize, 0.0f, 1.0f) * 0.25f) * 32768.0f);
    fxRevDamp  = (int32_t)(constrain(paramRevDamp, 0.0f, 1.0f) * 0.5f * 32768.0f);
    fxRevMix   = (int32_t)(constrain(paramRevMix, 0.0f, 0.999f) * 32768.0f);

    choInc     = (uint32_t)(constrain(paramChoRate, 0.1f, 5.0f) * 134217.728f);
    choDepthQ8 = (int32_t)(constrain(paramChoDepth, 0.0f, 1.0f) * 40960.0f);
    choMixQ15  = (int32_t)(constrain(paramChoMix, 0.0f, 1.0f) * 32767.0f);

    int bits = constrain((int)(paramCrBits + 0.5f), 4, 16);
    crShift  = 16 - bits;
    crHalf   = (crShift > 0) ? (1 << (crShift - 1)) : 0;
    crFactor = constrain((int)(paramCrRate + 0.5f), 1, 24);
    crMixQ15 = (int32_t)(constrain(paramCrMix, 0.0f, 1.0f) * 32767.0f);
}

static void clearReverbState() {
    memset(combBuf, 0, sizeof(combBuf));
    memset(apBufL, 0, sizeof(apBufL));
    memset(apBufR, 0, sizeof(apBufR));
    memset(combIdx, 0, sizeof(combIdx));
    memset(combStore, 0, sizeof(combStore));
    memset(apIdxL, 0, sizeof(apIdxL));
    memset(apIdxR, 0, sizeof(apIdxR));
}

static void clearDelayState() {
    memset(delayBufL, 0, sizeof(delayBufL));
    memset(delayBufR, 0, sizeof(delayBufR));
    delayWrite = 0;
    delayCur = delayTarget;
}

// leitura do chorus com interpolação linear (atraso em Q8 amostras)
static inline __attribute__((always_inline)) int32_t choRead(int32_t dQ8) {
    int32_t pos = ((int32_t)choW << 8) - dQ8;
    pos &= 0x3FFFF;
    int i0 = pos >> 8;
    int fr = pos & 255;
    int i1 = (i0 + 1) & 1023;
    return ((int32_t)choBuf[i0] * (256 - fr) + (int32_t)choBuf[i1] * fr) >> 8;
}

static void __not_in_flash_func(fxProcess)(int32_t dry, int32_t &outL, int32_t &outR) {
    int32_t x = dry;

    // ---------- BITCRUSHER (mono) ----------
    if (crMixQ15 > 0) {
        if (++crCount >= crFactor) {
            crCount = 0;
            int32_t q = x;
            if (crShift > 0) {
                q = ((x + crHalf) >> crShift) << crShift;
                if (q > 32767) q = 32767;
            }
            crHold = q;
        }
        x = x + (((crHold - x) * crMixQ15) >> 15);
    }

    // ---------- CHORUS (estéreo, LFOs em oposição) ----------
    int32_t l = x, r = x;
    choBuf[choW] = (int16_t)clamp16(x);
    if (choMixQ15 > 0) {
        int32_t lfoL = sineTable[choPhase >> 24];
        int32_t lfoR = sineTable[(uint32_t)(choPhase + 0x80000000u) >> 24];
        const int32_t base = 122880;                       // 15 ms em Q8
        int32_t tapL = choRead(base + ((lfoL * choDepthQ8) >> 15));
        int32_t tapR = choRead(base + ((lfoR * choDepthQ8) >> 15));
        l = x + (((tapL - x) * choMixQ15) >> 16);
        r = x + (((tapR - x) * choMixQ15) >> 16);
    }
    choPhase += choInc;
    choW = (choW + 1) & 1023;
    int32_t mono = (l + r) >> 1;

    // ---------- DELAY (ping-pong) ----------
    if (fxDelayMix > 0) {
        if (!dlyWasOn) { clearDelayState(); dlyWasOn = true; }
        if ((++delaySlewCount & 15) == 0) {
            int diff = delayTarget - delayCur;
            int step = diff / 128;
            if (step == 0) step = (diff > 0) ? 1 : ((diff < 0) ? -1 : 0);
            delayCur += step;
        }
        int rp = delayWrite - delayCur;
        if (rp < 0) rp += DELAY_MAX;
        int32_t dl = delayBufL[rp];
        int32_t dr = delayBufR[rp];
        delayBufL[delayWrite] = clamp16(mono + ((dr * fxDelayFb) >> 15));
        delayBufR[delayWrite] = clamp16((dl * fxDelayFb) >> 15);
        if (++delayWrite >= DELAY_MAX) delayWrite = 0;
        l += (dl * fxDelayMix) >> 15;
        r += (dr * fxDelayMix) >> 15;
    } else {
        dlyWasOn = false;
    }

    // ---------- REVERB ----------
    if (fxRevMix > 0) {
        if (!revWasOn) { clearReverbState(); revWasOn = true; }
        int32_t rin = (l + r) >> 6;
        int32_t accL = 0, accR = 0;
        for (int i = 0; i < 8; i++) {
            int idx = combIdx[i];
            int len = COMB_LEN[i];
            int32_t o = combBuf[i][idx];
            int j = idx + COMB_TAP[i];
            if (j >= len) j -= len;
            int32_t oR = combBuf[i][j];
            combStore[i] = o + ((((combStore[i] - o) * fxRevDamp) + 16384) >> 15);
            combBuf[i][idx] = clamp16(rin + (((combStore[i] * fxRevFb) + 16384) >> 15));
            if (++idx >= len) idx = 0;
            combIdx[i] = idx;
            accL += o;
            accR += oR;
        }
        for (int i = 0; i < 4; i++) {
            int32_t b = apBufL[i][apIdxL[i]];
            int32_t o = b - accL;
            apBufL[i][apIdxL[i]] = clamp16(accL + (b >> 1));
            if (++apIdxL[i] >= AP_LEN[i]) apIdxL[i] = 0;
            accL = o;

            b = apBufR[i][apIdxR[i]];
            o = b - accR;
            apBufR[i][apIdxR[i]] = clamp16(accR + (b >> 1));
            if (++apIdxR[i] >= AP_LEN[i] + STEREO_SPREAD) apIdxR[i] = 0;
            accR = o;
        }
        l += ((accL >> 2) * (fxRevMix >> 2)) >> 9;
        r += ((accR >> 2) * (fxRevMix >> 2)) >> 9;
    } else {
        revWasOn = false;
    }

    outL = l;
    outR = r;
}

// ---- Estado de controle do áudio ----
static int32_t fCur22 = 3000 << 8, qCur22 = 12000 << 8;       // coeficientes do filtro suavizados (Q22)
static int32_t fTarget22 = 3000 << 8, qTarget22 = 12000 << 8;
static uint8_t ctrlTick = 0;
static int32_t selfWetQ15 = 0;
static bool    seedOn = false;
static int32_t vcaQ15 = 0;

static int32_t MIX_GAIN_Q15[5] = {32768, 32768, 23170, 18918, 16384};
static int32_t mixGainQ15 = 32768;

// multiplicação (estado Q8) x (coeficiente Q14) sem estourar 32 bits, com arredondamento
static inline __attribute__((always_inline)) int32_t mulQ14(int32_t s, int32_t c) {
    return ((((s >> 8) * c) + 32) >> 6) + ((((s & 255) * c) + 8192) >> 14);
}

// uma linha da matriz (destino d): soma das 4 fontes x quantidades
static inline __attribute__((always_inline)) int32_t mmRow(int d, int32_t s0, int32_t s1, int32_t s2, int32_t s3) {
    return ((s0 * pMmQ15[d * 4 + 0]) >> 15) + ((s1 * pMmQ15[d * 4 + 1]) >> 15)
         + ((s2 * pMmQ15[d * 4 + 2]) >> 15) + ((s3 * pMmQ15[d * 4 + 3]) >> 15);
}

const int32_t FILTER2_Q14 = 18842;   // amortecimento fixo do 2º estágio (1,15)

// Gera até 96 amostras e devolve quantas foram geradas (para o medidor de CPU).
// Aqui dentro NÃO há nenhuma conta de float: tudo é inteiro e roda da RAM.
int __not_in_flash_func(audioProcess)() {
    int budget = 96;
    int produced = 0;

    // parâmetros publicados lidos uma vez por chamada
    const uint8_t fm = filtMode;
    const bool    isSuper = (currentWave == WAVE_SUPERSAW);
    const bool    isWt    = (currentWave == WAVE_TABLE);
    const bool    driveOn = pDriveOn;
    const int32_t driveGain = pDriveGainQ8, driveComp = pDriveCompQ15;
    const int32_t mainGain = pMainGainQ15, subGain = pSubGainQ15;
    const bool    subOn = (subGain > 200);
    const int32_t spreadQ16 = pSpreadQ16;
    const int     gsh = pGlideShift;
    const bool    pitchOn = pPitchOn;
    const int32_t pitchDec = pPitchDecInc;
    const uint32_t *ptab = pitchMulTbl[pitchTblSel];
    const int32_t volQ = volQ15;
    const bool    slope24 = (filtSlope != 0);
    const bool    pmOn = pPitchModOn;
    const bool    mmOn = pMmOn;
    const bool    o2On = pO2On, o2SyncOn = pO2Sync;
    const int32_t o2MulQ8 = pO2MulQ8, o2Main = pO2MainQ15, o2Gain = pO2GainQ15;
    const uint8_t o2w = o2Wave;
    const bool    ringOn = o2Ring;
    const bool    foldOn = pFoldOn;
    const int32_t foldGain = pFoldGainQ8;
    const int     uniCnt = pUniN;
    const bool    uniOn = (uniCnt > 1) && !isSuper && (currentWave != WAVE_NOISE);
    const int32_t uniGain = pUniGainQ15;
    int32_t       uniSpread[5];
    for (int k = 0; k < 5; k++) uniSpread[k] = pUniSpreadQ16[k];
    const bool    fmOn = pFmOn;
    const int32_t fmIdx = pFmIdxQ12, fmRatio = pFmRatioQ8, fmEnvAmt = pFmEnvQ15, fmFb = pFmFbQ12, fmDec = pFmDecInc;

    const int32_t KNEE = 20000 << 8;
    const int32_t LIM  = (65535 << 8);

    while (budget-- > 0 && i2s.availableForWrite() >= 2) {
        seqTick();

        // ---------- controle: 1 a cada 32 amostras (1 kHz), só inteiros ----------
        if ((ctrlTick++ & 31) == 0) {
            const uint32_t lfoOld = lfoPhase32;
            lfoPhase32 += (pLfoInc32 << 5);
            int32_t lfoRaw;                                               // Q15, -32768..32767
            switch (lfoShape) {
                case 1:  lfoRaw = (int32_t)(((lfoPhase32 & 0x80000000u) ? ~lfoPhase32 : lfoPhase32) >> 15) - 32768; break;
                case 2:  lfoRaw = (int32_t)(lfoPhase32 >> 16) - 32768; break;
                case 3:  lfoRaw = (lfoPhase32 & 0x80000000u) ? 32767 : -32767; break;
                case 4:                                                   // novo valor aleatório a cada ciclo
                    if (lfoPhase32 < lfoOld) {
                        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
                        lfoHold = (int32_t)(int16_t)(rngState >> 16);
                    }
                    lfoRaw = lfoHold;
                    break;
                default: lfoRaw = sineTable[lfoPhase32 >> 24]; break;
            }

            int32_t fsum = 0, asum = 0, fcnt = 0;
            for (int i = 0; i < MAX_VOICES; i++) {
                Voice &v = voices[i];
                if (!v.active) continue;
                filtEnvStep32(v);
                fsum += v.filtEnv;
                asum += v.env;
                fcnt++;
            }
            int32_t envAvgQ15 = (fcnt > 0) ? ((fsum / fcnt) >> 9) : 0;
            int32_t ampAvgQ15 = (fcnt > 0) ? ((asum / fcnt) >> 9) : 0;
            int32_t velQ15 = (int32_t)lastVelocity * 258 - 16384;

            // ---- matriz de modulação (fontes Q15 x quantidade Q15) ----
            int32_t mCut = 0, mPit = 0, mPwm = 0, mVol = 0;
            if (mmOn) {
                const int32_t s0 = lfoRaw, s1 = envAvgQ15, s2 = ampAvgQ15, s3 = (int32_t)lastVelocity * 258;
                mCut = mmRow(0, s0, s1, s2, s3);
                mPit = mmRow(1, s0, s1, s2, s3);
                mPwm = mmRow(2, s0, s1, s2, s3);
                mVol = mmRow(3, s0, s1, s2, s3);
            }
            if (pmOn) {
                if (mPit > 32768) mPit = 32768;
                if (mPit < -32768) mPit = -32768;
                uint32_t pos = (uint32_t)(mPit + 32768);          // 0..65536
                uint32_t ti = pos >> 9, fr = pos & 511;
                uint32_t ta = pitchModTbl[ti], tb = pitchModTbl[(ti < 128) ? ti + 1 : 128];
                pitchModMul = ta + (uint32_t)((((int32_t)(tb - ta)) * (int32_t)fr) >> 9);
            } else {
                pitchModMul = 65536;
            }
            int32_t gv = 32768 + mVol;
            if (gv < 0) gv = 0;
            if (gv > 49152) gv = 49152;
            volTargetQ15 = (volQ * gv) >> 15;

            int32_t pw = pPwmBaseQ16 + ((lfoRaw * pPwmModQ16) >> 15) + ((mPwm * 26214) >> 15);   // largura do pulso (Q16)
            if (pw < 3277)  pw = 3277;
            if (pw > 62259) pw = 62259;
            pulseWQ32 = (uint32_t)pw << 16;

            // posição da wavetable: base + LFO + linha "PWM/WT" da matriz (env a 100% varre a tabela toda)
            int32_t wp = pWtPosQ16 + ((lfoRaw * pWtLfoQ15) >> 15) + mPwm * 2;
            if (wp < 0)     wp = 0;
            if (wp > 65535) wp = 65535;
            wtPosTarget = wp;

            accentEnvQ15 -= (accentEnvQ15 >> 7) + (accentEnvQ15 > 0 ? 1 : 0);   // accent descarrega (~130 ms)
            int32_t m = pCutBaseQ15
                      + ((lfoRaw * pLfoHalfQ15) >> 15)
                      + ((envAvgQ15 * pFiltAmtQ15) >> 15)
                      + ((velQ15 * pVelFiltQ15) >> 15)
                      + mCut
                      + (accentEnvQ15 >> 1);                     // accent 303
            if (m < 328)   m = 328;
            if (m > 32768) m = 32768;
            fTarget22 = (int32_t)fTableQ14[m >> 5] * 256;
            qTarget22 = pQTargetQ14 * 256;
            selfWetQ15 = pSelfWetQ15;
            seedOn = pSeedOn;
        }

        // suavização por amostra dos coeficientes do filtro
        fCur22 += (fTarget22 - fCur22) >> 6;
        qCur22 += (qTarget22 - qCur22) >> 6;
        const int32_t fF = fCur22 >> 8;
        const int32_t qF = qCur22 >> 8;

        if (isWt) {                                     // morph suavizado por amostra (sem degraus a 1 kHz)
            wtPosCur += (wtPosTarget - wtPosCur) >> 6;
            uint32_t p7 = (uint32_t)wtPosCur * 7u;
            wtFrame = (int)(p7 >> 16);
            wtFracQ14 = (int32_t)((p7 & 0xFFFF) >> 2);
        }

        // ---------- vozes ----------
        int32_t mixSum = 0;
        int32_t activeCount = 0;
        int32_t heldCount = 0;
        int32_t envMaxQ24 = 0;

        for (int i = 0; i < MAX_VOICES; i++) {
            Voice &v = voices[i];
            if (!v.active) continue;
            ampEnvStep(v);
            if (!v.active) continue;

            if (v.phaseInc != v.targetInc) {
                int32_t d = (int32_t)(v.targetInc - v.phaseInc);
                int32_t th = 1 << gsh;
                if (d > -th && d < th) v.phaseInc = v.targetInc;
                else v.phaseInc += (d >> gsh);
            }

            uint32_t incEff = v.phaseInc;
            if (pitchOn && v.pitchEnv > 0) {
                v.pitchEnv -= pitchDec;
                if (v.pitchEnv < 0) v.pitchEnv = 0;
                uint64_t t = ((uint64_t)v.phaseInc * (uint64_t)ptab[v.pitchEnv >> 17]) >> 16;
                incEff = (t > 0x7FFFFFFFull) ? 0x7FFFFFFFu : (uint32_t)t;
            }
            if (pmOn) {
                uint64_t t2 = ((uint64_t)incEff * (uint64_t)pitchModMul) >> 16;
                incEff = (t2 > 0x7FFFFFFFull) ? 0x7FFFFFFFu : (uint32_t)t2;
            }
            v.incEff = incEff;

            if (isSuper) {
                uint32_t d = (((incEff >> 10) * (uint32_t)spreadQ16) >> 6);
                v.inc2 = incEff + d;
                v.inc3 = incEff - d;
            }

            // ---- FM: modulador seno na razão escolhida desvia a fase da portadora ----
            if (fmOn) {
                int32_t m = sineInterp(v.phaseFm + ((uint32_t)(v.fmPrev * fmFb) << 5));   // feedback
                v.fmPrev = m;
                v.phaseFm += (incEff >> 8) * (uint32_t)fmRatio;                           // segue o pitch da voz
                if (v.fmEnv > 0) { v.fmEnv -= fmDec; if (v.fmEnv < 0) v.fmEnv = 0; }
                int32_t eQ15 = (32768 - fmEnvAmt) + (((v.fmEnv >> 9) * fmEnvAmt) >> 15);
                int32_t idx = (fmIdx * eQ15) >> 15;                                       // Q12 ciclos
                v.pmOff = (uint32_t)(m * idx) << 5;
            } else {
                v.pmOff = 0;
            }
            int32_t s;
            if (uniOn) {                         // UNISON: várias cópias desafinadas da mesma onda
                int32_t acc = 0;
                for (int k = 0; k < uniCnt; k++) {
                    int32_t sp = uniSpread[k];
                    uint32_t dk = ((incEff >> 10) * (uint32_t)((sp < 0) ? -sp : sp)) >> 6;
                    uint32_t ik = (sp < 0) ? (incEff - dk) : (incEff + dk);
                    acc += oscSampleAt(v.uph[k] + v.pmOff, ik);
                    v.uph[k] += ik;
                }
                s = ((acc >> 1) * uniGain) >> 14;
            } else {
                s = oscSample(v);
            }
            if (o2On) {                          // OSC2: segue o pitch da voz + semitons/cents
                uint32_t i2 = (incEff >> 8) * (uint32_t)o2MulQ8;
                int32_t s2 = osc2Sample(v.phaseO2, i2, o2w);
                v.phaseO2 += i2;
                if (ringOn) s2 = (s * s2) >> 15;      // ring mod: OSC1 x OSC2
                s = ((s * o2Main) + (s2 * o2Gain)) >> 15;
            }
            if (subOn) {
                int32_t sb = subSample(v.phaseSub, incEff >> 1);
                s = ((s * mainGain) + (sb * subGain)) >> 15;
            }
            int32_t amp = ((v.env >> 9) * v.gainQ15) >> 15;
            mixSum += (s * amp) >> 15;
            if (v.env > envMaxQ24) envMaxQ24 = v.env;
            if (v.stage != ENV_RELEASE && v.stage != ENV_IDLE) heldCount++;

            {
                uint32_t oldPh = v.phase;
                v.phase += incEff;
                // hard sync: o OSC2 reinicia quando o OSC1 completa um ciclo
                if (o2SyncOn && o2On && v.phase < oldPh) v.phaseO2 = (v.phase >> 8) * (uint32_t)o2MulQ8;
            }
            if (isSuper) {
                v.phase2 += v.inc2;
                v.phase3 += v.inc3;
            }
            if (subOn) v.phaseSub += (incEff >> 1);
            activeCount++;
        }

        int32_t gTarget = MIX_GAIN_Q15[(heldCount > 4) ? 4 : heldCount];
        mixGainQ15 += (gTarget - mixGainQ15) >> 8;
        int32_t in = ((mixSum >> 2) * mixGainQ15) >> 13;
        if (in > 65535)  in = 65535;
        if (in < -65535) in = -65535;

        // ---------- drive (saturação cúbica antes do filtro, em inteiros) ----------
        // ---------- wavefolder: o que passa do limite é dobrado de volta ----------
        if (foldOn) {
            int32_t x = (in * foldGain) >> 8;
            int32_t p = (x + 65536) & 262143;                   // período de 4 x 65536
            if (p > 131072) p = 262144 - p;
            in = p - 65536;
            if (in > 65535) in = 65535;
        }

        if (driveOn) {
            int32_t x = (in * driveGain) >> 8;
            if (x > 32768)  x = 32768;
            if (x < -32768) x = -32768;
            int32_t x2 = (x * x) >> 15;
            int32_t x3 = (x2 * x) >> 15;
            int32_t y = x - ((x3 * 10923) >> 15);
            in = (y * driveComp) >> 15;
        }

        // ---------- filtro SVF em ponto fixo (LP / BP / HP, com auto-oscilação) ----------
        if (seedOn && activeCount > 0) {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            in += ((int32_t)(int16_t)(rngState >> 16)) >> 8;
        }
        const int32_t in8 = in * 256;
        svL += mulQ14(svB, fF);
        if (svL > LIM)  svL = LIM;
        if (svL < -LIM) svL = -LIM;
        int32_t hi = in8 - svL - mulQ14(svB, qF);
        if (hi > LIM)  hi = LIM;
        if (hi < -LIM) hi = -LIM;
        svB += mulQ14(hi, fF);
        if (svB > KNEE)       svB = KNEE + ((svB - KNEE) >> 2);
        else if (svB < -KNEE) svB = -KNEE + ((svB + KNEE) >> 2);
        if (svB > LIM)  svB = LIM;
        if (svB < -LIM) svB = -LIM;

        int32_t s1o;                                    // saída do 1º estágio (Q8)
        if (fm == 0)      s1o = svL;
        else if (fm == 1) s1o = (svB * 3) >> 1;         // passa-banda x1,5
        else              s1o = hi;
        int32_t lo;
        if (slope24) {
            // 2º SVF em cascata (mesma frequência, amortecimento fixo): 24 dB/oitava
            if (s1o > LIM)  s1o = LIM;
            if (s1o < -LIM) s1o = -LIM;
            sv2L += mulQ14(sv2B, fF);
            if (sv2L > LIM)  sv2L = LIM;
            if (sv2L < -LIM) sv2L = -LIM;
            int32_t hi2 = s1o - sv2L - mulQ14(sv2B, FILTER2_Q14);
            if (hi2 > LIM)  hi2 = LIM;
            if (hi2 < -LIM) hi2 = -LIM;
            sv2B += mulQ14(hi2, fF);
            if (sv2B > KNEE)       sv2B = KNEE + ((sv2B - KNEE) >> 2);
            else if (sv2B < -KNEE) sv2B = -KNEE + ((sv2B + KNEE) >> 2);
            if (sv2B > LIM)  sv2B = LIM;
            if (sv2B < -LIM) sv2B = -LIM;
            int32_t o2;
            if (fm == 0)      o2 = sv2L;
            else if (fm == 1) o2 = (sv2B * 9) >> 3;
            else              o2 = hi2;
            lo = o2 >> 8;
        } else {
            sv2L = 0; sv2B = 0;
            lo = s1o >> 8;
        }
        if (lo > 65535)  lo = 65535;
        if (lo < -65535) lo = -65535;
        volCurQ15 += (volTargetQ15 - volCurQ15) >> 5;   // volume (knob + matriz) suavizado
        int32_t dry = (lo * (volCurQ15 >> 1)) >> 14;

        // VCA pós-filtro (zona de auto-oscilação): a senoide acompanha o envelope
        vcaQ15 += ((envMaxQ24 >> 9) - vcaQ15) >> 4;
        int32_t pg = 32768 - ((selfWetQ15 * (32768 - vcaQ15)) >> 15);
        dry = (dry * pg) >> 15;
        dry = softLimit(dry);
        lastPlayedSample = (int16_t)dry;

        if ((ctrlTick & 1) == 0) {
            uint16_t w = scopeW;
            scopeBuf[w] = (int16_t)dry;
            scopeW = (uint16_t)((w + 1) & (SCOPE_LEN - 1));
        }

#if ENABLE_FX
        int32_t outL, outR;
        fxProcess(dry, outL, outR);
        {   // medidor de saída: pico + aviso de clip (limitador trabalhando pesado)
            int32_t aL = (outL < 0) ? -outL : outL, aR = (outR < 0) ? -outR : outR;
            int32_t a = (aL > aR) ? aL : aR;
            if (a > outPeak) outPeak = a;
            if (a > 40000) outClip = true;
        }
        i2s.write16((int16_t)softLimit(outL), (int16_t)softLimit(outR));
#else
        i2s.write16((int16_t)dry, (int16_t)dry);
#endif
        produced++;
    }
    return produced;
}

// ============================================================================
// KNOBS E MIDI (com MIDI LEARN: qualquer CC pode controlar qualquer um dos 16 parâmetros)
// ============================================================================
void applyKnob(int knobIndex, int value0to127) {
    float norm = value0to127 / 127.0f;
    switch (knobIndex) {
        case 0: paramCutoff  = norm; break;
        case 1: paramReso    = norm; break;
        case 2: paramAttack  = 0.001f + norm * 2.0f; break;
        case 3: paramDecay   = 0.001f + norm * 2.0f; break;
        case 4: paramSustain = norm; break;
        case 5: paramRelease = 0.001f + norm * 3.0f; break;
        case 6: currentWave  = (Waveform)((int)(norm * ((int)WAVE_COUNT - 0.01f))); break;
        case 7: paramVolume  = norm; break;
    }
}

void syncKnobsFromParams() {
    knobValue[0] = constrain((int)(paramCutoff * 127.0f + 0.5f), 0, 127);
    knobValue[1] = constrain((int)(paramReso * 127.0f + 0.5f), 0, 127);
    knobValue[2] = constrain((int)((paramAttack  - 0.001f) / 2.0f * 127.0f + 0.5f), 0, 127);
    knobValue[3] = constrain((int)((paramDecay   - 0.001f) / 2.0f * 127.0f + 0.5f), 0, 127);
    knobValue[4] = constrain((int)(paramSustain * 127.0f + 0.5f), 0, 127);
    knobValue[5] = constrain((int)((paramRelease - 0.001f) / 3.0f * 127.0f + 0.5f), 0, 127);
    knobValue[6] = constrain((int)(((float)currentWave + 0.5f) * 127.0f / (float)WAVE_COUNT), 0, 127);
    knobValue[7] = constrain((int)(paramVolume * 127.0f + 0.5f), 0, 127);
}

const int MAP_COUNT = 16;
const char* MAP_NAMES[MAP_COUNT] = {"CUTOFF", "RESONANCE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "WAVEFORM", "VOLUME",
                                    "DRIVE", "FILT AMT", "LFO RATE", "LFO DEPTH", "PULSE W", "SUB LEVEL", "DLY MIX", "REV MIX"};
uint8_t mapCC[MAP_COUNT] = {48, 49, 50, 51, 52, 53, 54, 55, 255, 255, 255, 255, 255, 255, 255, 255};
volatile int8_t  mapLearnIdx = -1;
volatile bool    mapDirty = false;
volatile uint8_t mapChangeCnt = 0;

void applyMapParam(int p, int v) {
    float n = v / 127.0f;
    if (p < 8) { knobValue[p] = v; applyKnob(p, v); return; }
    switch (p) {
        case 8:  paramDrive = n; break;
        case 9:  paramFiltAmt = n; break;
        case 10: paramLfoRate = 0.1f + n * 19.9f; break;
        case 11: paramLfoDepth = n; break;
        case 12: paramPwm = 0.05f + n * 0.90f; break;
        case 13: paramSubLevel = n; break;
        case 14: paramDelayMix = n; break;
        case 15: paramRevMix = n; break;
    }
}

void handleControlButton(uint8_t note, bool pressed) {
    if (!pressed) return;
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "Botao nota %u", note);
}

void onNoteOn(byte channel, byte note, byte velocity) {
    midiRxCount++;
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "Ch%u N%u v%u", channel, note, velocity);

    if (padLearnIdx >= 0) {
        if (velocity > 0) padLearnCapture(channel, note);
        return;
    }
    int pad = padFind(channel, note);
    if (pad >= 0) {
        if (velocity > 0) padPressed(pad);
        return;
    }

    if (velocity == 0) { keyOff(note); return; }
    if (note >= SYNTH_KEY_LOW && note <= SYNTH_KEY_HIGH) {
        if (seqRecArm) seqRecordNote(note, velocity);
        keyOn(note, velocity);
    } else {
        handleControlButton(note, true);
    }
}
void onNoteOff(byte channel, byte note, byte velocity) {
    midiRxCount++;
    if (padFind(channel, note) >= 0) return;
    if (note >= SYNTH_KEY_LOW && note <= SYNTH_KEY_HIGH) keyOff(note);
    else handleControlButton(note, false);
}
void onControlChange(byte channel, byte cc, byte value) {
    midiRxCount++;
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "CC %u = %u", cc, value);
    if (cc == 123 || cc == 120) {
        synthAllNotesOff();
        monoCount = 0; monoVoiceNote = -1;
        return;
    }
    if (mapLearnIdx >= 0) {
        if (cc < 120) {
            for (int j = 0; j < MAP_COUNT; j++) if (mapCC[j] == cc) mapCC[j] = 255;
            mapCC[mapLearnIdx] = cc;
            mapLearnIdx = -1;
            mapChangeCnt++;
            mapDirty = true;
        }
        return;
    }
    for (int p = 0; p < MAP_COUNT; p++) {
        if (mapCC[p] == cc) applyMapParam(p, value);
    }
}
void onProgramChange(byte channel, byte number) {
    midiRxCount++;
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "Prog %u", number);
    if (number < PRESET_COUNT) presetReq = number;
}

// ---- MIDI clock externo (24 pulsos por semínima) ----
void onClock() {
    if (!seqSyncExt || !seqRunning) return;
    if (extPulse == 0) seqStepPending = true;
    extPulse++;
    if (extPulse >= 6) extPulse = 0;
}
void onStart() {
    if (!seqSyncExt) return;
    seqPrepareStart();
    seqRunning = true;
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "MIDI Start");
}
void onContinue() {
    if (!seqSyncExt) return;
    if (!seqRunning) { extPulse = 0; seqStepPending = false; seqRunning = true; }
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "MIDI Continue");
}
void onStop() {
    if (!seqSyncExt) return;
    seqStopInternal();
    snprintf(lastMidiMsg, sizeof(lastMidiMsg), "MIDI Stop");
}

// ============================================================================
// PRESETS: 8 slots na flash (EEPROM emulada) + 8 sons de fábrica + nomes
// Mapa da EEPROM: 0..15 cabeçalho | 16..1295 slots | 1400 pads | 1500 nomes |
//                 1600 FX extras | 1800 extras (drive/glide/modos/slope) | 1900 mapa MIDI |
//                 2048 padrões A-D + chain (8 x 136) | 3200 matriz de modulação (8 x 20) |
//                 3400 FM (8 x 8) | 3500 OSC2 + unison (8 x 12) | 3700 calibração do touch |
//                 3720 wavetable (8 x 4) | 3760 LFO/accent/ring/fold (8 x 6)
// ============================================================================
const int     EE_SIZE = 4096;
const int     EE_HEADER = 16;
const int     PRESET_SLOT_BYTES = 160;
const uint8_t EE_MAGIC = 0x7B;
const uint8_t PRESET_MAGIC = 0xA6;
const int     NAME_ADDR = 1500;
const int     NAME_LEN = 12;
const uint8_t NAME_MAGIC = 0x4E;
const int     EXTRA_ADDR = 1800;
const int     EXTRA_BYTES = 12;
const uint8_t EXTRA_MAGIC = 0xE7;
const int     FXEX_ADDR = 1600;
const int     FXEX_BYTES = 8;
const uint8_t FXEX_MAGIC = 0xF1;
const int     MAP_ADDR = 1900;
const uint8_t MAP_MAGIC = 0x6D;

static_assert(sizeof(PresetData) <= PRESET_SLOT_BYTES, "PresetData maior que o slot");

void presetRedrawUI();

// fm: bits 0-1 = tipo do filtro (0 LP, 1 BP, 2 HP); bit 2 (valor 4) = 24 dB
//    nome         onda          A     D     S     R     cut   res   fAmt  fAtt  fDec  lfoR  lfoD  det   vol    dlyT   dlyFb dlyMix revS  revD  revM   pwm   pwmM  sub   pAmt  pDec  vel    drive fm vm glide
const FactoryPreset FACTORY[PRESET_COUNT] = {
    {"ROLLER",    WAVE_SAW,      0.002f,0.18f,0.35f,0.06f, 0.26f,0.38f,0.45f,0.002f,0.12f, 0.5f,0.00f,0.50f,0.85f, 363.0f,0.25f,0.00f, 0.30f,0.70f,0.05f, 0.30f,0.00f,0.45f, 0.00f,80.0f,0.30f, 0.25f,4,1,25.0f},
    {"DUB CHORD", WAVE_SAW,      0.003f,0.32f,0.00f,0.25f, 0.30f,0.25f,0.45f,0.003f,0.22f, 0.1f,0.06f,0.50f,0.75f, 363.0f,0.58f,0.35f, 0.85f,0.65f,0.35f, 0.30f,0.00f,0.00f, 0.00f,80.0f,0.40f, 0.10f,0,0,60.0f},
    {"HYPNO PLCK",WAVE_SINE,     0.001f,0.22f,0.00f,0.18f, 0.55f,0.20f,0.30f,0.001f,0.15f, 7.56f,0.12f,0.50f,0.75f, 242.0f,0.45f,0.30f, 0.60f,0.50f,0.20f, 0.30f,0.00f,0.00f, 0.00f,80.0f,0.30f, 0.00f,0,0,60.0f},
    {"RUMBLE",    WAVE_SINE,     0.001f,0.45f,0.00f,0.30f, 0.22f,0.10f,0.15f,0.001f,0.10f, 0.5f,0.00f,0.50f,0.42f, 250.0f,0.00f,0.00f, 0.90f,0.90f,0.30f, 0.30f,0.00f,0.00f, 0.45f,45.0f,0.00f, 0.10f,4,1,1.0f},
    {"BLIP",      WAVE_SQUARE,   0.001f,0.09f,0.00f,0.08f, 0.65f,0.45f,0.35f,0.001f,0.06f, 0.5f,0.00f,0.50f,0.70f, 121.0f,0.50f,0.30f, 0.50f,0.40f,0.15f, 0.40f,0.00f,0.00f, 0.08f,25.0f,0.20f, 0.00f,1,0,60.0f},
    {"DEEP PAD",  WAVE_TABLE,    0.900f,1.20f,0.80f,2.20f, 0.32f,0.15f,0.15f,0.800f,1.50f, 0.1f,0.18f,0.50f,0.65f, 363.0f,0.40f,0.18f, 0.90f,0.60f,0.50f, 0.30f,0.00f,0.00f, 0.00f,80.0f,0.00f, 0.00f,0,0,60.0f},
    {"ACID TOOL", WAVE_SAW,      0.002f,0.20f,0.20f,0.07f, 0.20f,0.68f,0.60f,0.002f,0.18f, 0.5f,0.00f,0.50f,0.80f, 363.0f,0.35f,0.15f, 0.40f,0.60f,0.08f, 0.30f,0.00f,0.00f, 0.00f,80.0f,0.35f, 0.30f,4,2,80.0f},
    {"METAL PERC",WAVE_SQUARE,   0.001f,0.06f,0.00f,0.05f, 0.70f,0.50f,0.20f,0.001f,0.05f, 0.5f,0.00f,0.50f,0.65f, 182.0f,0.30f,0.20f, 0.50f,0.30f,0.18f, 0.40f,0.00f,0.00f, 0.00f,80.0f,0.40f, 0.00f,2,0,60.0f}
};

// Extras de fábrica (o que não cabe na tabela acima): LFO, OSC2, FM, wavetable, chorus,
// bitcrusher, accent e o groove. Valem quando o slot está com o som de fábrica.
struct FactoryEx {
    uint8_t lfoShape, lfoSync, subSine;
    float accent;
    float o2Lvl, o2Semi, o2Det; uint8_t o2Wave, o2Ring, uniSel;
    float fmAmt, fmRatio, fmDec, fmEnv, fmFb;
    uint8_t wtBank; float wtPos, wtLfo;
    float choRate, choDepth, choMix, crBits, crRate, crMix;
    int bpm; float swing;
};
const FactoryEx FACTORY_EX[PRESET_COUNT] = {
    // lfo sync sub  acc    o2Lvl o2St  o2Det w ring uni  fmAmt ratio dec    env   fb    wt pos   wtLfo  choR choD choM  bits rate crush bpm swing
    {0, 0, 1, 0.35f,  0.00f, 0,    0,    0, 0, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 16, 1, 0.00f, 124, 0.12f},  // ROLLER
    {1, 1, 0, 0.30f,  0.60f, 7,    6,    0, 0, 1,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.5f, 0.6f, 0.30f, 16, 1, 0.00f, 122, 0.10f},  // DUB CHORD
    {0, 1, 0, 0.30f,  0.00f, 0,    0,    0, 0, 0,   0.38f, 3.5f, 120,   0.9f, 0.1f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 16, 1, 0.00f, 124, 0.15f},  // HYPNO PLCK
    {0, 0, 0, 0.20f,  0.00f, 0,    0,    0, 0, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 16, 1, 0.00f, 124, 0.00f},  // RUMBLE
    {0, 0, 0, 0.30f,  0.00f, 0,    0,    0, 0, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 10, 2, 0.35f, 125, 0.10f},  // BLIP
    {1, 1, 0, 0.30f,  0.40f, 7,    8,    2, 0, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 1, 0.3f, 0.35f, 0.4f, 0.6f, 0.45f, 16, 1, 0.00f, 120, 0.00f},  // DEEP PAD
    {0, 0, 0, 0.65f,  0.00f, 0,    0,    0, 0, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 16, 1, 0.00f, 124, 0.08f},  // ACID TOOL
    {0, 0, 0, 0.30f,  0.80f, 19,   0,    1, 1, 0,   0.00f, 2.0f, 200,   0.5f, 0.0f, 0, 0.0f, 0.0f,  0.8f, 0.5f, 0.00f, 12, 1, 0.20f, 126, 0.15f},  // METAL PERC
};

// Groove de cada preset (padrão A), tom de Sol menor. "G0" = Sol da oitava -1 (G1), "G1" = G2...
// Depois da nota: A = accent, S = slide, p/h/q = probabilidade 75/50/25 %, r = ratchet x2.
// Os padrões de fábrica não usam probabilidade: todo passo com nota toca sempre.
const char* const FACTORY_PAT[PRESET_COUNT][16] = {
    {"--","G0","G0A","G0",  "--","G0","G0","A#0", "--","G0","G0A","G0", "--","F0","G0","G1S"},      // ROLLER
    {"--","--","G1A","--",  "--","--","--","--",  "--","--","G1","--",   "--","F1","--","--"},      // DUB CHORD
    {"G1","--","D2","A#1",  "--","G2","D2","--", "G1","--","F2","A#1",  "D2","--","G1","C2"},     // HYPNO PLCK
    {"G0A","--","--","--",  "G0","--","--","--",  "G0A","--","--","--",   "G0","--","--","G0"},     // RUMBLE
    {"--","--","G2","--",   "--","D3","--","--", "--","G2","--","A#2",  "--","--","F2","--"},     // BLIP
    {"G1","--","--","--",   "--","--","--","--",  "D#1","--","--","--",   "--","--","--","--"},      // DEEP PAD
    {"G0A","G0","G1S","G0", "A#0","G0A","--","G0","C1S","D1","G0A","--",  "F0","G0","G1AS","G0"},   // ACID TOOL
    {"--","--","G3","--",   "--","--","G3A","--", "--","--","G3","G3",   "--","--","G3Ar","--"},    // METAL PERC
};


static float clampF(float v, float lo, float hi) {
    if (!(v >= lo)) return lo;
    if (v > hi) return hi;
    return v;
}

// ---- nomes personalizados dos slots ----
char slotName[PRESET_COUNT][NAME_LEN];
bool slotHasName[PRESET_COUNT];

void nameSaveAll() {
    EEPROM.write(NAME_ADDR, NAME_MAGIC);
    for (int i = 0; i < PRESET_COUNT; i++) {
        for (int j = 0; j < NAME_LEN; j++) {
            EEPROM.write(NAME_ADDR + 1 + i * NAME_LEN + j, slotHasName[i] ? (uint8_t)slotName[i][j] : 0);
        }
    }
    EEPROM.commit();
}

void nameLoadAll() {
    for (int i = 0; i < PRESET_COUNT; i++) { slotHasName[i] = false; slotName[i][0] = 0; }
    if (EEPROM.read(NAME_ADDR) != NAME_MAGIC) return;
    for (int i = 0; i < PRESET_COUNT; i++) {
        for (int j = 0; j < NAME_LEN; j++) slotName[i][j] = (char)EEPROM.read(NAME_ADDR + 1 + i * NAME_LEN + j);
        slotName[i][NAME_LEN - 1] = 0;
        for (int j = 0; slotName[i][j]; j++) {
            char c = slotName[i][j];
            bool ok = (c == ' ') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!ok) slotName[i][j] = '?';
        }
        slotHasName[i] = (slotName[i][0] != 0);
    }
}

// ---- parâmetros extras de cada slot (drive, glide, filtro, voz) ----
void extraWrite(int slot) {
    int a = EXTRA_ADDR + slot * EXTRA_BYTES;
    float drive = paramDrive, glide = paramGlideMs;
    EEPROM.write(a, EXTRA_MAGIC);
    EEPROM.put(a + 1, drive);
    EEPROM.put(a + 5, glide);
    EEPROM.write(a + 9, (uint8_t)((filtMode & 3) | ((voiceMode & 3) << 2) | ((filtSlope & 1) << 4)));
}

bool extraRead(int slot, float &drive, float &glide, uint8_t &modes) {
    int a = EXTRA_ADDR + slot * EXTRA_BYTES;
    if (EEPROM.read(a) != EXTRA_MAGIC) return false;
    EEPROM.get(a + 1, drive);
    EEPROM.get(a + 5, glide);
    modes = EEPROM.read(a + 9);
    return true;
}

void extraInvalidate(int slot) { EEPROM.write(EXTRA_ADDR + slot * EXTRA_BYTES, 0); }

void presetApplyExtra(float drive, float glide, uint8_t modes) {
    paramDrive = clampF(drive, 0.0f, 1.0f);
    paramGlideMs = clampF(glide, 1.0f, 500.0f);
    uint8_t fmd = modes & 3;
    if (fmd > 2) fmd = 0;
    filtMode = fmd;
    filtSlope = (modes >> 4) & 1;
    uint8_t vm = (modes >> 2) & 3;
    if (vm > 2) vm = 0;
    if (vm != voiceMode) { voiceMode = vm; monoResetReq = true; }
}

// ---- FX extras de cada slot (chorus e bitcrusher), 1 byte por parâmetro ----
void fxExtraWrite(int slot) {
    int a = FXEX_ADDR + slot * FXEX_BYTES;
    EEPROM.write(a, FXEX_MAGIC);
    EEPROM.write(a + 1, (uint8_t)constrain((int)((paramChoRate - 0.1f) / 4.9f * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 2, (uint8_t)constrain((int)(paramChoDepth * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 3, (uint8_t)constrain((int)(paramChoMix * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 4, (uint8_t)constrain((int)(paramCrBits + 0.5f), 4, 16));
    EEPROM.write(a + 5, (uint8_t)constrain((int)(paramCrRate + 0.5f), 1, 24));
    EEPROM.write(a + 6, (uint8_t)constrain((int)(paramCrMix * 255.0f + 0.5f), 0, 255));
}

void fxExtraInvalidate(int slot) { EEPROM.write(FXEX_ADDR + slot * FXEX_BYTES, 0); }

void fxExtraLoad(int slot, bool user) {
    int a = FXEX_ADDR + slot * FXEX_BYTES;
    if (user && EEPROM.read(a) == FXEX_MAGIC) {
        paramChoRate  = 0.1f + (EEPROM.read(a + 1) / 255.0f) * 4.9f;
        paramChoDepth = EEPROM.read(a + 2) / 255.0f;
        paramChoMix   = EEPROM.read(a + 3) / 255.0f;
        paramCrBits   = clampF((float)EEPROM.read(a + 4), 4.0f, 16.0f);
        paramCrRate   = clampF((float)EEPROM.read(a + 5), 1.0f, 24.0f);
        paramCrMix    = EEPROM.read(a + 6) / 255.0f;
        return;
    }
    paramChoRate = 0.8f; paramChoDepth = 0.5f; paramChoMix = 0.0f;
    paramCrBits = 16.0f; paramCrRate = 1.0f;  paramCrMix = 0.0f;
    if (!user) {
    }
}

int presetSlotAddr(int i) { return EE_HEADER + i * PRESET_SLOT_BYTES; }

const char* presetName(int i) {
    if (slotHasName[i]) return slotName[i];
    return slotIsUser[i] ? "USER PATCH" : FACTORY[i].name;
}

static uint16_t presetCrc(PresetData &d) {
    uint16_t saved = d.crc;
    d.crc = 0;
    const uint8_t *p = (const uint8_t*)&d;
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < sizeof(PresetData); i++) {
        c ^= p[i];
        c = (uint16_t)((c << 5) | (c >> 11));
        c = (uint16_t)(c + 0x1D3);
    }
    d.crc = saved;
    return c;
}

bool presetReadSlot(int i, PresetData &d) {
    EEPROM.get(presetSlotAddr(i), d);
    if (d.magic != PRESET_MAGIC) return false;
    return d.crc == presetCrc(d);
}

void presetCapture(PresetData &d) {
    memset(&d, 0, sizeof(d));
    d.magic = PRESET_MAGIC;
    d.wave = (uint8_t)currentWave;
    d.hasPattern = (uint8_t)(1 | ((seqMode & 3) << 4));
    d.reserved = subMode;
    d.bpm = (uint16_t)seqBpm;
    d.attack = paramAttack;   d.decay = paramDecay;   d.sustain = paramSustain; d.release = paramRelease;
    d.cutoff = paramCutoff;   d.reso = paramReso;     d.filtAmt = paramFiltAmt;
    d.filtAtt = paramFiltAtt; d.filtDec = paramFiltDec;
    d.lfoRate = paramLfoRate; d.lfoDepth = paramLfoDepth; d.detune = paramDetune; d.volume = paramVolume;
    d.dlyTime = paramDelayTime; d.dlyFb = paramDelayFb; d.dlyMix = paramDelayMix;
    d.revSize = paramRevSize;   d.revDamp = paramRevDamp; d.revMix = paramRevMix;
    d.pwm = paramPwm; d.pwmMod = paramPwmMod; d.subLevel = paramSubLevel;
    d.pitchAmt = paramPitchAmt; d.pitchDec = paramPitchDecMs; d.velFilt = paramVelFilt; d.swing = paramSwing;
    for (int i = 0; i < SEQ_STEPS; i++) {     // padrão A (os demais vão no registro "bank")
        d.semi[i] = seqBank[0][i].semi;
        d.oct[i]  = seqBank[0][i].oct;
        d.flags[i] = (seqBank[0][i].gate ? 1 : 0) | (seqBank[0][i].accent ? 2 : 0) | (seqBank[0][i].slide ? 4 : 0);
    }
}

void presetFromFactory(int i, PresetData &d) {
    const FactoryPreset &f = FACTORY[i];
    memset(&d, 0, sizeof(d));
    d.magic = PRESET_MAGIC;
    d.wave = f.wave;
    d.hasPattern = 0;
    d.reserved = 0;
    d.attack = f.a; d.decay = f.d; d.sustain = f.s; d.release = f.r;
    d.cutoff = f.cut; d.reso = f.res; d.filtAmt = f.fAmt; d.filtAtt = f.fAtt; d.filtDec = f.fDec;
    d.lfoRate = f.lfoR; d.lfoDepth = f.lfoD; d.detune = f.det; d.volume = f.vol;
    d.dlyTime = f.dT; d.dlyFb = f.dF; d.dlyMix = f.dM;
    d.revSize = f.rS; d.revDamp = f.rD; d.revMix = f.rM;
    d.pwm = f.pwm; d.pwmMod = f.pwmMod; d.subLevel = f.sub;
    d.pitchAmt = f.pAmt; d.pitchDec = f.pDecMs; d.velFilt = f.vel; d.swing = 0.0f;
}

void presetApply(const PresetData &d) {
    int w = d.wave;
    if (w < 0) w = 0;
    if (w >= (int)WAVE_COUNT) w = (int)WAVE_COUNT - 1;
    currentWave = (Waveform)w;

    paramAttack  = clampF(d.attack,  0.001f, 2.0f);
    paramDecay   = clampF(d.decay,   0.001f, 2.0f);
    paramSustain = clampF(d.sustain, 0.0f,   1.0f);
    paramRelease = clampF(d.release, 0.001f, 3.0f);
    paramCutoff  = clampF(d.cutoff,  0.05f,  1.0f);
    paramReso    = clampF(d.reso,    0.0f,   1.0f);
    paramFiltAmt = clampF(d.filtAmt, 0.0f,   1.0f);
    paramFiltAtt = clampF(d.filtAtt, 0.001f, 3.0f);
    paramFiltDec = clampF(d.filtDec, 0.001f, 3.0f);
    paramLfoRate = clampF(d.lfoRate, 0.1f,   20.0f);
    paramLfoDepth = clampF(d.lfoDepth, 0.0f, 1.0f);
    paramDetune  = clampF(d.detune,  0.0f,   1.0f);
    paramVolume  = clampF(d.volume,  0.0f,   1.0f);
    paramDelayTime = clampF(d.dlyTime, 40.0f, 500.0f);
    paramDelayFb   = clampF(d.dlyFb,   0.0f,  0.90f);
    paramDelayMix  = clampF(d.dlyMix,  0.0f,  1.0f);
    paramRevSize   = clampF(d.revSize, 0.0f,  1.0f);
    paramRevDamp   = clampF(d.revDamp, 0.0f,  1.0f);
    paramRevMix    = clampF(d.revMix,  0.0f,  1.0f);
    paramPwm       = clampF(d.pwm,     0.05f, 0.95f);
    paramPwmMod    = clampF(d.pwmMod,  0.0f,  1.0f);
    paramSubLevel  = clampF(d.subLevel, 0.0f, 1.0f);
    paramPitchAmt  = clampF(d.pitchAmt, 0.0f, 1.0f);
    paramPitchDecMs = clampF(d.pitchDec, 5.0f, 800.0f);
    paramVelFilt   = clampF(d.velFilt, 0.0f,  1.0f);
    subMode = d.reserved ? 1 : 0;

    if ((d.hasPattern & 1) && presetLoadPattern) {
        for (int i = 0; i < SEQ_STEPS; i++) {
            int semi = d.semi[i]; if (semi > 12) semi = 12;
            int oct = d.oct[i];   if (oct < -1) oct = -1; if (oct > 2) oct = 2;
            seqBank[0][i].semi = (uint8_t)semi;
            seqBank[0][i].oct = (int8_t)oct;
            seqBank[0][i].gate   = (d.flags[i] & 1) != 0;
            seqBank[0][i].accent = (d.flags[i] & 2) != 0;
            seqBank[0][i].slide  = (d.flags[i] & 4) != 0;
            seqBank[0][i].probIdx = 0;
            seqBank[0][i].ratchIdx = 0;
        }
        seqBpm = constrain((int)d.bpm, 40, 300);
        seqMode = (uint8_t)((d.hasPattern >> 4) & 3);
        paramSwing = clampF(d.swing, 0.0f, 1.0f);
        seqUpdateTiming();
    }
    syncKnobsFromParams();
}

// ---- padrões A-D + chain de cada slot (2 bytes por passo) ----
const int     BANK_ADDR = 2048;
const int     BANK_BYTES = 136;
const uint8_t BANK_MAGIC = 0xB4;
const int     MM_ADDR = 3200;
const int     MM_BYTES = 20;
const uint8_t MM_MAGIC = 0xA7;

void bankWrite(int slot) {
    int a = BANK_ADDR + slot * BANK_BYTES;
    EEPROM.write(a, BANK_MAGIC);
    uint8_t len = seqChainLen;
    EEPROM.write(a + 1, (uint8_t)((seqChainOn ? 0x80 : 0) | (len & 15)));
    for (int h = 0; h < 2; h++) {
        uint8_t c = 0;
        for (int k = 0; k < 4; k++) c |= (uint8_t)((seqChain[h * 4 + k] & 3) << (k * 2));
        EEPROM.write(a + 2 + h, c);
    }
    int p = a + 4;
    for (int pat = 0; pat < PATTERN_COUNT; pat++) {
        for (int i = 0; i < SEQ_STEPS; i++) {
            const SeqStep &s = seqBank[pat][i];
            uint8_t b0 = (uint8_t)((s.semi & 15) | (((s.oct + 1) & 3) << 4) | (s.gate ? 0x40 : 0) | (s.accent ? 0x80 : 0));
            uint8_t b1 = (uint8_t)((s.slide ? 1 : 0) | ((s.probIdx & 3) << 1) | ((s.ratchIdx & 3) << 3));
            EEPROM.write(p++, b0);
            EEPROM.write(p++, b1);
        }
    }
}

bool bankRead(int slot) {
    int a = BANK_ADDR + slot * BANK_BYTES;
    if (EEPROM.read(a) != BANK_MAGIC) return false;
    uint8_t h = EEPROM.read(a + 1);
    uint8_t len = h & 15;
    seqChainLen = (len < 1) ? 1 : ((len > 8) ? 8 : len);
    seqChainOn = (h & 0x80) != 0;
    for (int hh = 0; hh < 2; hh++) {
        uint8_t c = EEPROM.read(a + 2 + hh);
        for (int k = 0; k < 4; k++) seqChain[hh * 4 + k] = (c >> (k * 2)) & 3;
    }
    int p = a + 4;
    for (int pat = 0; pat < PATTERN_COUNT; pat++) {
        for (int i = 0; i < SEQ_STEPS; i++) {
            uint8_t b0 = EEPROM.read(p++);
            uint8_t b1 = EEPROM.read(p++);
            SeqStep &s = seqBank[pat][i];
            int semi = b0 & 15; if (semi > 12) semi = 12;
            s.semi = (uint8_t)semi;
            s.oct = (int8_t)(((b0 >> 4) & 3) - 1);
            s.gate = (b0 & 0x40) != 0;
            s.accent = (b0 & 0x80) != 0;
            s.slide = (b1 & 1) != 0;
            s.probIdx = (b1 >> 1) & 3;
            s.ratchIdx = (b1 >> 3) & 3;
        }
    }
    return true;
}

void bankInvalidate(int slot) { EEPROM.write(BANK_ADDR + slot * BANK_BYTES, 0); }

// ---- matriz de modulação de cada slot (1 byte com sinal por célula) ----
void mmWrite(int slot) {
    int a = MM_ADDR + slot * MM_BYTES;
    EEPROM.write(a, MM_MAGIC);
    for (int i = 0; i < 16; i++) {
        int v = (int)(clampF(mmAmt[i], -1.0f, 1.0f) * 127.0f + ((mmAmt[i] >= 0.0f) ? 0.5f : -0.5f));
        EEPROM.write(a + 1 + i, (uint8_t)(int8_t)v);
    }
}

void mmRead(int slot, bool user) {
    int a = MM_ADDR + slot * MM_BYTES;
    if (user && EEPROM.read(a) == MM_MAGIC) {
        for (int i = 0; i < 16; i++) mmAmt[i] = clampF((float)(int8_t)EEPROM.read(a + 1 + i) / 127.0f, -1.0f, 1.0f);
    } else {
        for (int i = 0; i < 16; i++) mmAmt[i] = 0.0f;
    }
}

void mmInvalidate(int slot) { EEPROM.write(MM_ADDR + slot * MM_BYTES, 0); }

// ---- FM de cada slot (1 byte por parâmetro) ----
const int     FM_ADDR = 3400;
const int     FM_BYTES = 8;
const uint8_t FM_MAGIC = 0xF4;

void fmWrite(int slot) {
    int a = FM_ADDR + slot * FM_BYTES;
    EEPROM.write(a, FM_MAGIC);
    EEPROM.write(a + 1, (uint8_t)constrain((int)(paramFmAmt * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 2, (uint8_t)constrain((int)(paramFmRatio * 16.0f + 0.5f), 4, 192));
    EEPROM.write(a + 3, (uint8_t)constrain((int)(sqrtf(paramFmDecMs) * 5.0f + 0.5f), 0, 255));
    EEPROM.write(a + 4, (uint8_t)constrain((int)(paramFmEnv * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 5, (uint8_t)constrain((int)(paramFmFb * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 6, fmSnap ? 1 : 0);
}

void fmRead(int slot, bool user) {
    int a = FM_ADDR + slot * FM_BYTES;
    if (user && EEPROM.read(a) == FM_MAGIC) {
        paramFmAmt   = EEPROM.read(a + 1) / 255.0f;
        paramFmRatio = clampF(EEPROM.read(a + 2) / 16.0f, 0.5f, 12.0f);
        float d = EEPROM.read(a + 3) / 5.0f;
        paramFmDecMs = clampF(d * d, 5.0f, 2000.0f);
        paramFmEnv   = EEPROM.read(a + 4) / 255.0f;
        paramFmFb    = EEPROM.read(a + 5) / 255.0f;
        fmSnap       = (EEPROM.read(a + 6) != 0);
    } else {
        paramFmAmt = 0.0f; paramFmRatio = 2.0f; paramFmDecMs = 200.0f;
        paramFmEnv = 0.5f; paramFmFb = 0.0f; fmSnap = true;
    }
}

void fmInvalidate(int slot) { EEPROM.write(FM_ADDR + slot * FM_BYTES, 0); }

// ---- OSC2 + UNISON de cada slot ----
const int     O2_ADDR = 3500;
const int     O2_BYTES = 12;
const uint8_t O2_MAGIC = 0xD2;

void o2Write(int slot) {
    int a = O2_ADDR + slot * O2_BYTES;
    EEPROM.write(a, O2_MAGIC);
    EEPROM.write(a + 1, (uint8_t)constrain((int)(paramO2Lvl * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 2, (uint8_t)constrain((int)(paramO2Semi + 24.5f), 0, 48));
    EEPROM.write(a + 3, (uint8_t)constrain((int)(paramO2Det + 100.5f), 0, 200));
    EEPROM.write(a + 4, (uint8_t)((o2Wave & 3) | (o2Sync ? 8 : 0) | ((uniSel & 3) << 4)));
    EEPROM.write(a + 5, (uint8_t)constrain((int)(paramUniDet * 255.0f + 0.5f), 0, 255));
}

void o2Read(int slot, bool user) {
    int a = O2_ADDR + slot * O2_BYTES;
    if (user && EEPROM.read(a) == O2_MAGIC) {
        paramO2Lvl  = EEPROM.read(a + 1) / 255.0f;
        paramO2Semi = (float)((int)EEPROM.read(a + 2) - 24);
        paramO2Det  = (float)((int)EEPROM.read(a + 3) - 100);
        uint8_t m = EEPROM.read(a + 4);
        o2Wave = m & 3; o2Sync = (m & 8) != 0; uniSel = (m >> 4) & 3;
        paramUniDet = EEPROM.read(a + 5) / 255.0f;
    } else {
        paramO2Lvl = 0.0f; paramO2Semi = 0.0f; paramO2Det = 0.0f;
        o2Wave = 0; o2Sync = false; uniSel = 0; paramUniDet = 0.4f;
    }
}

void o2Invalidate(int slot) { EEPROM.write(O2_ADDR + slot * O2_BYTES, 0); }

// ---- WAVETABLE de cada slot (banco, posição, LFO) ----
const int     WT_ADDR = 3720;
const int     WT_BYTES = 4;
const uint8_t WT_MAGIC = 0x57;

void wtWrite(int slot) {
    int a = WT_ADDR + slot * WT_BYTES;
    EEPROM.write(a, WT_MAGIC);
    EEPROM.write(a + 1, (uint8_t)(wtBank & 3));
    EEPROM.write(a + 2, (uint8_t)constrain((int)(paramWtPos * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 3, (uint8_t)constrain((int)(paramWtLfo * 255.0f + 0.5f), 0, 255));
}

void wtRead(int slot, bool user) {
    int a = WT_ADDR + slot * WT_BYTES;
    if (user && EEPROM.read(a) == WT_MAGIC) {
        wtBank     = EEPROM.read(a + 1) & 3;
        paramWtPos = EEPROM.read(a + 2) / 255.0f;
        paramWtLfo = EEPROM.read(a + 3) / 255.0f;
    } else {
        wtBank = 0; paramWtPos = 0.0f; paramWtLfo = 0.0f;
    }
}

void wtInvalidate(int slot) { EEPROM.write(WT_ADDR + slot * WT_BYTES, 0); }

// ---- LFO (forma/sync), accent, ring mod e wavefolder de cada slot ----
const int     NX_ADDR = 3760;
const int     NX_BYTES = 6;
const uint8_t NX_MAGIC = 0x6E;

void nxWrite(int slot) {
    int a = NX_ADDR + slot * NX_BYTES;
    EEPROM.write(a, NX_MAGIC);
    EEPROM.write(a + 1, (uint8_t)((lfoShape % 5) | (lfoSync ? 8 : 0) | (o2Ring ? 16 : 0)));
    EEPROM.write(a + 2, (uint8_t)constrain((int)(paramAccent * 255.0f + 0.5f), 0, 255));
    EEPROM.write(a + 3, (uint8_t)constrain((int)(paramFold * 255.0f + 0.5f), 0, 255));
}

void nxRead(int slot, bool user) {
    int a = NX_ADDR + slot * NX_BYTES;
    if (user && EEPROM.read(a) == NX_MAGIC) {
        uint8_t m = EEPROM.read(a + 1);
        lfoShape = (m & 7) % 5; lfoSync = (m & 8) != 0; o2Ring = (m & 16) != 0;
        paramAccent = EEPROM.read(a + 2) / 255.0f;
        paramFold   = EEPROM.read(a + 3) / 255.0f;
    } else {
        lfoShape = 0; lfoSync = false; o2Ring = false; paramAccent = 0.4f; paramFold = 0.0f;
    }
}

void nxInvalidate(int slot) { EEPROM.write(NX_ADDR + slot * NX_BYTES, 0); }

// aplica os extras e o groove do preset de fábrica i
void factoryExtraApply(int i, bool withPattern) {
    const FactoryEx &e = FACTORY_EX[i];
    lfoShape = e.lfoShape % 5; lfoSync = e.lfoSync != 0; subMode = e.subSine ? 1 : 0;
    paramAccent = e.accent;
    paramO2Lvl = e.o2Lvl; paramO2Semi = e.o2Semi; paramO2Det = e.o2Det;
    o2Wave = e.o2Wave & 3; o2Ring = e.o2Ring != 0; o2Sync = false;
    uniSel = e.uniSel & 3;
    paramFmAmt = e.fmAmt; paramFmRatio = e.fmRatio; paramFmDecMs = e.fmDec; paramFmEnv = e.fmEnv; paramFmFb = e.fmFb;
    wtBank = e.wtBank & 3; paramWtPos = e.wtPos; paramWtLfo = e.wtLfo;
    paramChoRate = e.choRate; paramChoDepth = e.choDepth; paramChoMix = e.choMix;
    paramCrBits = e.crBits; paramCrRate = e.crRate; paramCrMix = e.crMix;
    if (!withPattern) return;

    for (int k = 0; k < SEQ_STEPS; k++) {
        const char *t = FACTORY_PAT[i][k];
        SeqStep &st = seqBank[0][k];
        st.gate = false; st.accent = false; st.slide = false; st.probIdx = 0; st.ratchIdx = 0;
        if (t[0] == '-') continue;
        static const int8_t NOTE[7] = {9, 11, 0, 2, 4, 5, 7};    // A B C D E F G
        int semi = NOTE[(t[0] - 'A') % 7];
        int j = 1;
        if (t[j] == '#') { semi++; j++; }
        int oct = (t[j] - '0') - 1; j++;
        if (semi >= 12) { semi -= 12; oct++; }
        st.semi = (uint8_t)semi; st.oct = (int8_t)constrain(oct, -1, 2); st.gate = true;
        for (; t[j]; j++) {
            if (t[j] == 'A') st.accent = true;
            if (t[j] == 'S') st.slide = true;
            if (t[j] == 'p') st.probIdx = 1;
            if (t[j] == 'h') st.probIdx = 2;
            if (t[j] == 'q') st.probIdx = 3;
            if (t[j] == 'r') st.ratchIdx = 1;
        }
    }
    seqBpm = constrain(e.bpm, 40, 300);
    seqMode = 0;
    paramSwing = e.swing;
    seqUpdateTiming();
    seqRedrawMask = 0xFFFF;
    seqEditorDirty = true;
}

void presetLoadCore(int i) {
    if (i < 0 || i >= PRESET_COUNT) return;
    PresetData d;
    float drive = 0.0f, glide = 60.0f;
    uint8_t modes = 0;
    bool ok = slotIsUser[i] && presetReadSlot(i, d);
    if (ok) {
        if (!extraRead(i, drive, glide, modes)) { drive = 0.0f; glide = 60.0f; modes = 0; }
    } else {
        slotIsUser[i] = false;
        presetFromFactory(i, d);
        const FactoryPreset &f = FACTORY[i];
        drive = f.drive; glide = f.glide;
        modes = (uint8_t)((f.fmode & 3) | ((f.vmode & 3) << 2) | ((f.fmode & 4) ? 16 : 0));
    }
    presetApply(d);
    if (ok && presetLoadPattern) bankRead(i);
    mmRead(i, ok);
    fmRead(i, ok);
    o2Read(i, ok);
    wtRead(i, ok);
    nxRead(i, ok);
    presetApplyExtra(drive, glide, modes);
    fxExtraLoad(i, ok);
    if (!ok) factoryExtraApply(i, presetLoadPattern);
    presetCur = i;
    snprintf(presetMsg, sizeof(presetMsg), "Loaded: %s", presetName(i));
}

static void presetWriteHeader(int lastSlot) {
    EEPROM.write(0, EE_MAGIC);
    EEPROM.write(1, (uint8_t)lastSlot);
    EEPROM.write(2, presetLoadPattern ? 1 : 0);
    EEPROM.write(3, seqSyncExt ? 1 : 0);
}

void presetSaveSlot(int i) {
    snprintf(presetMsg, sizeof(presetMsg), "Saving...");
    presetRedrawUI();

    PresetData d;
    presetCapture(d);
    d.crc = presetCrc(d);
    EEPROM.put(presetSlotAddr(i), d);
    extraWrite(i);
    fxExtraWrite(i);
    bankWrite(i);
    mmWrite(i);
    fmWrite(i);
    o2Write(i);
    wtWrite(i);
    nxWrite(i);
    presetWriteHeader(i);
    bool ok = EEPROM.commit();

    if (ok) {
        slotIsUser[i] = true;
        presetCur = i;
        snprintf(presetMsg, sizeof(presetMsg), "Saved to slot %d", i + 1);
    } else {
        snprintf(presetMsg, sizeof(presetMsg), "Save failed!");
    }
    presetRedrawUI();
}

void presetFactorySlot(int i) {
    snprintf(presetMsg, sizeof(presetMsg), "Restoring...");
    presetRedrawUI();

    EEPROM.write(presetSlotAddr(i), 0);
    extraInvalidate(i);
    fxExtraInvalidate(i);
    bankInvalidate(i);
    mmInvalidate(i);
    fmInvalidate(i);
    o2Invalidate(i);
    wtInvalidate(i);
    nxInvalidate(i);
    presetWriteHeader(0);
    EEPROM.commit();
    slotIsUser[i] = false;
    slotHasName[i] = false;
    nameSaveAll();
    presetLoadCore(i);
    snprintf(presetMsg, sizeof(presetMsg), "Slot %d = factory", i + 1);
    presetRedrawUI();
}

void padSaveMap() {
    EEPROM.write(PAD_MAP_ADDR, PAD_MAGIC);
    for (int i = 0; i < PAD_COUNT; i++) {
        EEPROM.write(PAD_MAP_ADDR + 1 + i, padCh[i]);
        EEPROM.write(PAD_MAP_ADDR + 1 + PAD_COUNT + i, padNote[i]);
    }
    EEPROM.commit();
}

void padLoadMap() {
    if (EEPROM.read(PAD_MAP_ADDR) != PAD_MAGIC) return;
    for (int i = 0; i < PAD_COUNT; i++) {
        uint8_t c = EEPROM.read(PAD_MAP_ADDR + 1 + i);
        uint8_t n = EEPROM.read(PAD_MAP_ADDR + 1 + PAD_COUNT + i);
        if (c >= 1 && c <= 16 && n < 128) { padCh[i] = c; padNote[i] = n; }
        else                              { padCh[i] = 0; }
    }
}

void mapSave() {
    EEPROM.write(MAP_ADDR, MAP_MAGIC);
    for (int i = 0; i < MAP_COUNT; i++) EEPROM.write(MAP_ADDR + 1 + i, mapCC[i]);
    EEPROM.commit();
}

void mapLoad() {
    if (EEPROM.read(MAP_ADDR) != MAP_MAGIC) return;
    for (int i = 0; i < MAP_COUNT; i++) {
        uint8_t c = EEPROM.read(MAP_ADDR + 1 + i);
        mapCC[i] = (c < 120) ? c : 255;
    }
}

void presetBoot() {
    EEPROM.begin(EE_SIZE);
    seqInitBanks();
    PresetData tmp;
    for (int i = 0; i < PRESET_COUNT; i++) slotIsUser[i] = presetReadSlot(i, tmp);
    nameLoadAll();

    int last = 0;
    if (EEPROM.read(0) == EE_MAGIC) {
        last = EEPROM.read(1);
        if (last >= PRESET_COUNT) last = 0;
        presetLoadPattern = (EEPROM.read(2) != 0);
        seqSyncExt = (EEPROM.read(3) == 1);
    }
    presetLoadCore(last);
    padLoadMap();
    mapLoad();
}

// ============================================================================
// PUBLICAÇÃO DOS PARÂMETROS (roda no core da tela, a cada ~4 ms)
// Converte os floats em inteiros de ponto fixo; o core de áudio só lê esses inteiros.
// ============================================================================
void publishParams() {
    updateEnvCoefs();
    fxUpdateParams();

    pCutBaseQ15  = (int32_t)(clampF(paramCutoff, 0.0f, 1.0f) * 32768.0f);
    pLfoHalfQ15  = (int32_t)(clampF(paramLfoDepth, 0.0f, 1.0f) * 16384.0f);      // profundidade x 0,5
    pFiltAmtQ15  = (int32_t)(clampF(paramFiltAmt, 0.0f, 1.0f) * 32768.0f);
    pVelFiltQ15  = (int32_t)(clampF(paramVelFilt, 0.0f, 1.0f) * 32768.0f);
    pQTargetQ14  = (int32_t)((1.0f - clampF(paramReso, 0.0f, 1.0f) * 1.06f) * 16384.0f);
    if (lfoSync) {                // divisão do BPM: Hz = BPM / (240 x notas inteiras por ciclo)
        float hz = (float)seqBpm / (240.0f * LFO_SYNC_WHOLE[lfoSyncIdx()]);
        pLfoInc32 = (uint32_t)(hz * 134217.728f);
    } else {
        pLfoInc32 = (uint32_t)(clampF(paramLfoRate, 0.1f, 20.0f) * 134217.728f);
    }
    pAccentQ15   = (int32_t)(clampF(paramAccent, 0.0f, 1.0f) * 32768.0f);
    pFoldOn      = (paramFold > 0.01f);
    pFoldGainQ8  = (int32_t)((1.0f + clampF(paramFold, 0.0f, 1.0f) * 7.0f) * 256.0f);
    pPwmBaseQ16  = (int32_t)(clampF(paramPwm, 0.05f, 0.95f) * 65536.0f);
    pPwmModQ16   = (int32_t)(clampF(paramPwmMod, 0.0f, 1.0f) * 0.4f * 65536.0f);

    pDriveOn      = (paramDrive > 0.01f);
    pDriveGainQ8  = (int32_t)((1.0f + clampF(paramDrive, 0.0f, 1.0f) * 7.0f) * 256.0f);
    pDriveCompQ15 = (int32_t)(32768.0f / (1.0f + clampF(paramDrive, 0.0f, 1.0f) * 1.5f));

    float wet = clampF((paramReso - 0.88f) * 8.33f, 0.0f, 1.0f);
    pSelfWetQ15  = (int32_t)(wet * 32768.0f);
    pSeedOn      = (paramReso > 0.93f);

    pSpreadQ16   = (int32_t)((0.006f + clampF(paramDetune, 0.0f, 1.0f) * 0.02f) * 65536.0f);

    float sl = clampF(paramSubLevel, 0.0f, 1.0f);
    pSubGainQ15  = (int32_t)(sl * 0.70f * 32768.0f);
    pMainGainQ15 = 32768 - (int32_t)(sl * 0.35f * 32768.0f);

    pSwingQ8     = (int32_t)(clampF(paramSwing, 0.0f, 1.0f) * 85.0f);

    pWtPosQ16    = (int32_t)(clampF(paramWtPos, 0.0f, 1.0f) * 65535.0f);
    pWtLfoQ15    = (int32_t)(clampF(paramWtLfo, 0.0f, 1.0f) * 32768.0f);

    // matriz de modulação -> inteiros Q15
    bool mmAny = false, mmPit = false;
    for (int i = 0; i < 16; i++) {
        float a = clampF(mmAmt[i], -1.0f, 1.0f);
        pMmQ15[i] = (int32_t)(a * 32768.0f);
        if (fabsf(a) > 0.004f) { mmAny = true; if ((i >> 2) == 1) mmPit = true; }
    }
    pMmOn = mmAny;
    pPitchModOn = mmPit;

    float gm = (paramGlideMs < 1.0f) ? 1.0f : paramGlideMs;
    int gs = (int)(log2f(gm * 32.0f) + 0.5f);
    if (gs < 5) gs = 5;
    if (gs > 14) gs = 14;
    pGlideShift  = gs;

    pPitchDecInc = envIncFor(paramPitchDecMs * 0.001f);
    pPitchOn     = (paramPitchAmt > 0.01f);

    // OSC2
    float o2l = clampF(paramO2Lvl, 0.0f, 1.0f);
    float semi = clampF(paramO2Semi, -24.0f, 24.0f);
    float cents = clampF(paramO2Det, -100.0f, 100.0f);
    pO2MulQ8    = (int32_t)(exp2f((semi + cents / 100.0f) / 12.0f) * 256.0f + 0.5f);
    pO2MainQ15  = 32768 - (int32_t)(o2l * 0.30f * 32768.0f);
    pO2GainQ15  = (int32_t)(o2l * 0.75f * 32768.0f);
    pO2Sync     = o2Sync;
    pO2On       = (o2l > 0.01f);

    // UNISON: 1 / 2 / 3 / 5 cópias; deslocamentos relativos (Q8) de -1..+1
    static const int     UNI_N[4]    = {1, 2, 3, 5};
    static const int32_t UNI_GAIN[4] = {32768, 23170, 18918, 14654};      // ~1/raiz(N)
    static const int16_t UNI_OFF[4][5] = {{0, 0, 0, 0, 0}, {-256, 256, 0, 0, 0}, {-256, 0, 256, 0, 0}, {-256, -128, 0, 128, 256}};
    int us = uniSel & 3;
    pUniN = UNI_N[us];
    pUniGainQ15 = UNI_GAIN[us];
    float spreadFrac = clampF(paramUniDet, 0.0f, 1.0f) * 0.025f;         // até ~ +-43 cents
    for (int k = 0; k < 5; k++) pUniSpreadQ16[k] = (int32_t)(spreadFrac * 65536.0f * (float)UNI_OFF[us][k] / 256.0f);

    // FM
    float fa = clampF(paramFmAmt, 0.0f, 1.0f);
    pFmIdxQ12   = (int32_t)(fa * fa * 2.0f * 4096.0f);                  // 0..2 ciclos
    pFmRatioQ8  = (int32_t)(clampF(paramFmRatio, 0.25f, 12.0f) * 256.0f + 0.5f);
    pFmEnvQ15   = (int32_t)(clampF(paramFmEnv, 0.0f, 1.0f) * 32768.0f);
    pFmFbQ12    = (int32_t)(clampF(paramFmFb, 0.0f, 1.0f) * 1200.0f);
    pFmDecInc   = envIncFor(clampF(paramFmDecMs, 5.0f, 2000.0f) * 0.001f);
    pFmOn       = (pFmIdxQ12 > 4);

    // tabela do pitch envelope: só recalcula quando a quantidade muda (troca atômica de tabela)
    static float lastPitchAmt = -1.0f;
    if (fabsf(paramPitchAmt - lastPitchAmt) > 0.001f) {
        lastPitchAmt = paramPitchAmt;
        uint8_t nxt = pitchTblSel ^ 1;
        float oct = clampF(paramPitchAmt, 0.0f, 1.0f) * 3.0f;
        for (int i = 0; i < 129; i++) {
            pitchMulTbl[nxt][i] = (uint32_t)(exp2f(oct * (float)i * (1.0f / 128.0f)) * 65536.0f);
        }
        pitchTblSel = nxt;
    }
}

// ============================================================================
// ENCODER E TOQUE
// ============================================================================
// ENCODER (lido por interrupção; as leituras no loop1 perdiam passos durante
// os redesenhos da tela). Girar acelera: devagar = passos finos, rápido = grandes.
//   girar                 -> CUTOFF
//   segurar + girar       -> RESSONÂNCIA
//   clique curto          -> PLAY / STOP do sequenciador
//   segurar ~0,8 s parado -> PÂNICO (solta todas as notas e para o sequenciador)
const uint16_t BTN_DEBOUNCE_MS = 25;
const uint16_t ENC_QUIET_MS    = 250;     // botão só vale se o eixo estava parado antes de apertar
const uint16_t BTN_LONG_MS     = 800;
const bool  ENC_INVERT     = false;   // true se o sentido estiver trocado
const float ENC_STEP_FINE  = 0.005f;  // 0,5% por clique (giro lento)
const float ENC_STEP_MID   = 0.012f;
const float ENC_STEP_FAST  = 0.025f;  // giro rápido
const uint8_t ENC_FAST_MS  = 25;      // intervalo entre cliques abaixo disso = rápido
const uint8_t ENC_MID_MS   = 60;

#define R_START      0x0
#define R_CW_FINAL   0x1
#define R_CW_BEGIN   0x2
#define R_CW_NEXT    0x3
#define R_CCW_BEGIN  0x4
#define R_CCW_FINAL  0x5
#define R_CCW_NEXT   0x6
const uint8_t rotaryTable[7][4] = {
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | 0x10},
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | 0x20},
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};
uint8_t rotaryState = R_START;

static int8_t __not_in_flash_func(readRotaryStep)() {
    int pinstate = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
    rotaryState = rotaryTable[rotaryState & 0x0F][pinstate];
    uint8_t result = rotaryState & 0x30;
    rotaryState &= 0x0F;
    if (result == 0x10) return +1;
    if (result == 0x20) return -1;
    return 0;
}

// interrupção nos dois pinos (CHANGE): só acumula os cliques; o loop1 aplica
static volatile int16_t encSteps = 0;
static volatile uint32_t encEdgeMs = 0;     // último movimento em CLK/DT (mesmo sem completar um clique)
void __not_in_flash_func(encoderISR)() {
    encEdgeMs = millis();
    int8_t d = readRotaryStep();
    if (d) encSteps += d;
}

void encoderBegin() {
    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);       // botão entre GP2 e GND
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_DT),  encoderISR, CHANGE);
}

// estado do botão (com debounce)
static bool btnDown = false, btnRaw = false;
static unsigned long btnRawSince = 0, btnPressedAt = 0;
static bool btnTurned = false;           // girou enquanto segurava -> não é clique
static bool btnLongDone = false;
static bool btnQuiet = false;            // o aperto começou com o eixo parado (aperto de verdade)
static int32_t encStepTotal = 0;         // teste do encoder (MOD > VOICE/GLIDE)
static uint16_t encSwNoise = 0;          // apertos ignorados porque vieram durante o giro

// Encoders baratos (e fios longos ou mal presos) fazem o pino do botão "piscar" enquanto o eixo
// gira. Um aperto que começa durante o giro é ignorado: não dá PLAY/STOP nem vira ressonância.
static void encoderButton(unsigned long now) {
    bool raw = (digitalRead(ENC_SW) == LOW);
    if (raw != btnRaw) { btnRaw = raw; btnRawSince = now; }
    if (raw != btnDown && now - btnRawSince >= BTN_DEBOUNCE_MS) {
        btnDown = raw;
        if (btnDown) {                   // apertou
            btnPressedAt = now;
            btnTurned = false;
            btnLongDone = false;
            btnQuiet = (now - encEdgeMs >= ENC_QUIET_MS);
            if (!btnQuiet) encSwNoise++;
        } else if (btnQuiet && !btnTurned && !btnLongDone &&
                   now - encEdgeMs >= 60) {          // soltou: clique curto (e o eixo não mexeu)
            seqCmd = seqRunning ? 2 : 1;
            snprintf(lastMidiMsg, sizeof(lastMidiMsg), seqRunning ? "SEQ STOP" : "SEQ PLAY");
        }
    }
    // segurou parado: pânico (uma vez por toque)
    if (btnDown && btnQuiet && !btnTurned && !btnLongDone && now - btnPressedAt >= BTN_LONG_MS) {
        btnLongDone = true;
        if (seqRunning) seqCmd = 2;
        monoResetReq = true;             // o core de áudio solta todas as vozes
        snprintf(lastMidiMsg, sizeof(lastMidiMsg), "PANIC");
    }
}

void readEncoder() {
    static unsigned long lastStepMs = 0;
    unsigned long now = millis();
    encoderButton(now);

    noInterrupts();
    int16_t n = encSteps;
    encSteps = 0;
    interrupts();
    if (n == 0) return;
    if (ENC_INVERT) n = -n;

    unsigned long perClick = (now - lastStepMs) / (unsigned long)abs(n);   // tempo médio por clique
    lastStepMs = now;
    float step = (perClick < ENC_FAST_MS) ? ENC_STEP_FAST : ((perClick < ENC_MID_MS) ? ENC_STEP_MID : ENC_STEP_FINE);

    encStepTotal += n;
    if (btnDown) btnTurned = true;
    if (btnDown && btnQuiet) {           // segurando de verdade: ressonância
        paramReso = constrain(paramReso + (float)n * step, 0.0f, 1.0f);
    } else {
        paramCutoff = constrain(paramCutoff + (float)n * step, 0.05f, 1.0f);
    }
    syncKnobsFromParams();               // mantém os knobs MIDI coerentes
}

// ---- calibração do touch: leitura bruta nas bordas da tela (x=0/320, y=0/240) ----
// Sem calibração gravada vale o padrão TS_MIN/TS_MAX. Grava em CAL_ADDR.
const int     CAL_ADDR  = 3700;
const uint8_t CAL_MAGIC = 0xCA;
struct TouchCal { int16_t x0, x1, y0, y1; };
TouchCal touchCal = {TS_MAXX, TS_MINX, TS_MAXY, TS_MINY};

void touchCalLoad() {
    if (EEPROM.read(CAL_ADDR) != CAL_MAGIC) return;
    TouchCal c;
    EEPROM.get(CAL_ADDR + 1, c);
    // descarta dados absurdos (ex.: EEPROM corrompida): exige faixa mínima em cada eixo
    if (abs(c.x1 - c.x0) < 1000 || abs(c.y1 - c.y0) < 1000) return;
    touchCal = c;
}

void touchCalSave() {
    EEPROM.write(CAL_ADDR, CAL_MAGIC);
    EEPROM.put(CAL_ADDR + 1, touchCal);
    EEPROM.commit();
}

bool getTouch(int &sx, int &sy) {
    if (!ts.touched()) return false;
    TS_Point p = ts.getPoint();
    sx = map(p.x, touchCal.x0, touchCal.x1, 0, 320);
    sy = map(p.y, touchCal.y0, touchCal.y1, 0, 240);
    sx = constrain(sx, 0, 319);
    sy = constrain(sy, 0, 239);
    return true;
}

// ============================================================================
// GRÁFICOS E INTERFACE
// ============================================================================
const char* waveShortName(Waveform w) {
    switch (w) {
        case WAVE_SAW: return "SAW";    case WAVE_SQUARE:   return "SQR";
        case WAVE_TRIANGLE: return "TRI"; case WAVE_SINE:   return "SIN";
        case WAVE_PULSE: return "PLS";  case WAVE_SUPERSAW: return "SSW";
        case WAVE_NOISE: return "NSE";  case WAVE_TABLE:    return "WT";
        default: return "?";
    }
}
float waveShapeFor(Waveform w, float t) {
    switch (w) {
        case WAVE_TABLE: {           // ícone mostra o quadro mais próximo da posição base
            int f = constrain((int)(paramWtPos * 7.0f + 0.5f), 0, 7);
            return (float)wtData[0][wtBank & 3][f][(int)(t * 256.0f) & 255] / 32768.0f;
        }
        case WAVE_SAW:      return t * 2.0f - 1.0f;
        case WAVE_SQUARE:   return (t >= 0.5f) ? 1.0f : -1.0f;
        case WAVE_PULSE:    return (t >= 0.3f) ? 1.0f : -1.0f;
        case WAVE_TRIANGLE: { float saw = t * 2.0f - 1.0f; return fabsf(saw) * 2.0f - 1.0f; }
        case WAVE_SUPERSAW: { float saw = t * 2.0f - 1.0f; return saw * 0.9f; }
        case WAVE_NOISE:    return (float)random(-100, 100) / 100.0f;
        case WAVE_SINE:
        default: return sinf(2.0f * (float)M_PI * t);
    }
}

void drawScope(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);

    int gx = 8, gy = 4, gw = w - 16, gh = h - 8;
    int mid = gy + gh / 2;
    panelCanvas.drawFastHLine(gx, mid, gw, COL_MUTED);

    static int16_t snap[SCOPE_LEN];
    uint16_t wi = scopeW;
    for (int i = 0; i < SCOPE_LEN; i++) snap[i] = scopeBuf[(wi + i) & (SCOPE_LEN - 1)];

    int start = 0;
    for (int i = 1; i < SCOPE_LEN - gw; i++) {
        if (snap[i - 1] < -64 && snap[i] >= 0) { start = i; break; }
    }

    int prevX = gx, prevY = mid;
    for (int i = 0; i < gw; i++) {
        int32_t s = snap[start + i];
        int y = mid - (int)((s * (int32_t)gh) / 32768);
        if (y < gy) y = gy;
        if (y > gy + gh) y = gy + gh;
        int px = gx + i;
        if (i > 0) panelCanvas.drawLine(prevX, prevY, px, y, COL_ACCENT);
        prevX = px; prevY = y;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

const int WAVE_ICON_X = 8, WAVE_ICON_Y = 38, WAVE_ICON_W = 70, WAVE_ICON_H = 36;
const int WAVE_ICON_GAP_X = 8, WAVE_ICON_GAP_Y = 6;

void waveIconRect(int index, int &x, int &y) {
    int col = index % 4, row = index / 4;
    x = WAVE_ICON_X + col * (WAVE_ICON_W + WAVE_ICON_GAP_X);
    y = WAVE_ICON_Y + row * (WAVE_ICON_H + WAVE_ICON_GAP_Y);
}

int lastWaveDisplay = -1;

void drawWaveIcon(int index) {
    int x, y; waveIconRect(index, x, y);
    bool selected = ((int)currentWave == index);
    tft.fillRoundRect(x, y, WAVE_ICON_W, WAVE_ICON_H, RADIUS, COL_BG);
    tft.drawRoundRect(x, y, WAVE_ICON_W, WAVE_ICON_H, RADIUS, selected ? COL_BORDER : COL_MUTED);

    int gx = x + 5, gy = y + 4, gw = WAVE_ICON_W - 10, gh = WAVE_ICON_H - 18;
    int prevX = gx, prevY = gy + gh / 2;
    uint16_t lineCol = selected ? COL_ACCENT : COL_MUTED;

    for (int i = 0; i < gw; i++) {
        float t = (float)i / gw;
        float v = waveShapeFor((Waveform)index, t);
        int px = gx + i, py = gy + gh / 2 - (int)(v * (gh / 2));
        if (i > 0) tft.drawLine(prevX, prevY, px, py, lineCol);
        prevX = px; prevY = py;
    }
    tft.setTextSize(1);
    tft.setTextColor(selected ? COL_ACCENT : COL_MUTED);
    tft.setCursor(x + 5, y + WAVE_ICON_H - 10);
    tft.print(waveShortName((Waveform)index));
}

void drawWaveGrid() { for (int i = 0; i < (int)WAVE_COUNT; i++) drawWaveIcon(i); }

bool touchWaveGrid(int tx, int ty) {
    for (int i = 0; i < (int)WAVE_COUNT; i++) {
        int x, y; waveIconRect(i, x, y);
        if (tx >= x && tx < x + WAVE_ICON_W && ty >= y && ty < y + WAVE_ICON_H) {
            currentWave = (Waveform)i;
            return true;
        }
    }
    return false;
}

// ---------------- ENVELOPE (gráfico arrastável) ----------------
const int   ENV_PX0 = 8, ENV_PY0 = 38, ENV_PH = 75;
const int   ENV_GX0 = 6, ENV_GY0 = 20;
const int   ENV_GW  = PANEL_W - 12;
const int   ENV_GH  = ENV_PH - 26;
const float ENV_SEG = ENV_GW * 0.28f;

AdsrPts adsrPoints() {
    AdsrPts p;
    float aW = constrain(paramAttack  / 2.0f, 0.03f, 1.0f) * ENV_SEG;
    float dW = constrain(paramDecay   / 2.0f, 0.03f, 1.0f) * ENV_SEG;
    float sW = ENV_GW * 0.18f;
    float rW = constrain(paramRelease / 3.0f, 0.03f, 1.0f) * ENV_SEG;
    p.xa = ENV_GX0 + (int)aW;
    p.xd = p.xa + (int)dW;
    p.xs = p.xd + (int)sW;
    p.xr = p.xs + (int)rW;
    p.yTop = ENV_GY0 + 4;
    p.yBot = ENV_GY0 + ENV_GH - 4;
    p.ySus = p.yBot - (int)(paramSustain * (ENV_GH - 8));
    return p;
}

void drawADSR(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 6);
    panelCanvas.print("AMP ENVELOPE - DRAG POINTS");

    AdsrPts p = adsrPoints();
    panelCanvas.drawLine(ENV_GX0, p.yBot, p.xa, p.yTop, COL_ACCENT2);
    panelCanvas.drawLine(p.xa, p.yTop, p.xd, p.ySus, COL_ACCENT2);
    panelCanvas.drawLine(p.xd, p.ySus, p.xs, p.ySus, COL_ACCENT2);
    panelCanvas.drawLine(p.xs, p.ySus, p.xr, p.yBot, COL_ACCENT2);

    panelCanvas.fillCircle(p.xa, p.yTop, 5, COL_ACCENT);
    panelCanvas.fillCircle(p.xa, p.yTop, 2, COL_BG);
    panelCanvas.fillCircle(p.xd, p.ySus, 5, COL_ACCENT);
    panelCanvas.fillCircle(p.xd, p.ySus, 2, COL_BG);
    panelCanvas.fillCircle(p.xr, p.yBot, 5, COL_ACCENT);
    panelCanvas.fillCircle(p.xr, p.yBot, 2, COL_BG);

    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

// ---------------- FILTRO (resposta real, eixo log/dB, arrastável) ----------------
const int   FLT_PX0 = 8, FLT_PY0 = 38, FLT_PH = 75;
const int   FLT_GX0 = 8;
const int   FLT_GW  = PANEL_W - 16;
const int   FLT_YTOP = 18, FLT_YBOT = FLT_PH - 5;
const float FLT_DB_MAX = 36.0f, FLT_DB_MIN = -36.0f;
const float FLT_FMIN = 20.0f, FLT_FMAX = 16000.0f;
const float BP_GAIN_DB = 3.5f;

float cutoffParamToHz(float c)  { return 120.0f + c * c * 8000.0f; }
float cutoffHzToParam(float hz) { float v = (hz - 120.0f) / 8000.0f; if (v < 0.0f) v = 0.0f; return sqrtf(v); }
float fltFreqToX(float hz) { return (float)FLT_GX0 + (float)FLT_GW * (log10f(hz / FLT_FMIN) / log10f(FLT_FMAX / FLT_FMIN)); }
float fltXToFreq(float x)  { return FLT_FMIN * powf(FLT_FMAX / FLT_FMIN, (x - (float)FLT_GX0) / (float)FLT_GW); }
float fltDbToY(float db) {
    db = constrain(db, FLT_DB_MIN, FLT_DB_MAX);
    return (float)FLT_YTOP + (FLT_DB_MAX - db) * (float)(FLT_YBOT - FLT_YTOP) / (FLT_DB_MAX - FLT_DB_MIN);
}
float fltYToDb(float y) {
    return FLT_DB_MAX - (y - (float)FLT_YTOP) * (FLT_DB_MAX - FLT_DB_MIN) / (float)(FLT_YBOT - FLT_YTOP);
}
float resoToQ(float reso) { return 1.0f - reso * 1.06f; }
float resoToPeakDb(float reso) {
    float q = resoToQ(reso);
    if (q < 0.016f) q = 0.016f;
    return -20.0f * log10f(q);
}
float dbToReso(float db) {
    if (db <= 0.0f) return 0.0f;
    if (db >= 35.0f) return 1.0f;
    float q = powf(10.0f, -db / 20.0f);
    return constrain((1.0f - q) / 1.06f, 0.0f, 1.0f);
}

const char* FILTER_NAMES[3] = {"LP", "BP", "HP"};
const float FLT_Q2 = 1.15f;        // amortecimento do 2º estágio (igual ao do áudio)
const float BP2_GAIN_DB = 1.0f;

// contribuição do 2º estágio no ponto de corte (para posicionar a alça do gráfico)
float slopeOffsetDb() {
    if (!filtSlope) return 0.0f;
    float o = -20.0f * log10f(FLT_Q2);
    if (filtMode == 1) o += BP2_GAIN_DB;
    return o;
}

void drawFilterResponse(int x0, int y0, int h) {
    const int w = PANEL_W;
    const uint8_t fm = filtMode;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char title[40];
    const bool slope24 = (filtSlope != 0);
    snprintf(title, sizeof(title), "FILTER %s %ddB - DRAG: CUTOFF / RES", FILTER_NAMES[fm > 2 ? 0 : fm], slope24 ? 24 : 12);
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 6);
    panelCanvas.print(title);

    int y0dB = (int)fltDbToY(0.0f);
    for (int x = FLT_GX0; x < FLT_GX0 + FLT_GW; x += 4) panelCanvas.drawPixel(x, y0dB, COL_MUTED);
    const float marks[3] = {100.0f, 1000.0f, 10000.0f};
    for (int m = 0; m < 3; m++) {
        int mx = (int)fltFreqToX(marks[m]);
        for (int y = FLT_YTOP; y < FLT_YBOT; y += 4) panelCanvas.drawPixel(mx, y, COL_MUTED);
    }

    float fc = cutoffParamToHz(paramCutoff);
    float q = resoToQ(paramReso);
    if (q < 0.016f) q = 0.016f;
    const int STEP = 3;
    float ratio = powf(FLT_FMAX / FLT_FMIN, (float)STEP / (float)FLT_GW);
    float freq = FLT_FMIN;
    int prevX = FLT_GX0, prevY = y0dB;

    for (int i = 0; i <= FLT_GW; i += STEP) {
        int px = FLT_GX0 + i;
        float r = freq / fc;
        float r2 = r * r;
        float den = (1.0f - r2) * (1.0f - r2) + q * q * r2;
        float num = 1.0f;
        if (fm == 1) num = r2;
        else if (fm == 2) num = r2 * r2;
        float db = 10.0f * log10f(num / den) + ((fm == 1) ? BP_GAIN_DB : 0.0f);
        if (slope24) {
            float den2 = (1.0f - r2) * (1.0f - r2) + FLT_Q2 * FLT_Q2 * r2;
            db += 10.0f * log10f(num / den2) + ((fm == 1) ? BP2_GAIN_DB : 0.0f);
        }
        int py = (int)fltDbToY(db);
        if (i > 0) panelCanvas.drawLine(prevX, prevY, px, py, COL_ACCENT2);
        prevX = px; prevY = py;
        freq *= ratio;
    }

    int hx = (int)fltFreqToX(fc);
    int hy = (int)fltDbToY(resoToPeakDb(paramReso) + ((fm == 1) ? BP_GAIN_DB : 0.0f) + slopeOffsetDb());
    panelCanvas.fillCircle(hx, hy, 5, COL_ACCENT);
    panelCanvas.fillCircle(hx, hy, 2, COL_BG);

    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

// forma do LFO em t (0..1) para desenhar; n = índice do ciclo (S&H)
float lfoShapeF(float t, int n) {
    switch (lfoShape) {
        case 1:  return (t < 0.5f) ? (t * 4.0f - 1.0f) : (3.0f - t * 4.0f);
        case 2:  return t * 2.0f - 1.0f;
        case 3:  return (t < 0.5f) ? -1.0f : 1.0f;
        case 4: { uint32_t h = (uint32_t)(n + 7) * 2654435761u; return (float)((h >> 16) & 0xFFFF) / 32768.0f - 1.0f; }
        default: return sinf(t * 6.28318f);
    }
}

const char* LFO_SHAPE_NAMES[5] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "S&H"};

void drawLfoView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[44];
    float hz = lfoSync ? (float)seqBpm / (240.0f * LFO_SYNC_WHOLE[lfoSyncIdx()]) : paramLfoRate;
    if (lfoSync) snprintf(t, sizeof(t), "LFO %s  %s @ %d BPM", LFO_SHAPE_NAMES[lfoShape % 5], LFO_SYNC_NAMES[lfoSyncIdx()], seqBpm);
    else         snprintf(t, sizeof(t), "LFO %s  %.1f Hz", LFO_SHAPE_NAMES[lfoShape % 5], paramLfoRate);
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(t);

    float lfoPh = (float)(lfoPhase32 >> 8) * (1.0f / 16777216.0f);     // fase do LFO (0..1)
    int gx0 = 6, gy0 = 16, gw = w - 12, gh = h - 20;
    int mid = gy0 + gh / 2;
    float amp = paramLfoDepth * (gh * 0.45f) + 1.0f;
    float cyclesPerPx = (0.03f + constrain(hz / 20.0f, 0.0f, 1.0f) * 0.35f) / 6.28318f;
    int prevX = gx0 + 2, prevY = mid;
    for (int i = 0; i < gw - 4; i++) {
        float c = (float)i * cyclesPerPx + lfoPh;
        int n = (int)floorf(c);
        float y = mid - lfoShapeF(c - (float)n, n) * amp;
        int px = gx0 + 2 + i;
        if (i > 0) panelCanvas.drawLine(prevX, prevY, px, (int)y, COL_ACCENT2);
        prevX = px; prevY = (int)y;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawPitchView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[40];
    snprintf(t, sizeof(t), "PITCH ENV  +%d st  %d ms", (int)(paramPitchAmt * 36.0f + 0.5f), (int)(paramPitchDecMs + 0.5f));
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 6);
    panelCanvas.print(t);

    int gx0 = 8, gw = w - 16, yTop = 22, yBot = h - 8;
    panelCanvas.drawFastHLine(gx0, yBot, gw, COL_MUTED);
    int prevX = gx0, prevY = yBot - (int)(paramPitchAmt * (yBot - yTop));
    for (int i = 0; i < gw; i++) {
        float ms = (float)i / (float)gw * 800.0f;
        float frac = 1.0f - ms / paramPitchDecMs;
        if (frac < 0.0f) frac = 0.0f;
        int y = yBot - (int)(paramPitchAmt * frac * (yBot - yTop));
        int px = gx0 + i;
        if (i > 0) panelCanvas.drawLine(prevX, prevY, px, y, COL_ACCENT2);
        prevX = px; prevY = y;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawFenvView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[48];
    snprintf(t, sizeof(t), "FILTER ENV  A %d ms  D %d ms  AMT %d%%", (int)(paramFiltAtt * 1000.0f + 0.5f),
             (int)(paramFiltDec * 1000.0f + 0.5f), (int)(paramFiltAmt * 100.0f + 0.5f));
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 6);
    panelCanvas.print(t);

    // eixo de 0 a 3 s: sobe no ataque e cai no decay (altura = AMOUNT)
    int gx0 = 8, gw = w - 16, yTop = 18, yBot = h - 6;
    panelCanvas.drawFastHLine(gx0, yBot, gw, COL_MUTED);
    float span = 3.0f;
    int xa = gx0 + (int)(paramFiltAtt / span * gw);
    int xd = xa + (int)(paramFiltDec / span * gw);
    if (xd > gx0 + gw) xd = gx0 + gw;
    int yPk = yBot - (int)(paramFiltAmt * (yBot - yTop));
    panelCanvas.drawLine(gx0, yBot, xa, yPk, COL_ACCENT2);
    int prevX = xa, prevY = yPk;
    for (int x = xa + 1; x <= xd; x++) {               // decay exponencial até o fim do tempo
        float f = (float)(x - xa) / (float)max(1, xd - xa);
        int y = yBot - (int)((yBot - yPk) * expf(-4.0f * f));
        panelCanvas.drawLine(prevX, prevY, x, y, COL_ACCENT2);
        prevX = x; prevY = y;
    }
    if (prevX < gx0 + gw) panelCanvas.drawLine(prevX, prevY, gx0 + gw, yBot, COL_ACCENT2);
    panelCanvas.fillCircle(xa, yPk, 3, COL_ACCENT);
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawOsc2Graph(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[44];
    snprintf(t, sizeof(t), "PULSE %d%%  SUB %d%% (%s)", (int)(paramPwm * 100.0f + 0.5f),
             (int)(paramSubLevel * 100.0f + 0.5f), subMode ? "SIN" : "SQR");
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(t);

    int gx0 = 8, gw = w - 16, top = 18, bot = h - 5;
    int mid = (top + bot) / 2, half = (bot - top) / 2;
    int prevY = 0, prevSY = mid;
    for (int i = 0; i < gw; i++) {
        float tt = (float)i / (float)gw * 2.0f;
        tt -= floorf(tt);
        int y = (tt < paramPwm) ? top : bot;
        float ts2 = (float)i / (float)gw;
        float sv = subMode ? sinf(6.2832f * ts2) : ((ts2 < 0.5f) ? 1.0f : -1.0f);
        int sy = mid - (int)(sv * half * paramSubLevel);
        int px = gx0 + i;
        if (i > 0) {
            panelCanvas.drawLine(px - 1, prevY, px, y, COL_ACCENT2);
            panelCanvas.drawLine(px - 1, prevSY, px, sy, COL_MUTED);
        }
        prevY = y; prevSY = sy;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawDelayView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print("PING-PONG  filled=L  outline=R");

    int gx0 = 8, gw = w - 16;
    int gyBot = h - 4, gh = h - 20;
    panelCanvas.drawFastHLine(gx0, gyBot, gw, COL_MUTED);

    float pxPerMs = (float)gw / 2000.0f;
    panelCanvas.fillRect(gx0, gyBot - gh, 4, gh, COL_ACCENT);

    float amp = paramDelayMix;
    for (int n = 1; n < 80; n++) {
        int x = gx0 + (int)(n * paramDelayTime * pxPerMs);
        int bh = (int)(gh * amp);
        if (bh < 1 || x > gx0 + gw - 4) break;
        if (n & 1) panelCanvas.fillRect(x, gyBot - bh, 4, bh, COL_ACCENT);
        else if (bh < 3) panelCanvas.fillRect(x, gyBot - bh, 4, bh, COL_ACCENT);
        else panelCanvas.drawRect(x, gyBot - bh, 4, bh, COL_ACCENT);
        amp *= paramDelayFb;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawReverbView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);

    float fb = 0.70f + paramRevSize * 0.25f;
    float lnfb = logf(fb);
    float rt60 = (-6.9078f / lnfb) / 32.0f;

    char title[32];
    snprintf(title, sizeof(title), "ROOM DECAY  RT60 ~ %.1fs", rt60);
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(title);

    int gx0 = 8, gw = w - 16;
    int gyBot = h - 4, gh = h - 20;
    panelCanvas.drawFastHLine(gx0, gyBot, gw, COL_MUTED);

    float peak = gh * (0.25f + 0.75f * paramRevMix);
    int prevX = gx0, prevY = gyBot - (int)peak, prevD = prevY;
    for (int i = 0; i <= gw; i += 4) {
        float t = (float)i / (float)gw * 5.0f;
        int px = gx0 + i;
        int py = gyBot - (int)(peak * expf(lnfb * 32.0f * t));
        int pd = gyBot - (int)(peak * expf(lnfb * 32.0f * t * (1.0f + paramRevDamp * 2.0f)));
        if (i > 0) {
            panelCanvas.drawLine(prevX, prevY, px, py, COL_ACCENT2);
            panelCanvas.drawLine(prevX, prevD, px, pd, COL_MUTED);
        }
        panelCanvas.drawFastVLine(px, py, gyBot - py, COL_MUTED);
        prevX = px; prevY = py; prevD = pd;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawChorusView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[48];
    snprintf(t, sizeof(t), "CHORUS %.1f Hz  DEPTH %d%%  MIX %d%%", paramChoRate,
             (int)(paramChoDepth * 100.0f + 0.5f), (int)(paramChoMix * 100.0f + 0.5f));
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(t);

    int gx0 = 8, gw = w - 16, top = 18, bot = h - 5;
    int mid = (top + bot) / 2, half = (bot - top) / 2;
    panelCanvas.drawFastHLine(gx0, mid, gw, COL_MUTED);
    int prevL = mid, prevR = mid;
    for (int i = 0; i < gw; i++) {
        float a = sinf(6.2832f * 2.0f * (float)i / (float)gw);
        int yl = mid - (int)(a * half * paramChoDepth);
        int yr = mid + (int)(a * half * paramChoDepth);
        int px = gx0 + i;
        if (i > 0) {
            panelCanvas.drawLine(px - 1, prevL, px, yl, COL_ACCENT2);
            panelCanvas.drawLine(px - 1, prevR, px, yr, COL_MUTED);
        }
        prevL = yl; prevR = yr;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawCrushView(int x0, int y0, int h) {
    const int w = PANEL_W;
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    int bits = constrain((int)(paramCrBits + 0.5f), 4, 16);
    int rate = constrain((int)(paramCrRate + 0.5f), 1, 24);
    char t[48];
    snprintf(t, sizeof(t), "CRUSH %d BIT  RATE 1/%d  MIX %d%%", bits, rate, (int)(paramCrMix * 100.0f + 0.5f));
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(t);

    int gx0 = 8, gw = w - 16, top = 18, bot = h - 5;
    int mid = (top + bot) / 2, half = (bot - top) / 2;
    int lv = 1 << (min(bits, 7) - 1);
    int holdPx = rate * 3;
    float held = 0.0f;
    int nextHold = 0;
    int prevO = mid, prevC = mid;
    for (int i = 0; i < gw; i++) {
        float v = sinf(6.2832f * (float)i / (float)gw);
        if (i >= nextHold) {
            held = roundf(v * (float)lv) / (float)lv;
            nextHold = i + holdPx;
        }
        int yo = mid - (int)(v * half);
        int yc = mid - (int)(held * half);
        int px = gx0 + i;
        if (i > 0) {
            panelCanvas.drawLine(px - 1, prevO, px, yo, COL_MUTED);
            panelCanvas.drawLine(px - 1, prevC, px, yc, COL_ACCENT2);
        }
        prevO = yo; prevC = yc;
    }
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

// ---------------- SLIDERS ----------------
// Captura: o slider tocado no início do gesto fica "preso" ao dedo até soltar.
// Durante o arraste o Y é ignorado (o dedo pode escorregar para fora da barra sem
// soltar nem mexer no slider vizinho) e toques que começaram fora de um slider
// (botões, abas) nunca alteram sliders ao passar por cima deles.
// FINE: arrastando, afaste o dedo na vertical para longe da barra -> o movimento
// horizontal passa a valer 1/4 (perto) ou 1/10 (longe). O toque inicial continua absoluto.
const int FINE_BAND_PX = 12;      // folga acima/abaixo da barra antes de entrar no fine
const int FINE_STEP_PX = 40;      // distância extra para passar de 1/4 para 1/10
static TouchSlider *sliderGrab = nullptr;
static bool    touchIsNew = false;   // definido por handleTouch a cada evento
static float   sliderVpos = 0.0f;    // posição virtual do dedo na barra (px)
static int     sliderLastX = 0;
static uint8_t sliderFineDiv = 1;    // 1 = normal, 4 ou 10 = fine
static bool    sliderFineUsed = false;
// toque duplo no mesmo slider = volta ao valor padrão (o arraste fica travado até soltar)
const uint16_t DOUBLE_TAP_MS = 350;
static TouchSlider *sliderLastTap = nullptr;
static unsigned long sliderLastTapMs = 0;
static bool    sliderLock = false;

void drawSlider(TouchSlider &s) {
    sliderCanvas.fillScreen(COL_BG);
    sliderCanvas.setTextSize(1);
    sliderCanvas.setTextColor(COL_TEXT);
    sliderCanvas.setCursor(2, 0);
    sliderCanvas.print(s.label);
    if (&s == sliderGrab && sliderFineDiv > 1) {
        sliderCanvas.print(sliderFineDiv == 4 ? "  FINE 1/4" : "  FINE 1/10");
    }

    float f = (*s.value - s.minV) / (s.maxV - s.minV);
    f = constrain(f, 0.0f, 1.0f);

    char vb[16];
    if (s.value == &paramLfoRate && lfoSync) snprintf(vb, sizeof(vb), "%s", LFO_SYNC_NAMES[lfoSyncIdx()]);
    else if (s.fmt == 1) snprintf(vb, sizeof(vb), "%.1f Hz", *s.value);
    else if (s.fmt == 2) snprintf(vb, sizeof(vb), "%d ms", (int)(*s.value + 0.5f));
    else if (s.fmt == 3) snprintf(vb, sizeof(vb), "%d st", (int)(*s.value * 36.0f + 0.5f));
    else if (s.fmt == 4) snprintf(vb, sizeof(vb), "%d ms", (int)(*s.value * 1000.0f + 0.5f));
    else if (s.fmt == 5) snprintf(vb, sizeof(vb), "%d bit", (int)(*s.value + 0.5f));
    else if (s.fmt == 6) snprintf(vb, sizeof(vb), "1/%d", (int)(*s.value + 0.5f));
    else if (s.fmt == 8) snprintf(vb, sizeof(vb), "x%.2f", *s.value);
    else if (s.fmt == 9) snprintf(vb, sizeof(vb), "%+d st", (int)(*s.value + ((*s.value >= 0.0f) ? 0.5f : -0.5f)));
    else if (s.fmt == 10) snprintf(vb, sizeof(vb), "%+d ct", (int)(*s.value + ((*s.value >= 0.0f) ? 0.5f : -0.5f)));
    else if (s.fmt == 7) snprintf(vb, sizeof(vb), "%+d%%", (int)(*s.value * 100.0f + ((*s.value >= 0.0f) ? 0.5f : -0.5f)));
    else                 snprintf(vb, sizeof(vb), "%d%%", (int)(f * 100.0f + 0.5f));
    sliderCanvas.setTextColor(COL_MUTED);
    sliderCanvas.setCursor(s.w - 2 - (int)strlen(vb) * 6, 0);
    sliderCanvas.print(vb);

    int barY = 12, barH = s.h - 12;
    sliderCanvas.drawRoundRect(0, barY, s.w, barH, barH / 2, COL_BORDER);
    if (s.fmt == 7 || s.fmt == 9 || s.fmt == 10) {   // bipolar: preenche a partir do centro
        int mid = s.w / 2;
        float full = max(fabsf(s.minV), fabsf(s.maxV));
        int half = (int)(fabsf(*s.value) / full * (float)(s.w / 2 - 3));
        int innerH = barH - 6;
        sliderCanvas.drawFastVLine(mid, barY + 2, barH - 4, COL_MUTED);
        if (half > 2) {
            int r = min(innerH / 2, half / 2);
            if (*s.value >= 0.0f) sliderCanvas.fillRoundRect(mid, barY + 3, half, innerH, r, COL_ACCENT);
            else                  sliderCanvas.fillRoundRect(mid - half, barY + 3, half, innerH, r, COL_ACCENT);
        }
    } else {
        int fillW = (int)(f * (s.w - 6));
        if (fillW > 2) {
            int innerH = barH - 6;
            int r = min(innerH / 2, fillW / 2);
            sliderCanvas.fillRoundRect(3, barY + 3, fillW, innerH, r, COL_ACCENT);
        }
    }
    s.last = *s.value;
    tft.drawRGBBitmap(s.x, s.y, sliderCanvas.getBuffer(), s.w, s.h);
}

void refreshSlider(TouchSlider &s) {
    if (fabsf(*s.value - s.last) > 0.0005f * (s.maxV - s.minV)) drawSlider(s);
}

bool touchSlider(TouchSlider &s, int tx, int ty) {
    bool fineChanged = false;
    if (touchIsNew) {
        if (tx < s.x || tx >= s.x + s.w || ty < s.y || ty >= s.y + s.h) return false;
        sliderGrab = &s;
        sliderVpos = (float)tx;
        sliderLastX = tx;
        sliderFineDiv = 1;
        sliderFineUsed = false;
        unsigned long now = millis();
        if (sliderLastTap == &s && now - sliderLastTapMs < DOUBLE_TAP_MS) {
            *s.value = s.def;
            sliderLastTap = nullptr;
            sliderLock = true;
            drawSlider(s);
            return true;
        }
        sliderLastTap = &s;
        sliderLastTapMs = now;
        sliderLock = false;
    } else if (sliderGrab != &s) {
        return false;
    } else {
        if (sliderLock) return true;
        int dist = abs(ty - (s.y + s.h / 2)) - s.h / 2;     // distância vertical fora da barra
        uint8_t div = (dist <= FINE_BAND_PX) ? 1 : ((dist <= FINE_BAND_PX + FINE_STEP_PX) ? 4 : 10);
        if (div > 1) sliderFineUsed = true;
        if (sliderFineUsed) {
            // relativo: depois de usar o fine, o valor não pula de volta para baixo do dedo
            sliderVpos += (float)(tx - sliderLastX) / (float)div;
            sliderVpos = constrain(sliderVpos, (float)(s.x + 3), (float)(s.x + s.w - 3));
        } else {
            sliderVpos = (float)tx;
        }
        sliderLastX = tx;
        if (div != sliderFineDiv) { sliderFineDiv = div; fineChanged = true; }
    }
    float f = (sliderVpos - (float)(s.x + 3)) / (float)(s.w - 6);
    f = constrain(f, 0.0f, 1.0f);
    *s.value = s.minV + f * (s.maxV - s.minV);
    if (sliderFineDiv == 1) {            // zona morta no zero (bipolar); no fine dá para ajustar perto do zero
        if (s.fmt == 7 && fabsf(*s.value) < 0.03f) *s.value = 0.0f;
        if (s.fmt == 10 && fabsf(*s.value) < 3.0f) *s.value = 0.0f;
    }
    if (s.fmt == 9) *s.value = roundf(*s.value);                  // semitons inteiros
    if (s.fmt == 8 && fmSnap) *s.value = roundf(*s.value * 2.0f) * 0.5f;   // razão FM em passos de 0,5
    if (fineChanged) drawSlider(s);      // mostra/esconde a etiqueta FINE
    else             refreshSlider(s);   // só redesenha se o valor mudou (dedo parado = 0 SPI)
    return true;
}

// ---------------- ABAS ----------------
const int TAB_COUNT = 7;
const int TAB_GAP   = 3;
const int TAB_W     = 42;
const int TAB_X0    = 4;
Page currentPage = PAGE_ENV;
const char* PAGE_LABELS[TAB_COUNT] = {"OSC", "ENV", "FLT", "MOD", "SEQ", "FX", "PRE"};
uint8_t subPg[TAB_COUNT] = {0, 0, 0, 0, 0, 0, 0};

int pageSubCount(int i) {
    if (i == PAGE_OSC) return 6;
    if (i == PAGE_ENV || i == PAGE_MOD || i == PAGE_SEQ) return 3;
    if (i == PAGE_FLT || i == PAGE_PRE) return 2;
    return 1;
}
bool pageHasSub(int i) { return pageSubCount(i) > 1; }

// FX tem as próprias subpáginas (DLY/REV/CHR/BIT), mas aparece no menu e nos pontinhos como as outras
extern int fxSubPage;
int tabSubCount(int i) { return (i == PAGE_FX) ? 4 : pageSubCount(i); }
int tabSubIdx(int i)   { return (i == PAGE_FX) ? fxSubPage : (int)subPg[i]; }

const char* const SUB_NAMES[TAB_COUNT][6] = {
    {"WAVE", "PULSE / SUB", "FM", "OSC 2", "UNISON", "WAVETABLE"},
    {"AMP ENV", "FILTER ENV", "PITCH ENV"},
    {"FILTER", "MODE / FOLD"},
    {"LFO", "MATRIX", "VOICE/GLIDE"},
    {"STEPS", "PLAY / SYNC", "PATTERNS"},
    {"DELAY", "REVERB", "CHORUS", "CRUSH"},
    {"PRESETS", "MIDI MAP/CAL"},
};

// MENU DAS SUBPÁGINAS: toque na aba que já está aberta -> lista com os nomes
int8_t subMenuFor = -1;               // aba com o menu aberto (-1 = fechado)
const int SUBMENU_W = 180, SUBMENU_ROW = 28;

void subMenuRect(int &x, int &y, int &w, int &h) {
    int tx = TAB_X0 + subMenuFor * (TAB_W + TAB_GAP);
    w = SUBMENU_W;
    h = tabSubCount(subMenuFor) * SUBMENU_ROW + 8;
    x = constrain(tx, 4, 320 - w - 4);
    y = 31;
}

void drawSubMenu() {
    int x, y, w, h;
    subMenuRect(x, y, w, h);
    tft.fillRect(x + 3, y + 3, w, h, COL_MUTED);                 // sombra
    tft.fillRoundRect(x, y, w, h, RADIUS, COL_BG);
    tft.drawRoundRect(x, y, w, h, RADIUS, COL_BORDER);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, RADIUS, COL_BORDER);
    int n = tabSubCount(subMenuFor), cur = tabSubIdx(subMenuFor);
    tft.setTextSize(2);
    for (int k = 0; k < n; k++) {
        int ry = y + 4 + k * SUBMENU_ROW;
        bool on = (k == cur);
        if (on) tft.fillRoundRect(x + 4, ry, w - 8, SUBMENU_ROW - 2, 4, COL_ACCENT);
        tft.setTextColor(on ? COL_BG : COL_TEXT);
        tft.setCursor(x + 12, ry + 6);
        tft.print(SUB_NAMES[subMenuFor][k]);
    }
    tft.setTextSize(1);
}

// devolve a subpágina tocada (-1 = fora do menu)
int subMenuHit(int tx, int ty) {
    int x, y, w, h;
    subMenuRect(x, y, w, h);
    if (tx < x || tx >= x + w || ty < y + 4 || ty >= y + h - 4) return -1;
    int k = (ty - y - 4) / SUBMENU_ROW;
    return (k >= 0 && k < tabSubCount(subMenuFor)) ? k : -1;
}

void drawTabs() {
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = TAB_X0 + i * (TAB_W + TAB_GAP);
        bool active = ((int)currentPage == i);
        tft.fillRoundRect(x, 4, TAB_W, 26, RADIUS, active ? COL_BORDER : COL_BG);
        if (!active) tft.drawRoundRect(x, 4, TAB_W, 26, RADIUS, COL_BORDER);

        tft.setTextColor(active ? COL_BG : COL_TEXT);
        tft.setTextSize(2);
        int textW = strlen(PAGE_LABELS[i]) * 12;
        tft.setCursor(x + (TAB_W - textW) / 2, 8);
        tft.print(PAGE_LABELS[i]);

        if (active && tabSubCount(i) > 1) {
            int n = tabSubCount(i);
            int sx = x + TAB_W / 2 - ((n - 1) * 8 + 4) / 2;
            for (int k = 0; k < n; k++) {
                uint16_t c = (tabSubIdx(i) == k) ? COL_BG : COL_MUTED;
                tft.fillRect(sx + k * 8, 26, 4, 2, c);
            }
        }
    }
    tft.setTextSize(1);
}

static char lastCorte[32] = "", lastAtk[32] = "", lastSus[32] = "", lastEdit[32] = "", lastMsgLine[40] = "";

void updateTextField(int x, int y, int w, int h, char *cache, size_t cacheSize,
                     uint16_t color, uint8_t textSize, const char *fmt, ...) {
    char buf[48];
    va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    if (strncmp(buf, cache, cacheSize) == 0) return;
    strncpy(cache, buf, cacheSize - 1); cache[cacheSize - 1] = '\0';
    tft.fillRect(x, y, w, h, COL_BG);
    tft.setTextSize(textSize);
    tft.setTextColor(color); tft.setCursor(x, y); tft.print(buf);
    tft.setTextSize(1);
}

// ============================================================================
// BOTÕES E ESTADO DA INTERFACE
// ============================================================================
const int SEQ_GRID_Y = 66, SEQ_CELL_W = 35, SEQ_CELL_H = 30, SEQ_ROW_GAP = 4;
const int SEQ_KEY_Y = 136, SEQ_KEY_W = 21, SEQ_KEY_H = 24;

Btn btnPlay   = {8,   36, 50, 24, "PLAY"};
Btn btnBpmDn  = {62,  36, 28, 24, "-"};
Btn btnBpmUp  = {162, 36, 28, 24, "+"};
Btn btnPat    = {194, 36, 34, 24, "A"};
Btn btnRnd    = {232, 36, 36, 24, "RND"};
Btn btnClr    = {272, 36, 40, 24, "CLR"};
Btn btnOctDn  = {8,   164, 57, 24, "OCT-"};
Btn btnOctUp  = {69,  164, 57, 24, "OCT+"};
Btn btnAcc    = {130, 164, 58, 24, "ACCENT"};
Btn btnSld    = {192, 164, 57, 24, "SLIDE"};
Btn btnGate   = {253, 164, 59, 24, "GATE"};
Btn btnProb   = {8,   192, 72, 26, "PROB"};
Btn btnRatch  = {84,  192, 72, 26, "RATCH"};
Btn btnRec    = {160, 192, 56, 26, "REC"};
Btn btnLearn  = {220, 192, 92, 26, "LEARN"};

// SEQ página 3: padrões A-D e chain
Btn btnPatSel[4] = {{8, 36, 73, 28, "A"}, {85, 36, 73, 28, "B"}, {162, 36, 73, 28, "C"}, {239, 36, 73, 28, "D"}};
Btn btnChainOn = {8,   68, 150, 26, "CHAIN"};
Btn btnPatDup  = {162, 68, 150, 26, "COPY"};
Btn btnChDn    = {8,   148, 60, 26, "-"};
Btn btnChUp    = {252, 148, 60, 26, "+"};
const int CHAIN_Y = 112, CHAIN_CELL_W = 34, CHAIN_CELL_H = 30;
unsigned long dupArmUntil = 0;

// FLT página 2: inclinação do filtro
Btn btnSl[2] = {{8, 68, 150, 24, "12 dB"}, {162, 68, 150, 24, "24 dB"}};

Btn btnDir[4]  = {{8, 36, 73, 28, "FWD"}, {85, 36, 73, 28, "BWD"}, {162, 36, 73, 28, "PING"}, {239, 36, 73, 28, "RND"}};
Btn btnRecBig  = {8,   106, 304, 26, "LIVE REC"};
Btn btnClkInt  = {8,   138, 150, 26, "INT CLOCK"};
Btn btnClkExt  = {162, 138, 150, 26, "MIDI SYNC"};

Btn btnFmSnap = {8,   36, 150, 22, "SNAP"};
Btn btnO2Wave[4] = {{8, 36, 73, 24, "SAW"}, {85, 36, 73, 24, "SQR"}, {162, 36, 73, 24, "TRI"}, {239, 36, 73, 24, "SIN"}};
Btn btnO2Sync    = {8, 64, 150, 22, "SYNC"};
Btn btnO2Ring    = {162, 64, 150, 22, "RING"};
Btn btnUni[4]    = {{8, 36, 73, 28, "OFF"}, {85, 36, 73, 28, "x2"}, {162, 36, 73, 28, "x3"}, {239, 36, 73, 28, "x5"}};
Btn btnSubSqr = {8,   36, 150, 24, "SUB SQR"};
Btn btnSubSin = {162, 36, 150, 24, "SUB SIN"};

Btn btnFm[3] = {{8, 36, 98, 28, "LP"}, {111, 36, 98, 28, "BP"}, {214, 36, 98, 28, "HP"}};
Btn btnVm[3] = {{8, 36, 98, 28, "POLY"}, {111, 36, 98, 28, "MONO"}, {214, 36, 98, 28, "LEGATO"}};

Btn btnFxSel[4] = {{8, 36, 73, 24, "DLY"}, {85, 36, 73, 24, "REV"}, {162, 36, 73, 24, "CHR"}, {239, 36, 73, 24, "BIT"}};
int fxSubPage = 0;

Btn btnPreSave = {8,   134, 68, 28, "SAVE"};
Btn btnPreName = {80,  134, 68, 28, "NAME"};
Btn btnPreFact = {152, 134, 80, 28, "FACTORY"};
Btn btnPrePtn  = {236, 134, 76, 28, "PTN ON"};
Btn btnPreRnd  = {8,   166, 150, 28, "RANDOM"};
Btn btnPreUndo = {162, 166, 150, 28, "UNDO"};
Btn btnMapDef  = {8,   182, 98, 20, "DEFAULT MAP"};
Btn btnMapClr  = {111, 182, 98, 20, "CLEAR ALL"};
Btn btnTchCal  = {214, 182, 98, 20, "TOUCH CAL"};
bool touchCalReq = false;      // pedido de calibração: roda no loop1 depois que o dedo sai

const char* NOTE_NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
const char* KEY_LABELS[13] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B","C"};

int  seqLastDrawnPlay = -1;
bool seqUiPlay = false;
int  padLearnDrawn = -2;
int  seqModeDrawn = -1;
int  clkDrawn = -1;
int  fltModeDrawn = -1;
int  voiceModeDrawn = -1;
int  cpuDrawn = -1;
int  bpmDrawn = -1;
int  mapLearnDrawn = -2;
int  mapCntDrawn = -1;
bool recDrawn = false;
bool nameEdit = false;
unsigned long clrArmUntil = 0;

// desfazer: guarda o padrão antes de RND / CLR (SEQ p1) e COPY (SEQ p3).
// Logo depois da operação o botão CLR (ou COPY) vira UNDO por alguns segundos.
const uint16_t UNDO_MS = 5000;
SeqStep undoBuf[SEQ_STEPS];
int8_t  undoPat = -1;
uint8_t undoSrc = 0;            // página que ofereceu o UNDO (1 ou 3)
unsigned long undoUntil = 0;

void seqSaveUndo(int pat, uint8_t src) {
    memcpy(undoBuf, seqBank[pat & 3], sizeof(undoBuf));
    undoPat = (int8_t)(pat & 3);
    undoSrc = src;
    undoUntil = millis() + UNDO_MS;
}
bool undoActive(uint8_t src) { return undoPat >= 0 && undoSrc == src && millis() < undoUntil; }
void seqUndo() {
    if (undoPat < 0) return;
    memcpy(seqBank[undoPat], undoBuf, sizeof(undoBuf));
    undoPat = -1;
}
unsigned long bpmNextRepeat = 0;
unsigned long saveArmUntil = 0;
unsigned long factArmUntil = 0;

bool btnHit(const Btn &b, int tx, int ty) {
    return tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h;
}

void drawButtonEx(const Btn &b, bool active, const char *txt, uint8_t ts) {
    tft.fillRoundRect(b.x, b.y, b.w, b.h, RADIUS, active ? COL_BORDER : COL_BG);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, RADIUS, COL_BORDER);
    tft.setTextSize(ts);
    tft.setTextColor(active ? COL_BG : COL_TEXT);
    int tw = (int)strlen(txt) * 6 * ts, th = 8 * ts;
    tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2);
    tft.print(txt);
    tft.setTextSize(1);
}
void drawBtn(const Btn &b, bool active) { drawButtonEx(b, active, b.label, 1); }

// ============================================================================
// UI DO SEQUENCIADOR (página 1)
// ============================================================================
void drawPlayButton() { drawButtonEx(btnPlay, seqUiPlay, seqUiPlay ? "STOP" : "PLAY", 1); }

void drawRecButton() {
    recDrawn = seqRecArm;
    drawButtonEx(btnRec, seqRecArm, seqRecArm ? "REC ON" : "REC", 1);
}

void drawLearnButton() {
    int idx = padLearnIdx;
    padLearnDrawn = idx;
    char t[24];
    if (idx >= 0 && idx < 16)  snprintf(t, sizeof(t), "HIT PAD %d", idx + 1);
    else if (idx >= 16)        snprintf(t, sizeof(t), "HIT PLAY");
    else if (padsMapped())     snprintf(t, sizeof(t), "LEARN [OK]");
    else                       snprintf(t, sizeof(t), "LEARN");
    drawButtonEx(btnLearn, idx >= 0, t, 1);
}

int patDrawn = -1;
void drawPatBtn() {
    patDrawn = (int)seqEditPat | ((int)seqPlayPat << 2) | (seqRunning ? 16 : 0) | (seqChainOn ? 32 : 0);
    char t[2] = {(char)('A' + (seqEditPat & 3)), 0};
    drawButtonEx(btnPat, seqRunning && seqPlayPat == seqEditPat, t, 2);
}

void drawBpmBox() {
    bpmDrawn = seqBpm;
    int x = 94, y = 36, w = 64, h = 24;
    tft.fillRoundRect(x, y, w, h, RADIUS, COL_BG);
    tft.drawRoundRect(x, y, w, h, RADIUS, COL_MUTED);
    char b[8]; snprintf(b, sizeof(b), "%d", seqBpm);
    int tw = (int)strlen(b) * 12;
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(x + 4 + (36 - tw), y + 4);
    tft.print(b);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(x + 42, y + 8);
    tft.print(seqSyncExt ? "EXT" : "BPM");
}

void seqCellXY(int i, int &x, int &y) {
    int c = i % 8, r = i / 8;
    x = 8 + (c * 269) / 7;
    y = SEQ_GRID_Y + r * (SEQ_CELL_H + SEQ_ROW_GAP);
}

void drawSeqCell(int i) {
    int x, y; seqCellXY(i, x, y);
    const SeqStep &st = seq[i];
    bool playing = (seqLastDrawnPlay == i);
    bool sel = (seqEditStep == i);
    uint16_t bg = playing ? COL_BORDER : COL_BG;
    uint16_t fg = playing ? COL_BG : (st.gate ? COL_TEXT : COL_MUTED);

    tft.fillRoundRect(x, y, SEQ_CELL_W, SEQ_CELL_H, RADIUS, bg);
    tft.drawRoundRect(x, y, SEQ_CELL_W, SEQ_CELL_H, RADIUS,
                      (playing || sel || st.gate) ? COL_BORDER : COL_MUTED);
    if (sel) tft.drawRoundRect(x + 2, y + 2, SEQ_CELL_W - 4, SEQ_CELL_H - 4, RADIUS - 2,
                               playing ? COL_BG : COL_BORDER);

    char nb[8];
    if (st.gate) {
        int n = seqStepNote(i);
        snprintf(nb, sizeof(nb), "%s%d", NOTE_NAMES[n % 12], n / 12 - 1);
    } else {
        strcpy(nb, "--");
    }
    tft.setTextSize(1);
    tft.setTextColor(fg);
    tft.setCursor(x + (SEQ_CELL_W - (int)strlen(nb) * 6) / 2, y + 6);
    tft.print(nb);
    if (st.gate) {   // marcas: A accent | S slide | % probabilidade | 2-4 ratchet
        int ix = x + 3;
        if (st.accent)   { tft.setCursor(ix,      y + 18); tft.print("A"); }
        if (st.slide)    { tft.setCursor(ix + 7,  y + 18); tft.print("S"); }
        if (st.probIdx)  { tft.setCursor(ix + 14, y + 18); tft.print("%"); }
        if (st.ratchIdx) { tft.setCursor(ix + 21, y + 18); tft.print((char)('1' + st.ratchIdx)); }
    }
}

void drawSeqGrid() { for (int i = 0; i < SEQ_STEPS; i++) drawSeqCell(i); }

bool isBlackKey(int k) { return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

void drawSeqKey(int k) {
    int x = 8 + (k * 283) / 12;
    bool black = isBlackKey(k);
    bool sel = (seq[seqEditStep].semi == k);
    uint16_t fill = sel ? COL_MUTED : (black ? COL_BORDER : COL_BG);
    uint16_t fg = (sel || black) ? COL_BG : COL_TEXT;
    tft.fillRoundRect(x, SEQ_KEY_Y, SEQ_KEY_W, SEQ_KEY_H, 3, fill);
    tft.drawRoundRect(x, SEQ_KEY_Y, SEQ_KEY_W, SEQ_KEY_H, 3, COL_BORDER);
    tft.setTextSize(1);
    tft.setTextColor(fg);
    int tw = (int)strlen(KEY_LABELS[k]) * 6;
    tft.setCursor(x + (SEQ_KEY_W - tw) / 2, SEQ_KEY_Y + SEQ_KEY_H - 12);
    tft.print(KEY_LABELS[k]);
}

void drawSeqKeys() { for (int k = 0; k < 13; k++) drawSeqKey(k); }

void drawSeqRowD() {
    const SeqStep &st = seq[seqEditStep];
    drawBtn(btnOctDn, false);
    drawBtn(btnOctUp, false);
    drawBtn(btnAcc, st.accent);
    drawBtn(btnSld, st.slide);
    drawBtn(btnGate, st.gate);
}

const int PROB_PCT[4] = {100, 75, 50, 25};

void drawSeqRowE() {
    const SeqStep &st = seq[seqEditStep];
    char pb[16], rb[16];
    snprintf(pb, sizeof(pb), "PROB %d%%", PROB_PCT[st.probIdx & 3]);
    snprintf(rb, sizeof(rb), "RATCH x%d", (st.ratchIdx & 3) + 1);
    drawButtonEx(btnProb, st.probIdx > 0, pb, 1);
    drawButtonEx(btnRatch, st.ratchIdx > 0, rb, 1);
}

void drawSeqEditor() { drawSeqKeys(); drawSeqRowD(); drawSeqRowE(); }

// troca o padrão editado (e, com a chain desligada, o que toca no fim do compasso)
void seqSelectPattern(int p) {
    p &= 3;
    seqEditPat = (uint8_t)p;
    seq = seqBank[p];
    if (!seqChainOn) seqCuePat = (uint8_t)p;
}

void drawSeqPage3();

void seqRedrawPatternUI() {
    if (currentPage != PAGE_SEQ) return;
    if (subPg[PAGE_SEQ] == 0)      { drawPatBtn(); drawSeqGrid(); drawSeqEditor(); }
    else if (subPg[PAGE_SEQ] == 2) { drawSeqPage3(); }
}

void seqAudition(int i) {
    if (!seqUiPlay && seq[i].gate) seqAuditionReq = seqStepNote(i);
}

void seqRandomize() {
    const uint8_t scale[6] = {0, 3, 5, 7, 10, 12};
    for (int i = 0; i < SEQ_STEPS; i++) {
        seq[i].semi   = scale[random(0, 6)];
        seq[i].oct    = (random(0, 6) == 0) ? 1 : 0;
        seq[i].gate   = random(0, 100) < 78;
        seq[i].accent = random(0, 100) < 25;
        seq[i].slide  = random(0, 100) < 20;
        seq[i].probIdx = 0;
        seq[i].ratchIdx = 0;
    }
}

void seqClear() {
    for (int i = 0; i < SEQ_STEPS; i++) seq[i] = SeqStep{0, 0, false, false, false, 0, 0};
}

void seqChangeBpm(int d) {
    seqBpm = constrain(seqBpm + d, 40, 300);
    seqUpdateTiming();
    drawBpmBox();
}

void handleSeqTouch(int tx, int ty, bool newPress) {
    static bool bpmHeld = false;         // auto-repeat só se o toque COMEÇOU no botão
    bool dn = btnHit(btnBpmDn, tx, ty), up = btnHit(btnBpmUp, tx, ty);
    if (newPress) bpmHeld = (dn || up);
    if ((dn || up) && bpmHeld) {
        if (seqSyncExt) return;
        unsigned long now = millis();
        if (newPress) { bpmNextRepeat = now + 400; seqChangeBpm(dn ? -1 : 1); }
        else if (now >= bpmNextRepeat) { bpmNextRepeat = now + 70; seqChangeBpm(dn ? -1 : 1); }
        return;
    }
    if (!newPress) return;

    if (btnHit(btnPlay, tx, ty)) {
        seqUiPlay = !seqUiPlay;
        seqCmd = seqUiPlay ? 1 : 2;
        drawPlayButton();
        return;
    }
    if (btnHit(btnPat, tx, ty)) {
        seqSelectPattern(seqEditPat + 1);
        seqRedrawPatternUI();
        return;
    }
    if (btnHit(btnRnd, tx, ty)) {
        // RND várias vezes seguidas: o UNDO volta para o padrão de ANTES da primeira
        if (undoActive(1)) undoUntil = millis() + UNDO_MS;
        else               seqSaveUndo(seqEditPat, 1);
        seqRandomize();
        clrArmUntil = 0;
        drawButtonEx(btnClr, true, "UNDO", 1);
        drawSeqGrid(); drawSeqEditor();
        return;
    }
    if (btnHit(btnClr, tx, ty)) {
        if (undoActive(1)) {                     // logo depois de RND/CLR o botão é UNDO
            seqUndo();
            drawBtn(btnClr, false);
            drawSeqGrid(); drawSeqEditor();
        } else if (millis() < clrArmUntil) {
            seqSaveUndo(seqEditPat, 1);
            seqClear();
            clrArmUntil = 0;
            drawButtonEx(btnClr, true, "UNDO", 1);
            drawSeqGrid(); drawSeqEditor();
        } else {
            clrArmUntil = millis() + 2000;
            drawButtonEx(btnClr, true, "SURE?", 1);
        }
        return;
    }
    if (btnHit(btnRec, tx, ty)) {
        seqRecArm = !seqRecArm;
        drawRecButton();
        return;
    }
    if (btnHit(btnLearn, tx, ty)) {
        if (padLearnIdx >= 0) { padLearnIdx = -1; padMapDirty = true; }
        else                  { padLearnIdx = 0; }
        drawLearnButton();
        return;
    }

    for (int i = 0; i < SEQ_STEPS; i++) {
        int x, y; seqCellXY(i, x, y);
        if (tx >= x && tx < x + SEQ_CELL_W && ty >= y && ty < y + SEQ_CELL_H) {
            int old = seqEditStep;
            seqEditStep = i;
            drawSeqCell(old);
            drawSeqCell(i);
            drawSeqEditor();
            seqAudition(i);
            return;
        }
    }

    if (ty >= SEQ_KEY_Y && ty < SEQ_KEY_Y + SEQ_KEY_H && tx >= 8 && tx < 312) {
        int k = ((tx - 8) * 12 + 141) / 283;
        k = constrain(k, 0, 12);
        seq[seqEditStep].semi = (uint8_t)k;
        seq[seqEditStep].gate = true;
        drawSeqCell(seqEditStep);
        drawSeqEditor();
        seqAudition(seqEditStep);
        return;
    }

    SeqStep &st = seq[seqEditStep];
    if (btnHit(btnProb, tx, ty))  { st.probIdx  = (st.probIdx + 1) & 3;  drawSeqCell(seqEditStep); drawSeqRowE(); return; }
    if (btnHit(btnRatch, tx, ty)) { st.ratchIdx = (st.ratchIdx + 1) & 3; drawSeqCell(seqEditStep); drawSeqRowE(); return; }
    if (btnHit(btnOctDn, tx, ty)) { if (st.oct > -1) st.oct--; }
    else if (btnHit(btnOctUp, tx, ty)) { if (st.oct < 2) st.oct++; }
    else if (btnHit(btnAcc, tx, ty))  { st.accent = !st.accent; }
    else if (btnHit(btnSld, tx, ty))  { st.slide  = !st.slide; }
    else if (btnHit(btnGate, tx, ty)) { st.gate   = !st.gate; }
    else return;

    drawSeqCell(seqEditStep);
    drawSeqRowD();
    if (btnHit(btnOctDn, tx, ty) || btnHit(btnOctUp, tx, ty)) seqAudition(seqEditStep);
}

void updateSeqPlayhead() {
    int cur = (seqRunning && seqPlayPat == seqEditPat) ? seqPlayStep : -1;
    int pd = (int)seqEditPat | ((int)seqPlayPat << 2) | (seqRunning ? 16 : 0) | (seqChainOn ? 32 : 0);
    if (pd != patDrawn) drawPatBtn();
    if (cur != seqLastDrawnPlay) {
        int old = seqLastDrawnPlay;
        seqLastDrawnPlay = cur;
        if (old >= 0) drawSeqCell(old);
        if (cur >= 0) drawSeqCell(cur);
    }

    if (seqRedrawMask) {
        uint16_t m = seqRedrawMask;
        seqRedrawMask = 0;
        for (int i = 0; i < SEQ_STEPS; i++) if (m & (1u << i)) drawSeqCell(i);
    }
    if (seqEditorDirty) { seqEditorDirty = false; drawSeqEditor(); }

    if (seqCmd == 0 && seqUiPlay != seqRunning) { seqUiPlay = seqRunning; drawPlayButton(); }
    if (padLearnIdx != padLearnDrawn) drawLearnButton();
    if (recDrawn != seqRecArm) drawRecButton();
    if (bpmDrawn != seqBpm) drawBpmBox();

    if (clrArmUntil != 0 && millis() > clrArmUntil) {
        clrArmUntil = 0;
        drawBtn(btnClr, false);
    }
    if (undoPat >= 0 && undoSrc == 1 && millis() >= undoUntil) {
        undoPat = -1;
        drawBtn(btnClr, false);
    }
}

// ---------------- SEQ página 2: direção, swing, live record, clock ----------------
void drawSeqDirButtons() {
    for (int k = 0; k < 4; k++) drawBtn(btnDir[k], seqMode == k);
    seqModeDrawn = seqMode;
}

void drawRecBig() {
    recDrawn = seqRecArm;
    drawButtonEx(btnRecBig, seqRecArm, seqRecArm ? "LIVE REC: ON" : "LIVE REC: OFF", 2);
}

void drawClockButtons() {
    drawButtonEx(btnClkInt, !seqSyncExt, "INT CLOCK", 2);
    drawButtonEx(btnClkExt, seqSyncExt, "MIDI SYNC", 2);
    clkDrawn = seqSyncExt ? 1 : 0;
}

void drawSeqPage2() {
    drawSeqDirButtons();
    drawSlider(sliderSwing);
    drawRecBig();
    drawClockButtons();
    drawSlider(sliderAccent);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 206); tft.print("ACCENTS STACK UP AND OPEN THE FILTER (TB-303).");
}

void handleSeqPage2Touch(int tx, int ty, bool newPress) {
    if (touchSlider(sliderSwing, tx, ty)) return;
    if (touchSlider(sliderAccent, tx, ty)) return;
    if (!newPress) return;
    for (int k = 0; k < 4; k++) {
        if (btnHit(btnDir[k], tx, ty)) { seqMode = (uint8_t)k; drawSeqDirButtons(); return; }
    }
    if (btnHit(btnRecBig, tx, ty)) { seqRecArm = !seqRecArm; drawRecBig(); return; }
    if (btnHit(btnClkInt, tx, ty)) {
        if (seqSyncExt) { seqSyncExt = false; seqCmd = 2; settingsDirty = true; drawClockButtons(); }
        return;
    }
    if (btnHit(btnClkExt, tx, ty)) {
        if (!seqSyncExt) { seqSyncExt = true; seqCmd = 2; settingsDirty = true; drawClockButtons(); }
        return;
    }
}

void updateSeqPage2() {
    if (seqModeDrawn != (int)seqMode) drawSeqDirButtons();
    if (recDrawn != seqRecArm) drawRecBig();
    if (clkDrawn != (seqSyncExt ? 1 : 0)) drawClockButtons();
    refreshSlider(sliderSwing);
    refreshSlider(sliderAccent);
}

// ---------------- SEQ página 3: padrões A-D, chain e cópia ----------------
int p3Drawn = -1;

void drawPatSel() {
    for (int k = 0; k < 4; k++) {
        bool sel = (seqEditPat == k);
        drawButtonEx(btnPatSel[k], sel, btnPatSel[k].label, 2);
        if (seqRunning && seqPlayPat == k) {          // barrinha = padrão tocando agora
            const Btn &b = btnPatSel[k];
            tft.fillRect(b.x + b.w / 2 - 8, b.y + b.h - 6, 16, 3, sel ? COL_BG : COL_BORDER);
        }
    }
}

void drawChainButtons() {
    drawButtonEx(btnChainOn, seqChainOn, seqChainOn ? "CHAIN: ON" : "CHAIN: OFF", 2);
}

void drawPatDup() {
    char t[16];
    bool armed = (dupArmUntil != 0 && millis() < dupArmUntil);
    bool undo = undoActive(3);
    if (armed)     snprintf(t, sizeof(t), "SURE?");
    else if (undo) snprintf(t, sizeof(t), "UNDO");
    else           snprintf(t, sizeof(t), "COPY %c>%c", 'A' + (seqEditPat & 3), 'A' + ((seqEditPat + 1) & 3));
    drawButtonEx(btnPatDup, armed || undo, t, 2);
}

void chainCellXY(int i, int &x, int &y) { x = 8 + (i * 270) / 7; y = CHAIN_Y; }

void drawChainCell(int i) {
    int x, y; chainCellXY(i, x, y);
    bool inUse = (i < (int)seqChainLen);
    bool playing = seqChainOn && seqRunning && ((int)seqChainPos == i) && inUse;
    uint16_t fg = playing ? COL_BG : (inUse ? COL_TEXT : COL_MUTED);
    tft.fillRoundRect(x, y, CHAIN_CELL_W, CHAIN_CELL_H, RADIUS, playing ? COL_BORDER : COL_BG);
    tft.drawRoundRect(x, y, CHAIN_CELL_W, CHAIN_CELL_H, RADIUS, inUse ? COL_BORDER : COL_MUTED);
    char t[2] = {(char)('A' + (seqChain[i] & 3)), 0};
    tft.setTextSize(2);
    tft.setTextColor(fg);
    tft.setCursor(x + (CHAIN_CELL_W - 12) / 2, y + (CHAIN_CELL_H - 16) / 2);
    tft.print(t);
    tft.setTextSize(1);
}

void drawChainCells() { for (int i = 0; i < 8; i++) drawChainCell(i); }

void drawChainLen() {
    drawButtonEx(btnChDn, false, "-", 2);
    drawButtonEx(btnChUp, false, "+", 2);
    tft.fillRoundRect(72, 148, 176, 26, RADIUS, COL_BG);
    tft.drawRoundRect(72, 148, 176, 26, RADIUS, COL_MUTED);
    char t[16];
    snprintf(t, sizeof(t), "LENGTH: %d", (int)seqChainLen);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(72 + (176 - (int)strlen(t) * 12) / 2, 154);
    tft.print(t);
    tft.setTextSize(1);
}

static int p3Hash() {
    return (int)seqEditPat | ((int)seqPlayPat << 2) | (seqRunning ? 16 : 0) | (seqChainOn ? 32 : 0)
         | ((int)seqChainPos << 6) | ((int)seqChainLen << 9);
}

void drawSeqPage3() {
    drawPatSel();
    drawChainButtons();
    drawPatDup();
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 100); tft.print("CHAIN ORDER - TAP A CELL TO CHANGE");
    drawChainCells();
    drawChainLen();
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 182); tft.print("A-D: PATTERN TO EDIT + PLAY (SWITCHES AT BAR END).");
    tft.setCursor(8, 194); tft.print("CHAIN ON: PLAYS THE ORDER ABOVE, ONE BAR EACH.");
    tft.setCursor(8, 206); tft.print("COPY: DUPLICATES THE PATTERN INTO THE NEXT LETTER.");
    p3Drawn = p3Hash();
}

void handleSeqPage3Touch(int tx, int ty, bool newPress) {
    if (!newPress) return;
    for (int k = 0; k < 4; k++) {
        if (btnHit(btnPatSel[k], tx, ty)) {
            seqSelectPattern(k);
            dupArmUntil = 0;
            drawSeqPage3();
            return;
        }
    }
    if (btnHit(btnChainOn, tx, ty)) {
        seqChainOn = !seqChainOn;
        if (seqChainOn) { if (seqRunning) seqChainNextPos = (uint8_t)(seqChainLen - 1); }  // próximo compasso = 1ª posição
        else            seqCuePat = seqEditPat;
        drawChainButtons();
        return;
    }
    if (btnHit(btnPatDup, tx, ty)) {
        if (undoActive(3)) {
            seqUndo();
        } else if (dupArmUntil != 0 && millis() < dupArmUntil) {
            dupArmUntil = 0;
            int dst = (seqEditPat + 1) & 3;
            seqSaveUndo(dst, 3);
            memcpy(seqBank[dst], seqBank[seqEditPat & 3], sizeof(seqBank[0]));
        } else {
            dupArmUntil = millis() + 2000;
        }
        drawPatDup();
        return;
    }
    if (btnHit(btnChDn, tx, ty)) { if (seqChainLen > 1) seqChainLen--; drawChainCells(); drawChainLen(); return; }
    if (btnHit(btnChUp, tx, ty)) { if (seqChainLen < 8) seqChainLen++; drawChainCells(); drawChainLen(); return; }
    for (int i = 0; i < 8; i++) {
        int x, y; chainCellXY(i, x, y);
        if (tx >= x && tx < x + CHAIN_CELL_W && ty >= y && ty < y + CHAIN_CELL_H) {
            seqChain[i] = (uint8_t)((seqChain[i] + 1) & 3);
            drawChainCell(i);
            return;
        }
    }
}

void updateSeqPage3() {
    if (p3Drawn != p3Hash()) {
        p3Drawn = p3Hash();
        drawPatSel();
        drawChainCells();
        drawChainLen();
    }
    if (dupArmUntil != 0 && millis() >= dupArmUntil) { dupArmUntil = 0; drawPatDup(); }
    if (undoPat >= 0 && undoSrc == 3 && millis() >= undoUntil) { undoPat = -1; drawPatDup(); }
}

// ============================================================================
// UI DA ABA MOD (página 1): forma do LFO e sincronia com o BPM
// ============================================================================
Btn btnLfo[6] = {{8, 36, 47, 24, "SIN"}, {59, 36, 47, 24, "TRI"}, {110, 36, 47, 24, "SAW"},
                 {161, 36, 47, 24, "SQR"}, {212, 36, 47, 24, "S&H"}, {263, 36, 49, 24, "SYNC"}};
int lfoTopDrawn = -1;
int lfoTopHash() { return (int)lfoShape | (lfoSync ? 8 : 0); }

void drawLfoTop() {
    for (int k = 0; k < 5; k++) drawBtn(btnLfo[k], lfoShape == k);
    drawBtn(btnLfo[5], lfoSync);
    lfoTopDrawn = lfoTopHash();
}

void handleLfoTouch(int tx, int ty, bool newPress) {
    if (newPress) {
        for (int k = 0; k < 5; k++) {
            if (btnHit(btnLfo[k], tx, ty)) { lfoShape = (uint8_t)k; drawLfoTop(); return; }
        }
        if (btnHit(btnLfo[5], tx, ty)) {
            lfoSync = !lfoSync;
            drawLfoTop();
            drawSlider(sliderLfoRate);           // o valor passa a mostrar a divisão
            return;
        }
    }
    if (touchSlider(sliderLfoRate, tx, ty)) return;
    if (touchSlider(sliderLfoDepth, tx, ty)) return;
    touchSlider(sliderDetune, tx, ty);
}

// ============================================================================
// UI DA ABA MOD (página 4): MATRIZ DE MODULAÇÃO 4x4
// colunas = fontes (LFO, F.ENV, A.ENV, VEL) | linhas = destinos (CUTOFF, PITCH, PWM, VOLUME)
// ============================================================================
const char* MM_SRC_NAMES[4] = {"LFO", "F.ENV", "A.ENV", "VEL"};
const char* MM_DST_NAMES[4] = {"CUTOFF", "PITCH", "PWM/WT", "VOLUME"};   // PWM/WT: largura do pulso e posição da wavetable
int   mmSel = 0;
float mmLast[16];

void mmCellXY(int i, int &x, int &y) { x = 68 + (i & 3) * 62; y = 48 + (i >> 2) * 28; }

void drawMmCell(int i) {
    int x, y; mmCellXY(i, x, y);
    float a = mmAmt[i];
    bool on = fabsf(a) >= 0.005f;
    bool sel = (mmSel == i);
    tft.fillRoundRect(x, y, 58, 24, RADIUS, COL_BG);
    tft.drawRoundRect(x, y, 58, 24, RADIUS, (sel || on) ? COL_BORDER : COL_MUTED);
    if (sel) tft.drawRoundRect(x + 2, y + 2, 54, 20, RADIUS - 2, COL_BORDER);
    char t[8];
    if (on) snprintf(t, sizeof(t), "%+d", (int)(a * 100.0f + ((a >= 0.0f) ? 0.5f : -0.5f)));
    else    snprintf(t, sizeof(t), "--");
    tft.setTextSize(1);
    tft.setTextColor(on ? COL_TEXT : COL_MUTED);
    tft.setCursor(x + (58 - (int)strlen(t) * 6) / 2, y + 5);
    tft.print(t);
    int cx = x + 29;
    tft.drawFastVLine(cx, y + 13, 6, COL_MUTED);
    int w = (int)(fabsf(a) * 24.0f);
    if (w > 0) {
        if (a > 0) tft.fillRect(cx, y + 14, w, 4, COL_ACCENT);
        else       tft.fillRect(cx - w, y + 14, w, 4, COL_ACCENT);
    }
    mmLast[i] = a;
}

void mmSelectCell(int i) {
    mmSel = i;
    snprintf(mmLabel, sizeof(mmLabel), "%s > %s", MM_SRC_NAMES[i & 3], MM_DST_NAMES[i >> 2]);
    sliderMm.value = &mmAmt[i];
    sliderMm.last = -999.0f;
}

void drawMmPage() {
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    for (int s = 0; s < 4; s++) {
        int x, y; mmCellXY(s, x, y);
        tft.setCursor(x + (58 - (int)strlen(MM_SRC_NAMES[s]) * 6) / 2, 37);
        tft.print(MM_SRC_NAMES[s]);
    }
    tft.setTextColor(COL_TEXT);
    for (int d = 0; d < 4; d++) {
        int x, y; mmCellXY(d * 4, x, y);
        tft.setCursor(8, y + 8);
        tft.print(MM_DST_NAMES[d]);
    }
    for (int i = 0; i < 16; i++) drawMmCell(i);
    mmSelectCell(mmSel);
    drawSlider(sliderMm);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 196); tft.print("ADDS TO PANEL KNOBS. TAP A CELL, DRAG THE BAR.");
    tft.setCursor(8, 208); tft.print("TAP A SELECTED CELL AGAIN TO ZERO IT.");
}

void handleMmTouch(int tx, int ty, bool newPress) {
    if (touchSlider(sliderMm, tx, ty)) return;
    if (!newPress) return;
    for (int i = 0; i < 16; i++) {
        int x, y; mmCellXY(i, x, y);
        if (tx >= x && tx < x + 58 && ty >= y && ty < y + 24) {
            if (i == mmSel) {
                mmAmt[i] = 0.0f;
            } else {
                int old = mmSel;
                mmSelectCell(i);
                drawMmCell(old);
            }
            drawMmCell(i);
            drawSlider(sliderMm);
            return;
        }
    }
}

void updateMmPage() {
    for (int i = 0; i < 16; i++) {
        if (fabsf(mmAmt[i] - mmLast[i]) > 0.001f) drawMmCell(i);
    }
    refreshSlider(sliderMm);
}

// ============================================================================
// UI DA ABA FLT (página 2): tipo do filtro, drive e envelope do filtro
// ============================================================================
void drawFltModeButtons() {
    for (int k = 0; k < 3; k++) drawButtonEx(btnFm[k], filtMode == k, btnFm[k].label, 2);
    for (int k = 0; k < 2; k++) drawButtonEx(btnSl[k], filtSlope == k, btnSl[k].label, 2);
    fltModeDrawn = filtMode | (filtSlope << 2);
}

void drawFltPage2() {
    drawFltModeButtons();
    drawSlider(sliderFold);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 140); tft.print("LP: WARM BASS/PADS  BP: NASAL/VOCAL  HP: THIN");
    tft.setCursor(8, 154); tft.print("24 dB = STEEPER (MOOG-LIKE), 12 dB = SOFTER");
    tft.setCursor(8, 168); tft.print("WAVEFOLDER: TRY SIN OR TRI + ENV ON CUTOFF");
    tft.setCursor(8, 190); tft.print("FILTER ENVELOPE: ENV TAB > FILTER ENV");
}

void handleFltPage2Touch(int tx, int ty, bool newPress) {
    if (touchSlider(sliderFold, tx, ty)) return;
    if (!newPress) return;
    for (int k = 0; k < 3; k++) {
        if (btnHit(btnFm[k], tx, ty)) { filtMode = (uint8_t)k; drawFltModeButtons(); return; }
    }
    for (int k = 0; k < 2; k++) {
        if (btnHit(btnSl[k], tx, ty)) { filtSlope = (uint8_t)k; drawFltModeButtons(); return; }
    }
}

void updateFltPage2() {
    if (fltModeDrawn != (int)(filtMode | (filtSlope << 2))) drawFltModeButtons();
    refreshSlider(sliderFold);
}

// ============================================================================
// UI DA ABA MOD (página 3): modo de voz, glide e medidor de CPU
// ============================================================================
void drawVoiceButtons() {
    for (int k = 0; k < 3; k++) drawButtonEx(btnVm[k], voiceMode == k, btnVm[k].label, 2);
    voiceModeDrawn = voiceMode;
}

void drawCpuBar() {
    int pct = cpuPct;
    cpuDrawn = pct;
    tft.fillRect(8, 176, 304, 36, COL_BG);
    char t[24];
    snprintf(t, sizeof(t), "CPU LOAD  %d%%", pct);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(8, 178);
    tft.print(t);
    tft.drawRoundRect(8, 192, 304, 16, 5, COL_BORDER);
    int fw = (pct * 298) / 100;
    if (fw > 2) tft.fillRoundRect(11, 195, fw, 10, 3, COL_ACCENT);
}

char encTestDrawn[64] = "";

void drawVoicePage() {
    drawVoiceButtons();
    drawSlider(sliderGlide);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 108); tft.print("POLY: UP TO 4 NOTES AT ONCE.");
    tft.setCursor(8, 120); tft.print("MONO: ONE NOTE AT A TIME, ENV RETRIGGERS.");
    tft.setCursor(8, 132); tft.print("LEGATO: OVERLAPPING NOTES GLIDE (NO RETRIGGER).");
    tft.setCursor(8, 144); tft.print("GLIDE TIME ALSO SETS THE SEQUENCER SLIDE.");
    encTestDrawn[0] = 0;
    drawEncTest();
    drawCpuBar();
}

// teste do encoder: estado dos pinos ao vivo. Girando, CLK/DT mudam e STEPS conta;
// SW só deve ir a 0 quando o eixo é apertado.
void drawEncTest() {
    char t[64];
    snprintf(t, sizeof(t), "ENCODER  CLK:%d DT:%d SW:%d  STEPS:%+ld  NOISE:%u",
             digitalRead(ENC_CLK), digitalRead(ENC_DT), digitalRead(ENC_SW), (long)encStepTotal, (unsigned)encSwNoise);
    if (strcmp(t, encTestDrawn) == 0) return;
    strcpy(encTestDrawn, t);
    tft.fillRect(8, 158, 304, 10, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(8, 160);
    tft.print(t);
}

void handleVoicePageTouch(int tx, int ty, bool newPress) {
    if (touchSlider(sliderGlide, tx, ty)) return;
    if (!newPress) return;
    for (int k = 0; k < 3; k++) {
        if (btnHit(btnVm[k], tx, ty)) {
            if (voiceMode != k) { voiceMode = (uint8_t)k; monoResetReq = true; }
            drawVoiceButtons();
            return;
        }
    }
}

void updateVoicePage() {
    if (voiceModeDrawn != (int)voiceMode) drawVoiceButtons();
    refreshSlider(sliderGlide);
    drawEncTest();
    if (cpuDrawn != (int)cpuPct) drawCpuBar();
}

// ============================================================================
// UI DA ABA FX: DLY / REV / CHR / BIT
// ============================================================================
void drawFxToggle() {
    for (int k = 0; k < 4; k++) drawButtonEx(btnFxSel[k], fxSubPage == k, btnFxSel[k].label, 2);
    drawTabs();
}

void drawFxSub() {
    tft.fillRect(0, 62, 320, 158, COL_BG);
    switch (fxSubPage) {
        case 0:
            drawDelayView(8, 66, 46);
            drawSlider(sliderDlyTime); drawSlider(sliderDlyFb); drawSlider(sliderDlyMix);
            break;
        case 1:
            drawReverbView(8, 66, 46);
            drawSlider(sliderRevSize); drawSlider(sliderRevDamp); drawSlider(sliderRevMix);
            break;
        case 2:
            drawChorusView(8, 66, 46);
            drawSlider(sliderChoRate); drawSlider(sliderChoDepth); drawSlider(sliderChoMix);
            break;
        default:
            drawCrushView(8, 66, 46);
            drawSlider(sliderCrBits); drawSlider(sliderCrRate); drawSlider(sliderCrMix);
            break;
    }
}

void handleFxTouch(int tx, int ty, bool newPress) {
    if (newPress) {
        for (int k = 0; k < 4; k++) {
            if (btnHit(btnFxSel[k], tx, ty)) {
                if (fxSubPage != k) { fxSubPage = k; drawFxToggle(); drawFxSub(); }
                return;
            }
        }
    }
    switch (fxSubPage) {
        case 0:
            if (touchSlider(sliderDlyTime, tx, ty)) return;
            if (touchSlider(sliderDlyFb, tx, ty))   return;
            touchSlider(sliderDlyMix, tx, ty);
            break;
        case 1:
            if (touchSlider(sliderRevSize, tx, ty)) return;
            if (touchSlider(sliderRevDamp, tx, ty)) return;
            touchSlider(sliderRevMix, tx, ty);
            break;
        case 2:
            if (touchSlider(sliderChoRate, tx, ty))  return;
            if (touchSlider(sliderChoDepth, tx, ty)) return;
            touchSlider(sliderChoMix, tx, ty);
            break;
        default:
            if (touchSlider(sliderCrBits, tx, ty)) return;
            if (touchSlider(sliderCrRate, tx, ty)) return;
            touchSlider(sliderCrMix, tx, ty);
            break;
    }
}

// ============================================================================
// UI DA ABA OSC (página 2): PWM + sub-oscilador
// ============================================================================
int osc2SubDrawn = -1;

void drawOsc2Toggle() {
    drawButtonEx(btnSubSqr, subMode == 0, "SUB SQR", 2);
    drawButtonEx(btnSubSin, subMode == 1, "SUB SIN", 2);
    osc2SubDrawn = subMode;
}

void drawOsc2Page() {
    drawOsc2Toggle();
    drawOsc2Graph(8, 66, 46);
    drawSlider(sliderPwm);
    drawSlider(sliderPwmMod);
    drawSlider(sliderSubLvl);
}

void handleOsc2Touch(int tx, int ty, bool newPress) {
    if (newPress) {
        if (btnHit(btnSubSqr, tx, ty)) { subMode = 0; drawOsc2Toggle(); drawOsc2Graph(8, 66, 46); return; }
        if (btnHit(btnSubSin, tx, ty)) { subMode = 1; drawOsc2Toggle(); drawOsc2Graph(8, 66, 46); return; }
    }
    if (touchSlider(sliderPwm, tx, ty)) return;
    if (touchSlider(sliderPwmMod, tx, ty)) return;
    touchSlider(sliderSubLvl, tx, ty);
}

// ============================================================================
// UI DA ABA OSC (página 3): FM (modulador seno, modulação de fase)
// ============================================================================
float fmSnapRatio(float r) {
    r = roundf(r * 2.0f) / 2.0f;
    return (r < 0.5f) ? 0.5f : r;
}

void drawFmTop() {
    drawButtonEx(btnFmSnap, fmSnap, fmSnap ? "SNAP 0.5: ON" : "SNAP: OFF", 1);
    tft.fillRect(162, 36, 150, 22, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(paramFmAmt > 0.01f ? COL_TEXT : COL_MUTED);
    tft.setCursor(166, 43);
    tft.print(paramFmAmt > 0.01f ? "FM ACTIVE (SINE MOD)" : "FM OFF (AMOUNT = 0)");
}

void drawOscFmPage() {
    drawFmTop();
    drawSlider(sliderFmAmt);
    drawSlider(sliderFmRatio);
    drawSlider(sliderFmDec);
    drawSlider(sliderFmEnv);
    drawSlider(sliderFmFb);
}

void handleOscFmTouch(int tx, int ty, bool newPress) {
    if (newPress && btnHit(btnFmSnap, tx, ty)) {
        fmSnap = !fmSnap;
        if (fmSnap) paramFmRatio = fmSnapRatio(paramFmRatio);
        drawFmTop();
        drawSlider(sliderFmRatio);
        return;
    }
    if (touchSlider(sliderFmAmt, tx, ty))  { return; }
    if (touchSlider(sliderFmRatio, tx, ty)) return;      // o snap é feito dentro de touchSlider
    if (touchSlider(sliderFmDec, tx, ty)) return;
    if (touchSlider(sliderFmEnv, tx, ty)) return;
    touchSlider(sliderFmFb, tx, ty);
}

void updateOscFmPage() {
    static bool lastActive = false;
    bool act = (paramFmAmt > 0.01f);
    if (act != lastActive) { lastActive = act; drawFmTop(); }
    refreshSlider(sliderFmAmt);
    refreshSlider(sliderFmRatio);
    refreshSlider(sliderFmDec);
    refreshSlider(sliderFmEnv);
    refreshSlider(sliderFmFb);
}

// ============================================================================
// UI DA ABA OSC (página 4): OSC2 com hard sync
// ============================================================================
int o2Drawn = -1;

void drawO2Top() {
    for (int k = 0; k < 4; k++) drawButtonEx(btnO2Wave[k], o2Wave == k, btnO2Wave[k].label, 1);
    drawButtonEx(btnO2Sync, o2Sync, o2Sync ? "HARD SYNC: ON" : "HARD SYNC: OFF", 1);
    drawButtonEx(btnO2Ring, o2Ring, o2Ring ? "RING MOD: ON" : "RING MOD: OFF", 1);
    o2Drawn = (int)o2Wave | (o2Sync ? 8 : 0) | (o2Ring ? 16 : 0);
}

void drawOsc2bPage() {
    drawO2Top();
    drawSlider(sliderO2Lvl);
    drawSlider(sliderO2Semi);
    drawSlider(sliderO2Det);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 194); tft.print("SYNC: OSC2 RESTARTS WITH OSC1. RING: OSC1 x OSC2");
    tft.setCursor(8, 206); tft.print("+12 ST = OCTAVE UP, +7 = FIFTH. FINE = DETUNE.");
}

void handleOsc2bTouch(int tx, int ty, bool newPress) {
    if (newPress) {
        for (int k = 0; k < 4; k++) {
            if (btnHit(btnO2Wave[k], tx, ty)) { o2Wave = (uint8_t)k; drawO2Top(); return; }
        }
        if (btnHit(btnO2Sync, tx, ty)) { o2Sync = !o2Sync; drawO2Top(); return; }
        if (btnHit(btnO2Ring, tx, ty)) { o2Ring = !o2Ring; drawO2Top(); return; }
    }
    if (touchSlider(sliderO2Lvl, tx, ty)) return;
    if (touchSlider(sliderO2Semi, tx, ty)) return;
    touchSlider(sliderO2Det, tx, ty);
}

void updateOsc2bPage() {
    if (o2Drawn != ((int)o2Wave | (o2Sync ? 8 : 0) | (o2Ring ? 16 : 0))) drawO2Top();
    refreshSlider(sliderO2Lvl);
    refreshSlider(sliderO2Semi);
    refreshSlider(sliderO2Det);
}

// ============================================================================
// UI DA ABA OSC (página 5): UNISON
// ============================================================================
int uniDrawn = -1;

void drawUniTop() {
    for (int k = 0; k < 4; k++) drawButtonEx(btnUni[k], uniSel == k, btnUni[k].label, 2);
    uniDrawn = uniSel;
}

void drawUniPage() {
    drawUniTop();
    drawSlider(sliderUniDet);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 112); tft.print("UNISON STACKS DETUNED COPIES OF EACH VOICE.");
    tft.setCursor(8, 124); tft.print("ONLY ON SAW/SQR/PLS/TRI/SINE (NOT SUPERSAW/NOISE).");
    tft.setCursor(8, 136); tft.print("MORE COPIES = MORE CPU: WATCH THE METER (MOD TAB).");
    tft.setCursor(8, 148); tft.print("FOR STEREO WIDTH USE THE CHORUS (FX TAB).");
}

void handleUniTouch(int tx, int ty, bool newPress) {
    if (newPress) {
        for (int k = 0; k < 4; k++) {
            if (btnHit(btnUni[k], tx, ty)) { uniSel = (uint8_t)k; drawUniTop(); return; }
        }
    }
    touchSlider(sliderUniDet, tx, ty);
}

void updateUniPage() {
    if (uniDrawn != (int)uniSel) drawUniTop();
    refreshSlider(sliderUniDet);
}

// ============================================================================
// UI DA ABA OSC (página 6): WAVETABLE
// Tocar num banco já seleciona a onda WT. A vista mostra os 8 quadros empilhados
// (o mais próximo da posição em destaque) e, à direita, a onda que está tocando
// agora, já com LFO e matriz aplicados.
// ============================================================================
Btn btnWtBank[4] = {{8, 36, 73, 26, "ANALOG"}, {85, 36, 73, 26, "VOCAL"}, {162, 36, 73, 26, "ORGAN"}, {239, 36, 73, 26, "SYNC"}};
TouchSlider sliderWtPos = {8, 146, 304, 30, &paramWtPos, 0.0f, 1.0f, "WT POSITION",        0, -999.0f};
TouchSlider sliderWtLfo = {8, 180, 304, 30, &paramWtLfo, 0.0f, 1.0f, "POSITION LFO AMOUNT", 0, -999.0f};
int     wtTopDrawn = -1;
int32_t wtViewPos = -100000;

// posição que está tocando (Q16); com outra onda selecionada, mostra a posição base
int32_t wtLivePos() {
    return (currentWave == WAVE_TABLE) ? wtPosCur : (int32_t)(paramWtPos * 65535.0f);
}

void drawWtTop() {
    bool on = (currentWave == WAVE_TABLE);
    for (int k = 0; k < 4; k++) drawButtonEx(btnWtBank[k], on && wtBank == k, btnWtBank[k].label, 1);
    wtTopDrawn = (int)(wtBank & 3) | (on ? 4 : 0);
}

void drawWtView(int x0, int y0, int h) {
    const int w = PANEL_W;
    const int bank = wtBank & 3;
    const int32_t pos = wtLivePos();
    panelCanvas.fillRect(0, 0, w, h, COL_BG);
    panelCanvas.drawRoundRect(0, 0, w, h, RADIUS, COL_BORDER);
    char t[44];
    if (currentWave == WAVE_TABLE)
        snprintf(t, sizeof(t), "WAVETABLE %s  POS %d%%", WT_BANK_NAMES[bank], (int)((pos * 100L + 32767) >> 16));
    else
        snprintf(t, sizeof(t), "WAVETABLE OFF - TAP A BANK");
    panelCanvas.setTextSize(1);
    panelCanvas.setTextColor(COL_MUTED);
    panelCanvas.setCursor(8, 5);
    panelCanvas.print(t);

    // quadros empilhados (de trás para a frente), destaque no mais próximo da posição
    const int nearF = (int)(((uint32_t)pos * 7u + 32768u) >> 16);
    for (int f = WT_FRAMES - 1; f >= 0; f--) {
        const int16_t *fr = wtData[0][bank][f];
        int xo = 8 + f * 6, ym = h - 13 - f * 5;
        uint16_t c = (f == nearF) ? COL_ACCENT : COL_MUTED;
        int prevX = xo, prevY = ym - ((fr[0] * 8) >> 15);
        for (int i = 1; i < 100; i++) {
            int px = xo + i, py = ym - ((fr[(i * 256) / 100] * 8) >> 15);
            panelCanvas.drawLine(prevX, prevY, px, py, c);
            prevX = px; prevY = py;
        }
    }

    // onda atual (interpolada entre os quadros, como no áudio)
    const int gx = 166, gw = 130, mid = 45, amp = 24;
    panelCanvas.drawFastVLine(gx - 6, 18, h - 24, COL_MUTED);
    panelCanvas.drawFastHLine(gx, mid, gw, COL_MUTED);
    uint32_t p7 = (uint32_t)pos * 7u;
    int fi = (int)(p7 >> 16);
    if (fi > WT_FRAMES - 2) fi = WT_FRAMES - 2;
    int32_t frac = (int32_t)((p7 & 0xFFFF) >> 2);
    const int16_t *fa = wtData[0][bank][fi], *fb = wtData[0][bank][fi + 1];
    int prevX = gx, prevY = mid;
    for (int i = 0; i < gw; i++) {
        int k = (i * 256) / gw;
        int32_t s = fa[k] + (((fb[k] - fa[k]) * frac) >> 14);
        int py = mid - (int)((s * amp) >> 15);
        int px = gx + i;
        if (i > 0) panelCanvas.drawLine(prevX, prevY, px, py, COL_ACCENT2);
        prevX = px; prevY = py;
    }
    wtViewPos = pos;
    tft.drawRGBBitmap(x0, y0, panelCanvas.getBuffer(), w, h);
}

void drawWtPage() {
    drawWtTop();
    drawWtView(8, 66, 75);
    drawSlider(sliderWtPos);
    drawSlider(sliderWtLfo);
}

void handleWtTouch(int tx, int ty, bool newPress) {
    if (newPress) {
        for (int k = 0; k < 4; k++) {
            if (btnHit(btnWtBank[k], tx, ty)) {
                wtBank = (uint8_t)k;
                currentWave = WAVE_TABLE;
                drawWtTop();
                drawWtView(8, 66, 75);
                return;
            }
        }
    }
    if (touchSlider(sliderWtPos, tx, ty)) return;
    touchSlider(sliderWtLfo, tx, ty);
}

void updateWtPage() {
    int topNow = (int)(wtBank & 3) | ((currentWave == WAVE_TABLE) ? 4 : 0);
    if (wtTopDrawn != topNow) { drawWtTop(); drawWtView(8, 66, 75); }
    else if (abs(wtLivePos() - wtViewPos) > 256) drawWtView(8, 66, 75);   // ~0,4% da tabela
    refreshSlider(sliderWtPos);
    refreshSlider(sliderWtLfo);
}

// ============================================================================
// UI DA ABA PRE (presets + teclado virtual para renomear + MIDI MAP)
// ============================================================================
const int PRE_SLOT_W = 71, PRE_SLOT_H = 40, PRE_SLOT_Y = 38, PRE_ROW_GAP = 6;

void presetSlotXY(int i, int &x, int &y) {
    int c = i % 4, r = i / 4;
    x = 8 + c * 77;
    y = PRE_SLOT_Y + r * (PRE_SLOT_H + PRE_ROW_GAP);
}

void drawPresetSlot(int i) {
    int x, y; presetSlotXY(i, x, y);
    bool sel = (presetCur == i);
    uint16_t fg = sel ? COL_BG : COL_TEXT;
    tft.fillRoundRect(x, y, PRE_SLOT_W, PRE_SLOT_H, RADIUS, sel ? COL_BORDER : COL_BG);
    tft.drawRoundRect(x, y, PRE_SLOT_W, PRE_SLOT_H, RADIUS, sel ? COL_BORDER : COL_MUTED);
    tft.setTextSize(2);
    tft.setTextColor(fg);
    tft.setCursor(x + 6, y + 5);
    tft.print(i + 1);
    tft.setTextSize(1);
    tft.setTextColor(sel ? COL_BG : ((slotIsUser[i] || slotHasName[i]) ? COL_TEXT : COL_MUTED));
    tft.setCursor(x + 6, y + 27);
    tft.print(presetName(i));
}

void drawPresetSlots() { for (int i = 0; i < PRESET_COUNT; i++) drawPresetSlot(i); }

void drawPresetButtons() {
    bool saveArmed = (saveArmUntil != 0 && millis() < saveArmUntil);
    bool factArmed = (factArmUntil != 0 && millis() < factArmUntil);
    drawButtonEx(btnPreSave, saveArmed, saveArmed ? "SURE?" : "SAVE", 1);
    drawBtn(btnPreName, false);
    drawButtonEx(btnPreFact, factArmed, factArmed ? "SURE?" : "FACTORY", 1);
    drawButtonEx(btnPrePtn, presetLoadPattern, presetLoadPattern ? "PTN ON" : "PTN OFF", 1);
    drawButtonEx(btnPreRnd, false, "RANDOM", 2);
    drawButtonEx(btnPreUndo, false, (rndUndoState == 2) ? "REDO" : "UNDO", 2);
    if (rndUndoState == 0) tft.drawFastHLine(btnPreUndo.x + 40, btnPreUndo.y + 14, btnPreUndo.w - 80, COL_MUTED);
}

void presetShowMsg() {
    tft.fillRect(8, 200, 304, 18, COL_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(8, 200);
    tft.print(presetMsg);
    tft.setTextSize(1);
}

const char NAME_CHARS[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
char nameBuf[NAME_LEN];
int  nameLen = 0;

void nameCellXY(int i, int &x, int &y) {
    x = 11 + (i % 10) * 30;
    y = 70 + (i / 10) * 34;
}

void drawNameField() {
    tft.fillRoundRect(8, 38, 304, 26, RADIUS, COL_BG);
    tft.drawRoundRect(8, 38, 304, 26, RADIUS, COL_BORDER);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(16, 43);
    tft.print(nameBuf);
    if (nameLen < 10) tft.print("_");
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d/10", nameLen);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(276, 47);
    tft.print(cnt);
}

void drawNameKey(int i) {
    int x, y; nameCellXY(i, x, y);
    const int w = 27, h = 30;
    bool special = (i >= 36);
    bool okKey = (i == 38);
    tft.fillRoundRect(x, y, w, h, 4, okKey ? COL_BORDER : COL_BG);
    tft.drawRoundRect(x, y, w, h, 4, special ? COL_BORDER : COL_MUTED);
    tft.setTextColor(okKey ? COL_BG : COL_TEXT);
    if (!special) {
        char c[2] = {NAME_CHARS[i], 0};
        tft.setTextSize(2);
        tft.setCursor(x + 8, y + 7);
        tft.print(c);
    } else {
        const char* lab[4] = {"SPC", "DEL", "OK", "ESC"};
        tft.setTextSize(1);
        int tw = (int)strlen(lab[i - 36]) * 6;
        tft.setCursor(x + (w - tw) / 2, y + 11);
        tft.print(lab[i - 36]);
    }
    tft.setTextSize(1);
}

void drawNameEditor() {
    tft.fillRect(0, 32, 320, 190, COL_BG);
    drawNameField();
    for (int i = 0; i < 40; i++) drawNameKey(i);
}

void startNameEdit() {
    strncpy(nameBuf, presetName(presetCur), NAME_LEN - 1);
    nameBuf[NAME_LEN - 1] = 0;
    if (strlen(nameBuf) > 10) nameBuf[10] = 0;
    nameLen = (int)strlen(nameBuf);
    nameEdit = true;
    drawNameEditor();
}

// ---- PRE página 2: MIDI MAP (learn dos knobs) ----
void mapCellXY(int i, int &x, int &y) {
    x = 8 + (i % 4) * 77;
    y = 36 + (i / 4) * 36;
}

void drawMapCell(int i) {
    int x, y; mapCellXY(i, x, y);
    bool learning = (mapLearnIdx == i);
    tft.fillRoundRect(x, y, 71, 32, RADIUS, learning ? COL_BORDER : COL_BG);
    tft.drawRoundRect(x, y, 71, 32, RADIUS, learning ? COL_BORDER : COL_MUTED);
    tft.setTextSize(1);
    tft.setTextColor(learning ? COL_BG : COL_TEXT);
    tft.setCursor(x + 4, y + 5);
    tft.print(MAP_NAMES[i]);
    char cc[16];
    if (learning)            snprintf(cc, sizeof(cc), "MOVE KNOB");
    else if (mapCC[i] < 120) snprintf(cc, sizeof(cc), "CC %d", mapCC[i]);
    else                     snprintf(cc, sizeof(cc), "--");
    tft.setTextColor(learning ? COL_BG : COL_MUTED);
    tft.setCursor(x + 4, y + 19);
    tft.print(cc);
}

void drawMapPage() {
    for (int i = 0; i < MAP_COUNT; i++) drawMapCell(i);
    drawBtn(btnMapDef, false);
    drawBtn(btnMapClr, false);
    drawBtn(btnTchCal, false);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 208);
    tft.print("TAP A PARAMETER, THEN MOVE A KNOB OR SLIDER");
    mapLearnDrawn = mapLearnIdx;
    mapCntDrawn = mapChangeCnt;
}

void handleMapTouch(int tx, int ty, bool newPress) {
    if (!newPress) return;
    for (int i = 0; i < MAP_COUNT; i++) {
        int x, y; mapCellXY(i, x, y);
        if (tx >= x && tx < x + 71 && ty >= y && ty < y + 32) {
            mapLearnIdx = (mapLearnIdx == i) ? -1 : i;
            drawMapPage();
            return;
        }
    }
    if (btnHit(btnMapDef, tx, ty)) {
        for (int i = 0; i < MAP_COUNT; i++) mapCC[i] = (i < 8) ? CC_KNOB[i] : 255;
        mapLearnIdx = -1;
        mapChangeCnt++;
        mapDirty = true;
        drawMapPage();
        return;
    }
    if (btnHit(btnMapClr, tx, ty)) {
        for (int i = 0; i < MAP_COUNT; i++) mapCC[i] = 255;
        mapLearnIdx = -1;
        mapChangeCnt++;
        mapDirty = true;
        drawMapPage();
        return;
    }
    if (btnHit(btnTchCal, tx, ty)) {
        drawBtn(btnTchCal, true);
        touchCalReq = true;
        return;
    }
}

void updateMapPage() {
    if (mapLearnDrawn != (int)mapLearnIdx || mapCntDrawn != (int)mapChangeCnt) drawMapPage();
}

// ============================================================================
// RANDOM INTELIGENTE: sorteia primeiro o "tipo" de som e depois cada parâmetro dentro
// de faixas que funcionam para aquele tipo. Volume, modo de voz e sequência não mudam.
// UNDO troca o som atual pelo de antes (tocar de novo = REDO).
// ============================================================================
#define SNAP_FLOATS(X) X(paramAttack) X(paramDecay) X(paramSustain) X(paramRelease) \
    X(paramCutoff) X(paramReso) X(paramFiltAmt) X(paramFiltAtt) X(paramFiltDec) X(paramDrive) X(paramFold) \
    X(paramLfoRate) X(paramLfoDepth) X(paramDetune) X(paramPwm) X(paramPwmMod) X(paramSubLevel) \
    X(paramPitchAmt) X(paramPitchDecMs) X(paramVelFilt) X(paramFmAmt) X(paramFmRatio) X(paramFmDecMs) \
    X(paramFmEnv) X(paramFmFb) X(paramO2Lvl) X(paramO2Semi) X(paramO2Det) X(paramUniDet) X(paramWtPos) \
    X(paramWtLfo) X(paramDelayTime) X(paramDelayFb) X(paramDelayMix) X(paramRevSize) X(paramRevDamp) \
    X(paramRevMix) X(paramChoRate) X(paramChoDepth) X(paramChoMix) X(paramCrBits) X(paramCrRate) X(paramCrMix)
#define SNAP_BYTES(X) X(subMode) X(filtMode) X(filtSlope) X(lfoShape) X(lfoSync) X(o2Wave) X(o2Sync) \
    X(o2Ring) X(uniSel) X(wtBank)

SoundSnap rndSnap;

void soundCapture(SoundSnap &sn) {
    int i = 0, k = 0;
#define CAP_F(v) sn.f[i++] = v;
#define CAP_B(v) sn.b[k++] = (uint8_t)(v);
    SNAP_FLOATS(CAP_F) SNAP_BYTES(CAP_B)
#undef CAP_F
#undef CAP_B
    for (int j = 0; j < 16; j++) sn.mm[j] = mmAmt[j];
    sn.wave = (uint8_t)currentWave;
}

void soundRestore(const SoundSnap &sn) {
    int i = 0, k = 0;
#define RES_F(v) v = sn.f[i++];
#define RES_B(v) v = sn.b[k++];
    SNAP_FLOATS(RES_F) SNAP_BYTES(RES_B)
#undef RES_F
#undef RES_B
    for (int j = 0; j < 16; j++) mmAmt[j] = sn.mm[j];
    currentWave = (Waveform)sn.wave;
    syncKnobsFromParams();
}

static float rr(float a, float b) { return a + (b - a) * (float)random(10001) / 10000.0f; }
static bool  chance(int pct) { return random(100) < pct; }
template <size_t N> static Waveform pickWave(const Waveform (&w)[N]) { return w[random(N)]; }

const char* const RND_KIND[5] = {"BASS", "LEAD", "PAD", "PLUCK", "KEYS"};

void soundRandomize() {
    soundCapture(rndSnap);
    rndUndoState = 1;
    randomSeed(micros() ^ (millis() << 8));
    int kind = (int)random(5);

    // ponto de partida neutro
    paramDrive = 0.0f; paramFold = 0.0f; paramPitchAmt = 0.0f; paramVelFilt = rr(0.0f, 0.35f);
    paramFmAmt = 0.0f; paramO2Lvl = 0.0f; o2Sync = false; o2Ring = false; uniSel = 0;
    paramSubLevel = 0.0f; paramPwmMod = 0.0f; paramWtLfo = 0.0f; paramLfoDepth = 0.0f;
    paramDelayMix = 0.0f; paramChoMix = 0.0f; paramCrMix = 0.0f; paramCrBits = 16.0f; paramCrRate = 1.0f;
    for (int j = 0; j < 16; j++) mmAmt[j] = 0.0f;
    filtMode = 0;
    filtSlope = chance(50) ? 1 : 0;
    paramPwm = rr(0.2f, 0.8f);
    paramDetune = rr(0.25f, 0.7f);
    lfoShape = (uint8_t)random(5);
    lfoSync = chance(30);
    paramLfoRate = rr(0.3f, 6.0f);
    paramRevSize = rr(0.35f, 0.85f); paramRevDamp = rr(0.2f, 0.7f);
    paramDelayTime = rr(120.0f, 480.0f); paramDelayFb = rr(0.2f, 0.55f);
    paramChoRate = rr(0.3f, 1.5f); paramChoDepth = rr(0.3f, 0.8f);

    if (kind == 0) {                                         // BASS: curto, grave, filtro fechado
        const Waveform w[] = {WAVE_SAW, WAVE_SQUARE, WAVE_PULSE, WAVE_SAW, WAVE_TABLE};
        currentWave = pickWave(w);
        paramAttack = rr(0.002f, 0.01f); paramDecay = rr(0.1f, 0.5f); paramSustain = rr(0.3f, 0.8f); paramRelease = rr(0.04f, 0.2f);
        paramCutoff = rr(0.2f, 0.5f); paramReso = rr(0.1f, 0.6f);
        paramFiltAmt = rr(0.3f, 0.8f); paramFiltAtt = rr(0.001f, 0.01f); paramFiltDec = rr(0.1f, 0.5f);
        if (chance(55)) { paramSubLevel = rr(0.3f, 0.7f); subMode = chance(50) ? 1 : 0; }
        if (chance(35)) { paramO2Lvl = rr(0.3f, 0.6f); const float st[] = {-12, 0, 7, 12}; paramO2Semi = st[random(4)]; paramO2Det = rr(-8, 8); o2Wave = (uint8_t)random(4); }
        if (chance(30)) paramDrive = rr(0.1f, 0.45f);
        if (chance(12)) { paramPitchAmt = rr(0.05f, 0.2f); paramPitchDecMs = rr(20, 80); }
        paramRevMix = rr(0.0f, 0.1f);
    } else if (kind == 1) {                                  // LEAD: brilhante, sustentado
        const Waveform w[] = {WAVE_SAW, WAVE_SQUARE, WAVE_PULSE, WAVE_SUPERSAW, WAVE_TABLE};
        currentWave = pickWave(w);
        paramAttack = rr(0.002f, 0.03f); paramDecay = rr(0.2f, 0.8f); paramSustain = rr(0.5f, 0.9f); paramRelease = rr(0.1f, 0.4f);
        paramCutoff = rr(0.45f, 0.8f); paramReso = rr(0.05f, 0.5f);
        paramFiltAmt = rr(0.1f, 0.5f); paramFiltAtt = rr(0.001f, 0.05f); paramFiltDec = rr(0.2f, 0.8f);
        if (chance(60)) {
            paramO2Lvl = rr(0.3f, 0.7f); o2Wave = (uint8_t)random(4);
            const float st[] = {0, 7, 12, -12, 5};
            paramO2Semi = st[random(5)]; paramO2Det = rr(-12, 12);
            o2Sync = chance(15);
        }
        if (chance(25)) { uniSel = 1; paramUniDet = rr(0.2f, 0.5f); }
        if (chance(50)) paramLfoDepth = rr(0.05f, 0.2f);
        if (chance(50)) paramDelayMix = rr(0.12f, 0.3f);
        paramRevMix = rr(0.08f, 0.25f);
    } else if (kind == 2) {                                  // PAD: lento, largo, com movimento
        const Waveform w[] = {WAVE_SUPERSAW, WAVE_SAW, WAVE_TABLE, WAVE_TRIANGLE, WAVE_PULSE};
        currentWave = pickWave(w);
        paramAttack = rr(0.3f, 1.5f); paramDecay = rr(0.5f, 1.5f); paramSustain = rr(0.6f, 1.0f); paramRelease = rr(0.8f, 2.5f);
        paramCutoff = rr(0.35f, 0.7f); paramReso = rr(0.0f, 0.3f);
        paramFiltAmt = rr(0.0f, 0.3f); paramFiltAtt = rr(0.2f, 1.0f); paramFiltDec = rr(0.5f, 2.0f);
        lfoShape = chance(70) ? (chance(50) ? 0 : 1) : lfoShape;
        paramLfoRate = rr(0.1f, 1.0f); paramLfoDepth = rr(0.08f, 0.3f);
        if (chance(50)) { uniSel = chance(50) ? 2 : 1; paramUniDet = rr(0.3f, 0.7f); }
        if (currentWave == WAVE_TABLE) { wtBank = (uint8_t)random(4); paramWtPos = rr(0.0f, 0.6f); paramWtLfo = rr(0.2f, 0.6f); }
        if (currentWave == WAVE_PULSE) paramPwmMod = rr(0.2f, 0.5f);
        if (chance(55)) paramChoMix = rr(0.3f, 0.6f);
        paramRevMix = rr(0.3f, 0.5f);
        if (chance(30)) paramDelayMix = rr(0.1f, 0.25f);
    } else if (kind == 3) {                                  // PLUCK: ataque seco, filtro que fecha rápido
        const Waveform w[] = {WAVE_SAW, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SINE, WAVE_PULSE, WAVE_TABLE};
        currentWave = pickWave(w);
        paramAttack = rr(0.001f, 0.005f); paramDecay = rr(0.15f, 0.6f); paramSustain = rr(0.0f, 0.15f); paramRelease = rr(0.1f, 0.5f);
        paramCutoff = rr(0.15f, 0.4f); paramReso = rr(0.1f, 0.5f);
        paramFiltAmt = rr(0.5f, 0.9f); paramFiltAtt = 0.001f; paramFiltDec = rr(0.08f, 0.4f);
        if (currentWave == WAVE_SINE || chance(25)) {
            const float ra[] = {2.0f, 3.0f, 3.5f, 4.0f, 1.5f};
            paramFmAmt = rr(0.2f, 0.5f); paramFmRatio = ra[random(5)]; paramFmDecMs = rr(60, 300); paramFmEnv = rr(0.5f, 1.0f);
        }
        if (chance(60)) paramDelayMix = rr(0.15f, 0.35f);
        paramRevMix = rr(0.15f, 0.3f);
    } else {                                                 // KEYS: piano elétrico/órgão
        const Waveform w[] = {WAVE_SINE, WAVE_TRIANGLE, WAVE_TABLE, WAVE_SQUARE};
        currentWave = pickWave(w);
        paramAttack = rr(0.002f, 0.02f); paramDecay = rr(0.4f, 1.2f); paramSustain = rr(0.2f, 0.6f); paramRelease = rr(0.2f, 0.6f);
        paramCutoff = rr(0.4f, 0.75f); paramReso = rr(0.0f, 0.25f);
        paramFiltAmt = rr(0.1f, 0.4f); paramFiltAtt = rr(0.001f, 0.02f); paramFiltDec = rr(0.3f, 1.0f);
        if (currentWave == WAVE_TABLE) wtBank = 2;           // ORGAN
        if (currentWave != WAVE_TABLE && chance(60)) {
            paramFmAmt = rr(0.1f, 0.35f); paramFmRatio = chance(50) ? 1.0f : 2.0f; paramFmDecMs = rr(200, 800); paramFmEnv = rr(0.6f, 1.0f);
        }
        if (chance(40)) { paramChoMix = rr(0.25f, 0.5f); paramChoRate = rr(0.4f, 1.2f); }
        paramRevMix = rr(0.12f, 0.3f);
    }

    // temperos raros em qualquer tipo
    if (chance(20) && kind != 0) filtMode = chance(50) ? 1 : 2;
    if (chance(12) && (currentWave == WAVE_SINE || currentWave == WAVE_TRIANGLE)) paramFold = rr(0.2f, 0.5f);
    if (chance(8)) { paramCrMix = rr(0.3f, 0.6f); paramCrBits = rr(6.0f, 10.0f); paramCrRate = rr(1.0f, 4.0f); }
    if (chance(8) && kind != 2) o2Ring = (paramO2Lvl > 0.0f);
    if (currentWave == WAVE_SQUARE || currentWave == WAVE_PULSE) { if (chance(30)) paramPwmMod = rr(0.15f, 0.45f); }
    if (currentWave == WAVE_TABLE && paramWtLfo == 0.0f) { wtBank = (wtBank > 3) ? 0 : wtBank; paramWtPos = rr(0.0f, 1.0f); }
    if (filtSlope == 1 && paramReso > 0.45f) paramReso = 0.45f;     // 24 dB + muita ressonância apita

    syncKnobsFromParams();
    snprintf(presetMsg, sizeof(presetMsg), "Random: %s", RND_KIND[kind]);
}

void soundUndo() {
    if (rndUndoState == 0) { snprintf(presetMsg, sizeof(presetMsg), "Nothing to undo"); return; }
    SoundSnap cur;
    soundCapture(cur);
    soundRestore(rndSnap);
    rndSnap = cur;                                   // tocar de novo refaz
    rndUndoState = (rndUndoState == 1) ? 2 : 1;
    snprintf(presetMsg, sizeof(presetMsg), (rndUndoState == 2) ? "Undone" : "Redone");
}

void drawPagePRE_static() {
    if (subPg[PAGE_PRE] == 1) { drawMapPage(); return; }
    if (nameEdit) { drawNameEditor(); return; }
    drawPresetSlots();
    drawPresetButtons();
    presetShowMsg();
}

void handleNameTouch(int tx, int ty, bool newPress) {
    if (!newPress) return;
    for (int i = 0; i < 40; i++) {
        int x, y; nameCellXY(i, x, y);
        if (tx < x || tx >= x + 27 || ty < y || ty >= y + 30) continue;

        if (i < 36 || i == 36) {
            if (nameLen < 10) {
                nameBuf[nameLen++] = (i == 36) ? ' ' : NAME_CHARS[i];
                nameBuf[nameLen] = 0;
                drawNameField();
            }
        } else if (i == 37) {
            if (nameLen > 0) { nameBuf[--nameLen] = 0; drawNameField(); }
        } else if (i == 38) {
            while (nameLen > 0 && nameBuf[nameLen - 1] == ' ') nameBuf[--nameLen] = 0;
            if (nameLen == 0) {
                slotHasName[presetCur] = false;
            } else {
                strncpy(slotName[presetCur], nameBuf, NAME_LEN - 1);
                slotName[presetCur][NAME_LEN - 1] = 0;
                slotHasName[presetCur] = true;
            }
            nameSaveAll();
            nameEdit = false;
            snprintf(presetMsg, sizeof(presetMsg), "Renamed slot %d", presetCur + 1);
            drawPagePRE_static();
        } else {
            nameEdit = false;
            drawPagePRE_static();
        }
        return;
    }
}

void presetRedrawUI() {
    if (nameEdit) return;
    if (subMenuFor >= 0) { switchPage(currentPage); return; }   // fecha o menu e redesenha tudo
    if (currentPage == PAGE_PRE) {
        if (subPg[PAGE_PRE] == 0) {
            drawPresetSlots();
            drawPresetButtons();
            presetShowMsg();
        }
    } else if (currentPage == PAGE_SEQ && subPg[PAGE_SEQ] == 0) {
        drawPatBtn();
        drawSeqGrid();
        drawBpmBox();
        drawSeqEditor();
    } else if (currentPage == PAGE_SEQ && subPg[PAGE_SEQ] == 2) {
        drawSeqPage3();
    }
}

void handlePresetTouch(int tx, int ty, bool newPress) {
    if (nameEdit) { handleNameTouch(tx, ty, newPress); return; }
    if (!newPress) return;

    for (int i = 0; i < PRESET_COUNT; i++) {
        int x, y; presetSlotXY(i, x, y);
        if (tx >= x && tx < x + PRE_SLOT_W && ty >= y && ty < y + PRE_SLOT_H) {
            saveArmUntil = 0; factArmUntil = 0;
            rndUndoState = 0;                       // outro som carregado: o UNDO do random não vale mais
            presetLoadCore(i);
            presetRedrawUI();
            return;
        }
    }

    if (btnHit(btnPreSave, tx, ty)) {
        if (saveArmUntil != 0 && millis() < saveArmUntil) {
            saveArmUntil = 0;
            presetSaveSlot(presetCur);
        } else {
            saveArmUntil = millis() + 2000;
            factArmUntil = 0;
            snprintf(presetMsg, sizeof(presetMsg), "Save to slot %d?", presetCur + 1);
            presetShowMsg();
        }
        drawPresetButtons();
        return;
    }
    if (btnHit(btnPreName, tx, ty)) {
        saveArmUntil = 0; factArmUntil = 0;
        startNameEdit();
        return;
    }
    if (btnHit(btnPreFact, tx, ty)) {
        if (factArmUntil != 0 && millis() < factArmUntil) {
            factArmUntil = 0;
            presetFactorySlot(presetCur);
        } else {
            factArmUntil = millis() + 2000;
            saveArmUntil = 0;
            snprintf(presetMsg, sizeof(presetMsg), "Reset slot %d?", presetCur + 1);
            presetShowMsg();
        }
        drawPresetButtons();
        return;
    }
    if (btnHit(btnPrePtn, tx, ty)) {
        presetLoadPattern = !presetLoadPattern;
        drawPresetButtons();
        return;
    }
    if (btnHit(btnPreRnd, tx, ty)) {
        saveArmUntil = 0; factArmUntil = 0;
        soundRandomize();
        drawPresetButtons();
        presetShowMsg();
        return;
    }
    if (btnHit(btnPreUndo, tx, ty)) {
        saveArmUntil = 0; factArmUntil = 0;
        soundUndo();
        drawPresetButtons();
        presetShowMsg();
        return;
    }
}

void updatePresetPage() {
    if (nameEdit) return;
    bool redraw = false;
    if (saveArmUntil != 0 && millis() >= saveArmUntil) { saveArmUntil = 0; redraw = true; }
    if (factArmUntil != 0 && millis() >= factArmUntil) { factArmUntil = 0; redraw = true; }
    if (redraw) drawPresetButtons();
}

// ============================================================================
// PÁGINAS
// ============================================================================
void drawPageOSC_static() {
    if (subPg[PAGE_OSC] == 0) {
        drawWaveGrid();
        drawScope(8, 122, 44);
        drawSlider(sliderVolume);
        lastWaveDisplay = (int)currentWave;
    } else if (subPg[PAGE_OSC] == 1) {
        drawOsc2Page();
    } else if (subPg[PAGE_OSC] == 2) {
        drawOscFmPage();
    } else if (subPg[PAGE_OSC] == 3) {
        drawOsc2bPage();
    } else if (subPg[PAGE_OSC] == 4) {
        drawUniPage();
    } else {
        drawWtPage();
    }
}

void drawPageENV_static() {
    uint8_t sp = subPg[PAGE_ENV];
    if (sp == 0) {
        drawADSR(ENV_PX0, ENV_PY0, ENV_PH);
    } else if (sp == 1) {
        drawFenvView(8, 38, 54);
        drawSlider(sliderFiltAmt);
        drawSlider(sliderFAtt);
        drawSlider(sliderFDec);
        drawSlider(sliderVelFilt);
    } else {
        drawPitchView(8, 38, 75);
        drawSlider(sliderPitchAmt);
        drawSlider(sliderPitchDec);
        tft.setTextSize(1);
        tft.setTextColor(COL_MUTED);
        tft.setCursor(8, 190); tft.print("THE NOTE STARTS HIGHER AND FALLS BACK:");
        tft.setCursor(8, 204); tft.print("KICKS, ZAPS, TOMS. SHORT DECAY = PUNCH.");
    }
}

void drawPageFLT_static() {
    if (subPg[PAGE_FLT] == 0) {
        drawFilterResponse(FLT_PX0, FLT_PY0, FLT_PH);
        drawSlider(sliderCutoff);
        drawSlider(sliderReso);
        drawSlider(sliderDrive);
    } else {
        drawFltPage2();
    }
}

void drawPageMOD_static() {
    uint8_t sp = subPg[PAGE_MOD];
    if (sp == 0) {
        drawLfoTop();
        drawLfoView(8, 64, 50);
        drawSlider(sliderLfoRate);
        drawSlider(sliderLfoDepth);
        drawSlider(sliderDetune);
    } else if (sp == 1) {
        drawMmPage();
    } else {
        drawVoicePage();
    }
}

void drawPageSEQ_static() {
    seqRedrawMask = 0;
    seqEditorDirty = false;
    if (subPg[PAGE_SEQ] == 0) {
        seqLastDrawnPlay = (seqRunning && seqPlayPat == seqEditPat) ? seqPlayStep : -1;
        seqUiPlay = seqRunning;
        drawPlayButton();
        drawButtonEx(btnBpmDn, false, "-", 2);
        drawBpmBox();
        drawButtonEx(btnBpmUp, false, "+", 2);
        drawPatBtn();
        drawBtn(btnRnd, false);
        drawBtn(btnClr, false);
        drawSeqGrid();
        drawSeqEditor();
        drawRecButton();
        drawLearnButton();
    } else if (subPg[PAGE_SEQ] == 1) {
        drawSeqPage2();
    } else {
        drawSeqPage3();
    }
}

void drawPageFX_static() {
    drawFxToggle();
    drawFxSub();
}

int dragTarget = 0;

void switchPage(Page p) {
    currentPage = p;
    subMenuFor = -1;
    dragTarget = 0;
    sliderGrab = nullptr;
    nameEdit = false;
    mapLearnIdx = -1;
    tft.fillRect(0, 32, 320, 190, COL_BG);
    drawTabs();

    lastCorte[0] = lastAtk[0] = lastSus[0] = lastEdit[0] = lastMsgLine[0] = '\0';
    clrArmUntil = 0; saveArmUntil = 0; factArmUntil = 0; dupArmUntil = 0;
    undoPat = -1;

    switch (p) {
        case PAGE_OSC: drawPageOSC_static(); break;
        case PAGE_ENV: drawPageENV_static(); break;
        case PAGE_FLT: drawPageFLT_static(); break;
        case PAGE_MOD: drawPageMOD_static(); break;
        case PAGE_SEQ: drawPageSEQ_static(); break;
        case PAGE_FX:  drawPageFX_static();  break;
        case PAGE_PRE: drawPagePRE_static(); break;
    }
}

int pickEnvHandle(int tx, int ty) {
    int lx = tx - ENV_PX0, ly = ty - ENV_PY0;
    if (lx < 0 || lx >= PANEL_W || ly < 0 || ly >= ENV_PH) return 0;
    AdsrPts p = adsrPoints();
    int best = 0;
    long bestD = 1600;
    long d;
    d = (long)sq(lx - p.xa) + (long)sq(ly - p.yTop); if (d < bestD) { bestD = d; best = 11; }
    d = (long)sq(lx - p.xd) + (long)sq(ly - p.ySus); if (d < bestD) { bestD = d; best = 12; }
    d = (long)sq(lx - p.xr) + (long)sq(ly - p.yBot); if (d < bestD) { bestD = d; best = 13; }
    return best;
}

bool inFilterPanel(int tx, int ty) {
    return tx >= FLT_PX0 && tx < FLT_PX0 + PANEL_W && ty >= FLT_PY0 && ty < FLT_PY0 + FLT_PH;
}

void applyDrag(int tx, int ty) {
    float lx = (float)(tx - ENV_PX0);
    float ly = (float)(ty - ENV_PY0);
    if (dragTarget == 1) {
        float x = constrain(lx, (float)FLT_GX0, (float)(FLT_GX0 + FLT_GW));
        paramCutoff = constrain(cutoffHzToParam(fltXToFreq(x)), 0.05f, 1.0f);
        float db = fltYToDb(ly);
        if (filtMode == 1) db -= BP_GAIN_DB;
        db -= slopeOffsetDb();
        paramReso = dbToReso(db);
    } else if (dragTarget >= 11 && dragTarget <= 13) {
        AdsrPts p = adsrPoints();
        if (dragTarget == 11) {
            paramAttack = constrain((lx - (float)ENV_GX0) / ENV_SEG * 2.0f, 0.002f, 2.0f);
        } else if (dragTarget == 12) {
            paramDecay = constrain((lx - (float)p.xa) / ENV_SEG * 2.0f, 0.002f, 2.0f);
            paramSustain = constrain(((float)p.yBot - ly) / (float)(ENV_GH - 8), 0.0f, 1.0f);
        } else {
            paramRelease = constrain((lx - (float)p.xs) / ENV_SEG * 3.0f, 0.002f, 3.0f);
        }
    }
}

// chamado por pollTouch quando o dedo sai da tela
void touchReleased() {
    dragTarget = 0;
    TouchSlider *g = sliderGrab;
    sliderGrab = nullptr;
    if (g && sliderFineDiv > 1) { sliderFineDiv = 1; drawSlider(*g); }   // apaga a etiqueta FINE
    sliderFineDiv = 1;
}

void handleTouch(int tx, int ty, bool newPress) {
    touchIsNew = newPress;
    if (newPress) { dragTarget = 0; sliderGrab = nullptr; }

    if (!newPress && dragTarget != 0) {
        applyDrag(tx, ty);
        syncKnobsFromParams();
        return;
    }

    // arraste de slider pode subir até a faixa das abas (fine mode); senão ignora
    if (ty < 32 && !newPress && sliderGrab == nullptr) return;
    // menu de subpáginas aberto: um toque escolhe um item ou fecha o menu
    if (subMenuFor >= 0) {
        if (!newPress) return;
        int k = subMenuHit(tx, ty);
        int tab = subMenuFor;
        subMenuFor = -1;
        if (k >= 0) {
            if (tab == PAGE_FX) fxSubPage = k;
            else subPg[tab] = (uint8_t)k;
            switchPage((Page)tab);
            return;
        }
        int idx = (tx - TAB_X0) / (TAB_W + TAB_GAP);
        if (ty < 32 && idx >= 0 && idx < TAB_COUNT && idx != tab) switchPage((Page)idx);
        else switchPage((Page)tab);                          // fora do menu: só fecha
        return;
    }

    if (ty < 32 && newPress) {
        int idx = (tx - TAB_X0) / (TAB_W + TAB_GAP);
        if (idx < 0 || idx >= TAB_COUNT) return;
        if (idx == (int)currentPage) {
            if (tabSubCount(idx) > 1) { subMenuFor = (int8_t)idx; drawSubMenu(); }
        } else {
            switchPage((Page)idx);
        }
        return;
    }

    switch (currentPage) {
        case PAGE_OSC:
            if (subPg[PAGE_OSC] == 0) {
                // forma de onda só no toque inicial (arrastar por cima dos ícones não troca a onda)
                if (!(newPress && touchWaveGrid(tx, ty))) touchSlider(sliderVolume, tx, ty);
            } else if (subPg[PAGE_OSC] == 1) {
                handleOsc2Touch(tx, ty, newPress);
            } else if (subPg[PAGE_OSC] == 2) {
                handleOscFmTouch(tx, ty, newPress);
            } else if (subPg[PAGE_OSC] == 3) {
                handleOsc2bTouch(tx, ty, newPress);
            } else if (subPg[PAGE_OSC] == 4) {
                handleUniTouch(tx, ty, newPress);
            } else {
                handleWtTouch(tx, ty, newPress);
            }
            syncKnobsFromParams();
            break;
        case PAGE_ENV:
            if (subPg[PAGE_ENV] == 1) {
                if (!touchSlider(sliderFiltAmt, tx, ty))
                    if (!touchSlider(sliderFAtt, tx, ty))
                        if (!touchSlider(sliderFDec, tx, ty))
                            touchSlider(sliderVelFilt, tx, ty);
                break;
            }
            if (subPg[PAGE_ENV] == 2) {
                if (!touchSlider(sliderPitchAmt, tx, ty)) touchSlider(sliderPitchDec, tx, ty);
                break;
            }
            if (newPress) dragTarget = pickEnvHandle(tx, ty);
            if (dragTarget != 0) applyDrag(tx, ty);
            syncKnobsFromParams();
            break;
        case PAGE_FLT:
            if (subPg[PAGE_FLT] == 1) {
                handleFltPage2Touch(tx, ty, newPress);
                break;
            }
            if (newPress && inFilterPanel(tx, ty)) dragTarget = 1;
            if (dragTarget != 0) { applyDrag(tx, ty); }
            else {
                if (!touchSlider(sliderCutoff, tx, ty))
                    if (!touchSlider(sliderReso, tx, ty))
                        touchSlider(sliderDrive, tx, ty);
            }
            syncKnobsFromParams();
            break;
        case PAGE_MOD:
            if (subPg[PAGE_MOD] == 0) {
                handleLfoTouch(tx, ty, newPress);
            } else if (subPg[PAGE_MOD] == 1) {
                handleMmTouch(tx, ty, newPress);
            } else {
                handleVoicePageTouch(tx, ty, newPress);
            }
            break;
        case PAGE_SEQ:
            if (subPg[PAGE_SEQ] == 0)      handleSeqTouch(tx, ty, newPress);
            else if (subPg[PAGE_SEQ] == 1) handleSeqPage2Touch(tx, ty, newPress);
            else                           handleSeqPage3Touch(tx, ty, newPress);
            break;
        case PAGE_FX:
            handleFxTouch(tx, ty, newPress);
            break;
        case PAGE_PRE:
            if (subPg[PAGE_PRE] == 1) handleMapTouch(tx, ty, newPress);
            else                      handlePresetTouch(tx, ty, newPress);
            break;
        default: break;
    }
}

// medidor de saída (barra fina embaixo da tela): -48..0 dB + bloco de CLIP por 1 s
void drawLevelMeter() {
    static int lastW = -1;
    static int lastClip = -1;
    static float level = 0.0f;
    static unsigned long clipUntil = 0;
    int32_t pk = outPeak;
    outPeak = 0;
    if (outClip) { outClip = false; clipUntil = millis() + 1000; }
    float db = (pk > 0) ? 20.0f * log10f((float)pk / 32768.0f) : -60.0f;
    float target = constrain((db + 48.0f) / 48.0f, 0.0f, 1.0f);
    level = (target > level) ? target : level * 0.85f;          // sobe na hora, desce suave
    const int X = 8, Y = 234, W = 280, H = 4;
    int w = (int)(level * W);
    if (w != lastW) {
        tft.fillRect(X, Y, w, H, COL_ACCENT);
        tft.fillRect(X + w, Y, W - w, H, COL_BG);
        tft.drawFastHLine(X + w, Y + H / 2, W - w, COL_MUTED);
        lastW = w;
    }
    int clip = (millis() < clipUntil) ? 1 : 0;
    if (clip != lastClip) {
        tft.fillRect(X + W + 4, Y, 20, H, clip ? COL_ACCENT : COL_BG);
        if (!clip) tft.drawRect(X + W + 4, Y, 20, H, COL_MUTED);
        lastClip = clip;
    }
}

unsigned long lastDisplayUpdate = 0;
void updateDisplay() {
    if (millis() - lastDisplayUpdate < 40) return;
    lastDisplayUpdate = millis();

    // com o menu aberto a página não é redesenhada por cima dele
    if (subMenuFor < 0) switch (currentPage) {
        case PAGE_OSC: {
            if (subPg[PAGE_OSC] == 0) {
                if (lastWaveDisplay != (int)currentWave) {
                    int old = lastWaveDisplay;
                    lastWaveDisplay = (int)currentWave;
                    if (old >= 0) drawWaveIcon(old);
                    drawWaveIcon(lastWaveDisplay);
                }
                drawScope(8, 122, 44);
                refreshSlider(sliderVolume);
            } else if (subPg[PAGE_OSC] == 2) {
                updateOscFmPage();
            } else if (subPg[PAGE_OSC] == 3) {
                updateOsc2bPage();
            } else if (subPg[PAGE_OSC] == 4) {
                updateUniPage();
            } else if (subPg[PAGE_OSC] == 5) {
                updateWtPage();
            } else {
                static float lW = -1, lS = -1;
                if (paramPwm != lW || paramSubLevel != lS || osc2SubDrawn != (int)subMode) {
                    if (osc2SubDrawn != (int)subMode) drawOsc2Toggle();
                    drawOsc2Graph(8, 66, 46);
                    lW = paramPwm; lS = paramSubLevel;
                }
                refreshSlider(sliderPwm);
                refreshSlider(sliderPwmMod);
                refreshSlider(sliderSubLvl);
            }
            break;
        }
        case PAGE_ENV: {
            uint8_t sp = subPg[PAGE_ENV];
            if (sp == 0) {
                static float lA = -1, lD = -1, lS = -1, lR = -1;
                if (paramAttack != lA || paramDecay != lD || paramSustain != lS || paramRelease != lR) {
                    drawADSR(ENV_PX0, ENV_PY0, ENV_PH);
                    lA = paramAttack; lD = paramDecay; lS = paramSustain; lR = paramRelease;
                }
                updateTextField(8, 130, 304, 16, lastAtk, sizeof(lastAtk), COL_TEXT, 2,
                                "Atk: %.2fs | Dec: %.2fs", paramAttack, paramDecay);
                updateTextField(8, 156, 304, 16, lastSus, sizeof(lastSus), COL_TEXT, 2,
                                "Sus: %d%%  | Rel: %.2fs", (int)(paramSustain * 100), paramRelease);
                updateTextField(8, 190, 304, 10, lastCorte, sizeof(lastCorte), COL_MUTED, 1,
                                "%s", "DRAG THE 3 POINTS");
            } else if (sp == 1) {
                static float lAt = -1, lDe = -1, lAm = -1;
                if (paramFiltAtt != lAt || paramFiltDec != lDe || paramFiltAmt != lAm) {
                    drawFenvView(8, 38, 54);
                    lAt = paramFiltAtt; lDe = paramFiltDec; lAm = paramFiltAmt;
                }
                refreshSlider(sliderFiltAmt);
                refreshSlider(sliderFAtt);
                refreshSlider(sliderFDec);
                refreshSlider(sliderVelFilt);
            } else {
                static float lA = -1, lD = -1;
                if (paramPitchAmt != lA || paramPitchDecMs != lD) {
                    drawPitchView(8, 38, 75);
                    lA = paramPitchAmt; lD = paramPitchDecMs;
                }
                refreshSlider(sliderPitchAmt);
                refreshSlider(sliderPitchDec);
            }
            break;
        }
        case PAGE_FLT: {
            if (subPg[PAGE_FLT] == 0) {
                static float lC = -1, lR = -1;
                static int lMode = -1;
                int curMode = filtMode | (filtSlope << 2);
                if (paramCutoff != lC || paramReso != lR || lMode != curMode) {
                    drawFilterResponse(FLT_PX0, FLT_PY0, FLT_PH);
                    lC = paramCutoff; lR = paramReso; lMode = curMode;
                }
                refreshSlider(sliderCutoff);
                refreshSlider(sliderReso);
                refreshSlider(sliderDrive);
            } else {
                updateFltPage2();
            }
            break;
        }
        case PAGE_MOD: {
            if (subPg[PAGE_MOD] == 0) {
                if (lfoTopDrawn != lfoTopHash()) drawLfoTop();
                drawLfoView(8, 64, 50);
                refreshSlider(sliderLfoRate);
                refreshSlider(sliderLfoDepth);
                refreshSlider(sliderDetune);
            } else if (subPg[PAGE_MOD] == 1) {
                updateMmPage();
            } else {
                updateVoicePage();
            }
            break;
        }
        case PAGE_SEQ:
            if (subPg[PAGE_SEQ] == 0)      updateSeqPlayhead();
            else if (subPg[PAGE_SEQ] == 1) updateSeqPage2();
            else                           updateSeqPage3();
            break;
        case PAGE_FX: {
            if (fxSubPage == 0) {
                static float lT = -1, lF = -1, lM = -1;
                if (paramDelayTime != lT || paramDelayFb != lF || paramDelayMix != lM) {
                    drawDelayView(8, 66, 46);
                    lT = paramDelayTime; lF = paramDelayFb; lM = paramDelayMix;
                }
                refreshSlider(sliderDlyTime);
                refreshSlider(sliderDlyFb);
                refreshSlider(sliderDlyMix);
            } else if (fxSubPage == 1) {
                static float lS = -1, lD = -1, lM = -1;
                if (paramRevSize != lS || paramRevDamp != lD || paramRevMix != lM) {
                    drawReverbView(8, 66, 46);
                    lS = paramRevSize; lD = paramRevDamp; lM = paramRevMix;
                }
                refreshSlider(sliderRevSize);
                refreshSlider(sliderRevDamp);
                refreshSlider(sliderRevMix);
            } else if (fxSubPage == 2) {
                static float lR = -1, lD = -1, lM = -1;
                if (paramChoRate != lR || paramChoDepth != lD || paramChoMix != lM) {
                    drawChorusView(8, 66, 46);
                    lR = paramChoRate; lD = paramChoDepth; lM = paramChoMix;
                }
                refreshSlider(sliderChoRate);
                refreshSlider(sliderChoDepth);
                refreshSlider(sliderChoMix);
            } else {
                static float lB = -1, lR = -1, lM = -1;
                if (paramCrBits != lB || paramCrRate != lR || paramCrMix != lM) {
                    drawCrushView(8, 66, 46);
                    lB = paramCrBits; lR = paramCrRate; lM = paramCrMix;
                }
                refreshSlider(sliderCrBits);
                refreshSlider(sliderCrRate);
                refreshSlider(sliderCrMix);
            }
            break;
        }
        case PAGE_PRE:
            if (subPg[PAGE_PRE] == 1) updateMapPage();
            else                      updatePresetPage();
            break;
    }

    if (btnDown && btnQuiet) updateTextField(8, 222, 150, 10, lastEdit, sizeof(lastEdit), COL_WARN, 1, "Reso (knob): %d%%",
                                 (int)(paramReso * 100.0f + 0.5f));
    else         updateTextField(8, 222, 150, 10, lastEdit, sizeof(lastEdit), COL_WARN, 1, "Cutoff (knob): %d%%",
                                 (int)(paramCutoff * 100.0f + 0.5f));
    drawLevelMeter();
    updateTextField(160, 222, 152, 10, lastMsgLine, sizeof(lastMsgLine), COL_MUTED, 1, "%.12s #%u CPU%d%%",
                    lastMidiMsg, (unsigned)midiRxCount, (int)cpuPct);
}

// ============================================================================
// LEITURA DO TOQUE (máquina de estados: assenta -> arrasta -> solta)
//  - toque novo só vale depois de 2 leituras próximas (a 1ª, com pouca pressão, vem torta)
//  - no arraste aplica a leitura ANTERIOR (atraso de 1 leitura, ~5 ms): a última amostra
//    antes de levantar o dedo, que costuma pular, nunca chega à interface
//  - saltos isolados são descartados e a posição passa por um filtro leve (média móvel)
//  - soltar = sem toque por TOUCH_RELEASE_MS (cobre falhas curtas de pressão no arraste)
// ============================================================================
const uint8_t TOUCH_POLL_MS    = 5;
const uint8_t TOUCH_RELEASE_MS = 60;
const int     TOUCH_SETTLE_PX  = 10;
const int     TOUCH_JUMP_PX    = 40;

struct TouchTrack {
    bool down = false, hasPrev = false;
    int  sx0 = 0, sy0 = 0;         // onde o gesto começou (swipe)
    unsigned long t0 = 0;
    int  px = 0, py = 0;           // leitura anterior (ainda não aplicada)
    int  fxQ = 0, fyQ = 0;         // posição filtrada x4 (ponto fixo, evita "travar" 1 px abaixo)
    uint8_t outliers = 0;
    unsigned long lastSeen = 0, lastPoll = 0;
};
TouchTrack touchTr;

// SWIPE: arrastar na horizontal (fora de sliders e gráficos) troca a subpágina;
// para a esquerda = próxima, para a direita = anterior. Na aba FX troca DLY/REV/CHR/BIT.
const int      SWIPE_MIN_DX = 80, SWIPE_MAX_DY = 40;
const uint16_t SWIPE_MAX_MS = 700;

void checkSwipe(int x0, int y0, int x1, int y1, unsigned long ms) {
    int dx = x1 - x0, dy = y1 - y0;
    if (y0 < 32 || ms > SWIPE_MAX_MS || abs(dx) < SWIPE_MIN_DX || abs(dy) > SWIPE_MAX_DY) return;
    if (currentPage == PAGE_PRE && nameEdit) return;
    if (subMenuFor >= 0) return;
    int dir = (dx < 0) ? 1 : -1;
    if (currentPage == PAGE_FX) {
        fxSubPage = (fxSubPage + dir + 4) & 3;
        drawFxToggle(); drawFxSub();
        return;
    }
    int p = (int)currentPage;
    if (!pageHasSub(p)) return;
    int n = pageSubCount(p);
    subPg[p] = (uint8_t)((subPg[p] + dir + n) % n);
    switchPage((Page)p);
}

void pollTouch() {
    TouchTrack &t = touchTr;
    unsigned long now = millis();
    if (now - t.lastPoll < TOUCH_POLL_MS) return;
    t.lastPoll = now;

    int rx, ry;
    if (!getTouch(rx, ry)) {
        t.hasPrev = false;
        if (t.down && now - t.lastSeen > TOUCH_RELEASE_MS) {
            t.down = false;
            bool free = (sliderGrab == nullptr && dragTarget == 0);   // o gesto não pegou slider nem gráfico
            int ex = (t.fxQ + 2) >> 2, ey = (t.fyQ + 2) >> 2;
            touchReleased();
            if (free) checkSwipe(t.sx0, t.sy0, ex, ey, t.lastSeen - t.t0);
        }
        return;
    }
    t.lastSeen = now;
    if (!t.hasPrev) { t.px = rx; t.py = ry; t.hasPrev = true; return; }

    if (!t.down) {
        if (abs(rx - t.px) > TOUCH_SETTLE_PX || abs(ry - t.py) > TOUCH_SETTLE_PX) {
            t.px = rx; t.py = ry;          // ainda assentando
            return;
        }
        int sx = (rx + t.px) >> 1, sy = (ry + t.py) >> 1;
        t.down = true; t.outliers = 0;
        t.fxQ = sx << 2; t.fyQ = sy << 2;
        t.sx0 = sx; t.sy0 = sy; t.t0 = now;
        t.px = rx; t.py = ry;
        handleTouch(sx, sy, true);
        return;
    }

    int ax = t.px, ay = t.py;
    t.px = rx; t.py = ry;
    int fx = (t.fxQ + 2) >> 2, fy = (t.fyQ + 2) >> 2;
    if ((abs(ax - fx) > TOUCH_JUMP_PX || abs(ay - fy) > TOUCH_JUMP_PX) && ++t.outliers < 3) {
        handleTouch(fx, fy, false);        // mantém o gesto vivo (ex.: auto-repeat do BPM)
        return;
    }
    if (t.outliers >= 3) { t.fxQ = ax << 2; t.fyQ = ay << 2; }   // salto confirmado: segue o dedo
    else { t.fxQ += ((ax << 2) - t.fxQ) >> 1; t.fyQ += ((ay << 2) - t.fyQ) >> 1; }
    t.outliers = 0;
    handleTouch((t.fxQ + 2) >> 2, (t.fyQ + 2) >> 2, false);
}

// ============================================================================
// CALIBRAÇÃO DO TOUCH (bloqueia só o core da tela; o áudio continua tocando)
// Toca 2 cruzes nos cantos (definem a escala) e 1 no centro (confere o resultado).
// Entrada: botão TOUCH CAL (PRE p2) ou dedo segurado na tela ao ligar.
// ============================================================================
const int CAL_PT[3][2] = {{24, 24}, {296, 216}, {160, 120}};
const int CAL_TOL_PX   = 12;          // erro máximo aceito no ponto de conferência

void drawCalScreen(int k, const char *msg) {
    tft.fillScreen(COL_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(70, 70);  tft.print("TOUCH CAL");
    tft.setTextSize(1);
    char t[40];
    snprintf(t, sizeof(t), "TAP THE CENTER OF THE CROSS (%d/3)", k + 1);
    tft.setCursor(60, 94);  tft.print(t);
    if (msg) { tft.setTextColor(COL_MUTED); tft.setCursor(60, 150); tft.print(msg); }
    int x = CAL_PT[k][0], y = CAL_PT[k][1];
    tft.drawFastHLine(x - 12, y, 25, COL_BORDER);
    tft.drawFastVLine(x, y - 12, 25, COL_BORDER);
    tft.drawCircle(x, y, 6, COL_BORDER);
}

// espera um toque firme e devolve a média das leituras brutas; false = desistiu (timeout)
bool calReadRaw(int &rx, int &ry) {
    unsigned long t0 = millis();
    while (ts.touched()) { if (millis() - t0 > 5000) return false; delay(5); }
    for (;;) {
        t0 = millis();
        while (!ts.touched()) { if (millis() - t0 > 20000) return false; delay(5); }
        delay(40);                                   // deixa a pressão assentar
        long sx = 0, sy = 0; int n = 0;
        while (ts.touched() && n < 24) {
            TS_Point p = ts.getPoint();
            sx += p.x; sy += p.y; n++;
            delay(4);
        }
        while (ts.touched()) delay(5);
        delay(120);
        if (n >= 6) { rx = (int)(sx / n); ry = (int)(sy / n); return true; }
        // toque curto demais: tenta de novo no mesmo ponto
    }
}

void touchCalibrate() {
    TouchCal old = touchCal;
    const char *msg = nullptr;
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        int rx[2], ry[2];
        bool got = true;
        for (int k = 0; k < 2 && got; k++) {
            drawCalScreen(k, msg);
            got = calReadRaw(rx[k], ry[k]);
        }
        if (!got) break;
        if (abs(rx[1] - rx[0]) < 1000 || abs(ry[1] - ry[0]) < 1000) { msg = "POINTS TOO CLOSE - TRY AGAIN"; continue; }

        // extrapola a leitura bruta para as bordas da tela
        float kx = (float)(rx[1] - rx[0]) / (float)(CAL_PT[1][0] - CAL_PT[0][0]);
        float ky = (float)(ry[1] - ry[0]) / (float)(CAL_PT[1][1] - CAL_PT[0][1]);
        touchCal.x0 = (int16_t)(rx[0] - CAL_PT[0][0] * kx);
        touchCal.x1 = (int16_t)(rx[0] + (320 - CAL_PT[0][0]) * kx);
        touchCal.y0 = (int16_t)(ry[0] - CAL_PT[0][1] * ky);
        touchCal.y1 = (int16_t)(ry[0] + (240 - CAL_PT[0][1]) * ky);

        drawCalScreen(2, msg);
        int cx, cy;
        if (!calReadRaw(cx, cy)) break;
        int sx = map(cx, touchCal.x0, touchCal.x1, 0, 320);
        int sy = map(cy, touchCal.y0, touchCal.y1, 0, 240);
        if (abs(sx - CAL_PT[2][0]) <= CAL_TOL_PX && abs(sy - CAL_PT[2][1]) <= CAL_TOL_PX) ok = true;
        else msg = "CHECK POINT MISSED - TRY AGAIN";
    }

    tft.fillScreen(COL_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(40, 110);
    if (ok) { touchCalSave(); tft.print("CALIBRATION SAVED"); }
    else    { touchCal = old;  tft.print("CANCELLED"); }
    tft.setTextSize(1);
    delay(700);

    touchTr = TouchTrack();       // descarta o gesto em andamento
    touchReleased();
    tft.fillScreen(COL_BG);
    switchPage(currentPage);
}

// todos os sliders: no boot cada um guarda o valor inicial do código como padrão
// (toque duplo volta para ele)
TouchSlider *ALL_SLIDERS[] = {
    &sliderVolume, &sliderPwm, &sliderPwmMod, &sliderSubLvl,
    &sliderCutoff, &sliderReso, &sliderFiltAmt, &sliderDrive, &sliderFAtt, &sliderFDec, &sliderFold,
    &sliderLfoRate, &sliderLfoDepth, &sliderDetune, &sliderPitchAmt, &sliderPitchDec, &sliderVelFilt,
    &sliderGlide, &sliderMm, &sliderFmAmt, &sliderFmRatio, &sliderFmDec, &sliderFmEnv, &sliderFmFb,
    &sliderO2Lvl, &sliderO2Semi, &sliderO2Det, &sliderUniDet, &sliderWtPos, &sliderWtLfo,
    &sliderSwing, &sliderAccent,
    &sliderDlyTime, &sliderDlyFb, &sliderDlyMix, &sliderRevSize, &sliderRevDamp, &sliderRevMix,
    &sliderChoRate, &sliderChoDepth, &sliderChoMix, &sliderCrBits, &sliderCrRate, &sliderCrMix,
};

// ============================================================================
// TELA DE ABERTURA: números caindo (estilo Matrix) e o nome no meio (~3 s)
// Usa a mesma paleta da interface (COL_BG / COL_TEXT / cinzas), então fica com as
// mesmas cores do menu em qualquer painel (inclusive IPS que inverte as cores).
// Termina com uma linha de varredura que limpa a tela.
// ============================================================================
void bootSplash() {
    const uint16_t G_HEAD = COL_TEXT, G_MID = COL_MUTED, G_DIM = 0xA554, BLACK = COL_BG;
    const int COLS = 40, ROWS = 24, CW = 8, CH = 10;
    const int BX = 26, BY = 74, BW = 268, BH = 96;           // caixa do título
    int16_t head[COLS];
    uint8_t len[COLS], spd[COLS];
    randomSeed(micros());
    for (int c = 0; c < COLS; c++) {
        head[c] = -(int16_t)random(0, ROWS);
        len[c] = (uint8_t)random(6, 16);
        spd[c] = (uint8_t)random(1, 4);
    }
    tft.fillScreen(BLACK);

    const char *title = "TOBRI SYNTH";
    const char *by1 = "desenvolvido por";
    const char *by2 = "@t.obr.i  2026";
    const int FRAMES = 100, TITLE_AT = 40;
    for (int fr = 0; fr < FRAMES; fr++) {
        bool box = (fr >= TITLE_AT);
        if (fr == TITLE_AT) {
            tft.fillRoundRect(BX, BY, BW, BH, 8, BLACK);
            tft.drawRoundRect(BX, BY, BW, BH, 8, G_HEAD);
            tft.drawRoundRect(BX + 2, BY + 2, BW - 4, BH - 4, 6, G_DIM);
        }
        if (box) {                                           // título "digitado", uma letra por quadro
            int n = fr - TITLE_AT + 1;
            int tl = (int)strlen(title);
            tft.setTextSize(3);
            tft.setTextColor(G_HEAD);
            tft.setCursor(160 - tl * 9, BY + 16);
            for (int k = 0; k < tl && k < n; k++) tft.print(title[k]);
            if (n == tl + 2) {
                tft.drawFastHLine(BX + 30, BY + 46, BW - 60, G_DIM);
                tft.setTextSize(1);
                tft.setTextColor(G_MID);
                tft.setCursor(160 - (int)strlen(by1) * 3, BY + 54);
                tft.print(by1);
                tft.setTextSize(2);
                tft.setTextColor(G_HEAD);
                tft.setCursor(160 - (int)strlen(by2) * 6, BY + 68);
                tft.print(by2);
            }
            tft.setTextSize(1);
        }
        for (int c = 0; c < COLS; c++) {
            if (fr % spd[c]) continue;
            head[c]++;
            int x = c * CW;
            const int h = head[c];
            const int rows[4] = {h - 1, h, h - (int)len[c] / 2, h - (int)len[c]};
            for (int r : rows) {
                if (r < 0 || r >= ROWS) continue;
                int y = r * CH;
                if (box && x + CW > BX && x < BX + BW && y + CH > BY && y < BY + BH) continue;
                if (r == head[c] - (int)len[c]) { tft.fillRect(x, y, CW, CH, BLACK); continue; }
                uint16_t col = (r == head[c]) ? G_HEAD : (r == head[c] - 1) ? G_MID : G_DIM;
                tft.drawChar(x + 1, y + 1, (char)('0' + random(10)), col, BLACK, 1);
            }
            if (head[c] - (int)len[c] >= ROWS) {
                head[c] = -(int16_t)random(0, 8);
                len[c] = (uint8_t)random(6, 16);
                spd[c] = (uint8_t)random(1, 4);
            }
        }
        delay(30);
    }
    delay(500);

    // transição: linha de varredura descendo e apagando a abertura
    for (int y = 0; y < 240; y += 6) {
        tft.fillRect(0, y, 320, 6, COL_BG);
        if (y + 8 < 240) {
            tft.drawFastHLine(0, y + 6, 320, COL_TEXT);
            tft.drawFastHLine(0, y + 7, 320, COL_MUTED);
        }
        delay(8);
    }
}

// depois da abertura as abas "caem" uma a uma antes da página aparecer
void drawTabsIntro() {
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = TAB_X0 + i * (TAB_W + TAB_GAP);
        for (int y = -22; y <= 4; y += 13) {                      // desce em 3 passos
            tft.fillRect(x, 0, TAB_W, 32, COL_BG);
            tft.drawRoundRect(x, y, TAB_W, 26, RADIUS, COL_BORDER);
            tft.setTextSize(2);
            tft.setTextColor(COL_TEXT);
            tft.setCursor(x + (TAB_W - (int)strlen(PAGE_LABELS[i]) * 12) / 2, y + 4);
            tft.print(PAGE_LABELS[i]);
            delay(12);
        }
    }
    tft.setTextSize(1);
    delay(80);
}

// ============================================================================
// SETUP E LOOPS
// ============================================================================
void setup1() {
    delay(300);
    for (TouchSlider *sl : ALL_SLIDERS) sl->def = *sl->value;   // antes de carregar o preset

    encoderBegin();          // interrupção no core da tela (core 1)

    SPI1.setSCK(TFT_SCK);
    SPI1.setTX(TFT_MOSI);
    SPI1.setRX(TOUCH_MISO);
    SPI1.begin();

    tft.begin(20000000);
    tft.invertDisplay(false);
    tft.setRotation(1);

    ts.begin(SPI1);
    ts.setRotation(1);

    bootSplash();
    tft.fillScreen(COL_BG);

    presetBoot();
    touchCalLoad();
    publishParams();

    // dedo segurado na tela ao ligar (~1 s) = calibrar (saída de emergência se o toque estiver errado)
    unsigned long holdT0 = millis();
    while (ts.touched() && millis() - holdT0 < 1000) delay(10);
    if (ts.touched()) {
        tft.setTextColor(COL_TEXT);
        tft.setTextSize(2);
        tft.setCursor(40, 110);
        tft.print("RELEASE TO CALIBRATE");
        tft.setTextSize(1);
        touchCalibrate();      // termina com switchPage(currentPage)
    }
    tft.fillScreen(COL_BG);
    drawTabsIntro();
    switchPage(PAGE_ENV);

    core1_booting = false;
}

void loop1() {
    readEncoder();

    if (presetReq >= 0) {
        int r = presetReq;
        presetReq = -1;
        presetLoadCore(r);
        presetRedrawUI();
    }

    if (padMapDirty) {
        padMapDirty = false;
        padSaveMap();
    }

    if (mapDirty) {
        mapDirty = false;
        mapSave();
    }

    if (settingsDirty) {
        settingsDirty = false;
        EEPROM.write(0, EE_MAGIC);
        EEPROM.write(3, seqSyncExt ? 1 : 0);
        EEPROM.commit();
    }

    // converte os parâmetros em ponto fixo para o core de áudio (a cada ~4 ms)
    static unsigned long lastPub = 0;
    unsigned long nowPub = millis();
    if (nowPub - lastPub >= 4) {
        lastPub = nowPub;
        publishParams();
    }

    pollTouch();
    if (touchCalReq && !touchTr.down) {
        touchCalReq = false;
        touchCalibrate();
    }
    updateDisplay();
}

void setup() {
    Serial.begin(115200);

    USBDevice.setID(0x1209, 0xFFBE);
    USBDevice.setManufacturerDescriptor("Tobri Synth");
    USBDevice.setProductDescriptor("RP2040 Synth");
    usb_midi.begin();

    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.turnThruOff();
    MIDI.setHandleNoteOn(onNoteOn);
    MIDI.setHandleNoteOff(onNoteOff);
    MIDI.setHandleControlChange(onControlChange);
    MIDI.setHandleProgramChange(onProgramChange);
    MIDI.setHandleClock(onClock);
    MIDI.setHandleStart(onStart);
    MIDI.setHandleContinue(onContinue);
    MIDI.setHandleStop(onStop);

    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        sineTable[i] = (int16_t)(sinf((float)i * 2.0f * (float)M_PI / WAVETABLE_SIZE) * 32767.0f);
    }

    wtInit();                  // usa a sineTable acima
    initNoteTable();
    initFilterTable();
    initPitchModTable();
    seqUpdateTiming();
    publishParams();

    i2s.setBCLK(I2S_BCLK_PIN);
    i2s.setDATA(I2S_DATA_PIN);
    i2s.setBitsPerSample(16);
    i2s.setBuffers(6, 64);
    if (!i2s.begin(SAMPLE_RATE)) {
        Serial.println("Erro ao iniciar I2S!");
    }
}

void loop() {
    static uint32_t cpuBusyUs = 0, cpuSamples = 0;
    static unsigned long cpuWinStart = 0;

    for (int i = 0; i < 16 && MIDI.read(); i++) { }

    if (monoResetReq) {
        monoResetReq = false;
        synthAllNotesOff();
        monoCount = 0;
        monoVoiceNote = -1;
    }

    uint32_t t0 = micros();
    int n = audioProcess();
    if (n > 0) {
        cpuBusyUs += (uint32_t)(micros() - t0);
        cpuSamples += (uint32_t)n;
    }

    unsigned long now = millis();
    if (now - cpuWinStart >= 500) {
        if (cpuSamples > 0) {
            float perSample = (float)cpuBusyUs / (float)cpuSamples;
            int p = (int)(perSample * 3.2f + 0.5f);
            if (p < 0) p = 0;
            if (p > 100) p = 100;
            cpuPct = (uint8_t)p;
        }
        cpuBusyUs = 0;
        cpuSamples = 0;
        cpuWinStart = now;
    }
}