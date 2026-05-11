// ============================================================
// clavier_test.ino
// Test audio carre + activation progressive des sous-systemes
// Ouvrir ce dossier comme sketch Arduino separe.
//
// TEST_STAGE:
//   0 = audio seul
//   1 = audio + touches
//   2 = audio + touches + leds
//   3 = audio + touches + leds + potards
//   4 = audio + touches + leds + potards + ecran
// ============================================================

#include <Arduino.h>

#define SAMPLE_RATE_HZ 8192
#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_EXTERNAL_TIMED
#define MOZZI_AUDIO_RATE SAMPLE_RATE_HZ
#define CONTROL_RATE 256

#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/square_no_alias_2048_int8.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

// ==================== TEST PARAMETERS ====================
#define TEST_STAGE 0
#define TEST_ENABLE_BUTTONS (TEST_STAGE >= 1)
#define TEST_ENABLE_LEDS    (TEST_STAGE >= 2)
#define TEST_ENABLE_POTS    (TEST_STAGE >= 3)
#define TEST_ENABLE_DISPLAY (TEST_STAGE >= 4)

// ==================== PINS ====================
#define LED_PIN        23
#define I2C_SDA        21
#define I2C_SCL        22
#define OLED_SDA       26
#define OLED_SCL       27
#define I2S_LRC        19
#define I2S_BCLK       18
#define I2S_DIN         5

#define POT_VOLUME     34
#define POT_PARAM_1    35
#define POT_ENV        32
#define POT_BPM        33
#define POT_OCTAVE      2

#define ROWS            4
#define COLS            8
#define MAIN_BUTTONS    (ROWS * COLS)
#define EXTRA_BUTTONS   4
#define TOTAL_BUTTONS   (MAIN_BUTTONS + EXTRA_BUTTONS)
#define LEDS_PER_BUTTON 3
#define LED_COUNT       (TOTAL_BUTTONS * LEDS_PER_BUTTON)
#define BRIGHTNESS      120

constexpr TickType_t BUTTON_SCAN_TASK_DELAY_TICKS = 5;
constexpr TickType_t UI_TASK_DELAY_TICKS = 10;

const uint8_t extraMcpPins[EXTRA_BUTTONS] = {8, 11, 13, 15};
const uint8_t POT_PINS[5] = {POT_VOLUME, POT_PARAM_1, POT_ENV, POT_BPM, POT_OCTAVE};

// ==================== HARDWARE OBJECTS ====================
I2SClass i2s;
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);
Adafruit_MCP23X17 mcp1;
Adafruit_MCP23X17 mcp2;
Adafruit_MCP23X17 mcpExtra;
TwoWire buttonsWire = TwoWire(1);
U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

TaskHandle_t buttonScanTaskHandle = nullptr;
TaskHandle_t uiTaskHandle = nullptr;

// ==================== TEST STATE ====================
volatile uint32_t gMainMask = 0;
volatile uint8_t gExtraMask = 0;
volatile uint8_t gPressedCount = 0;

int gPotValues[5] = {0, 0, 0, 0, 0};
volatile uint16_t gSquareFreqHz = 220;
volatile int16_t gSquareAmplitude = 100;

Oscil<SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> gSquareOsc(SQUARE_NO_ALIAS_2048_DATA);

unsigned long gLastLedMs = 0;
unsigned long gLastPotMs = 0;
unsigned long gLastDisplayMs = 0;
uint8_t gLedPhase = 0;

// ==================== HELPERS ====================
static inline int invertPotValue(int raw) {
  return 4095 - constrain(raw, 0, 4095);
}

static inline void setButtonLeds(int buttonIndex, uint32_t color) {
  int base = buttonIndex * LEDS_PER_BUTTON;
  for (int i = 0; i < LEDS_PER_BUTTON; i++) {
    strip.setPixelColor(base + i, color);
  }
}

void scanButtonsOnce() {
#if TEST_ENABLE_BUTTONS
  uint16_t b1 = mcp1.readGPIOAB();
  uint16_t b2 = mcp2.readGPIOAB();
  uint32_t nextMainMask = 0;

  for (int i = 0; i < MAIN_BUTTONS; i++) {
    bool raw = (i < 16) ? !(b1 & (1 << i)) : !(b2 & (1 << (i - 16)));
    int row = i / COLS;
    int col = (COLS - 1) - (i % COLS);
    int key = row * COLS + col;
    if (raw) nextMainMask |= (1UL << key);
  }

  uint16_t bx = mcpExtra.readGPIOAB();
  uint8_t nextExtraMask = 0;
  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    bool raw = !(bx & (1 << extraMcpPins[i]));
    if (raw) nextExtraMask |= (1U << i);
  }

  gMainMask = nextMainMask;
  gExtraMask = nextExtraMask;
  gPressedCount = (uint8_t)(__builtin_popcount(nextMainMask) + __builtin_popcount((unsigned)nextExtraMask));
#endif
}

void readPotsOnce() {
#if TEST_ENABLE_POTS
  for (int i = 0; i < 5; i++) {
    gPotValues[i] = invertPotValue(analogRead(POT_PINS[i]));
  }
  gSquareFreqHz = (uint16_t)map(gPotValues[0], 0, 4095, 80, 880);
  gSquareAmplitude = (int16_t)map(gPotValues[1], 0, 4095, 2500, 14000);
#endif
}

void renderTestLeds() {
#if TEST_ENABLE_LEDS
  strip.clear();

#if TEST_ENABLE_BUTTONS
  for (int key = 0; key < MAIN_BUTTONS; key++) {
    bool pressed = ((gMainMask >> key) & 0x1U) != 0;
    uint32_t color = pressed ? strip.Color(0, 180, 40) : strip.Color(0, 0, 0);
    setButtonLeds(key, color);
  }

  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    int key = MAIN_BUTTONS + i;
    bool pressed = ((gExtraMask >> i) & 0x1U) != 0;
    uint32_t color = pressed ? strip.Color(180, 40, 0) : strip.Color(0, 0, 0);
    setButtonLeds(key, color);
  }
#else
  int active = gLedPhase % TOTAL_BUTTONS;
  setButtonLeds(active, strip.Color(0, 60, 180));
  gLedPhase++;
#endif

  strip.show();
#endif
}

void renderTestDisplay() {
#if TEST_ENABLE_DISPLAY
  char line[32];

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "CLAVIER TEST");

  snprintf(line, sizeof(line), "stage: %d", TEST_STAGE);
  u8g2.drawStr(0, 28, line);

  snprintf(line, sizeof(line), "freq: %u Hz", (unsigned)gSquareFreqHz);
  u8g2.drawStr(0, 44, line);

  snprintf(line, sizeof(line), "amp: %d", (int)gSquareAmplitude);
  u8g2.drawStr(0, 60, line);

  snprintf(line, sizeof(line), "buttons: %u", (unsigned)gPressedCount);
  u8g2.drawStr(0, 76, line);

  snprintf(line, sizeof(line), "pots: %d %d", gPotValues[0], gPotValues[1]);
  u8g2.drawStr(0, 92, line);

  u8g2.drawStr(0, 112, TEST_ENABLE_BUTTONS ? "btn:on" : "btn:off");
  u8g2.drawStr(52, 112, TEST_ENABLE_LEDS ? "led:on" : "led:off");
  u8g2.drawStr(0, 126, TEST_ENABLE_POTS ? "pot:on" : "pot:off");
  u8g2.drawStr(52, 126, TEST_ENABLE_DISPLAY ? "scr:on" : "scr:off");
  u8g2.sendBuffer();
#endif
}

void buttonScanTask(void* parameter) {
  (void)parameter;
  for (;;) {
    scanButtonsOnce();
    vTaskDelay(BUTTON_SCAN_TASK_DELAY_TICKS);
  }
}

void uiTask(void* parameter) {
  (void)parameter;
  for (;;) {
    unsigned long now = millis();

#if TEST_ENABLE_POTS
    if (now - gLastPotMs >= 100) {
      gLastPotMs = now;
      readPotsOnce();
    }
#endif

#if TEST_ENABLE_LEDS
    if (now - gLastLedMs >= 40) {
      gLastLedMs = now;
      renderTestLeds();
    }
#endif

#if TEST_ENABLE_DISPLAY
    if (now - gLastDisplayMs >= 120) {
      gLastDisplayMs = now;
      renderTestDisplay();
    }
#endif

    vTaskDelay(UI_TASK_DELAY_TICKS);
  }
}

// ============================================================
//  MOZZI CALLBACKS
// ============================================================
void updateControl() {
#if TEST_ENABLE_POTS
  gSquareOsc.setFreq((float)gSquareFreqHz);
#endif
}

#if (MOZZI_COMPATIBILITY_LEVEL <= MOZZI_COMPATIBILITY_1_1) && MOZZI_IS(MOZZI_AUDIO_CHANNELS, MOZZI_MONO)
AudioOutput_t updateAudio() {
#else
AudioOutput updateAudio() {
#endif
  int16_t sample = gSquareOsc.next();
  int32_t scaled = ((int32_t)sample * (int32_t)gSquareAmplitude) / 127;
  if (scaled > 32767) scaled = 32767;
  if (scaled < -32768) scaled = -32768;
  return MonoOutput::from16Bit((int16_t)scaled);
}

void audioOutput(const AudioOutput f) {
  int32_t mono = (int32_t)f.l();
#if MOZZI_AUDIO_BITS < 16
  mono <<= (16 - MOZZI_AUDIO_BITS);
#elif MOZZI_AUDIO_BITS > 16
  mono >>= (MOZZI_AUDIO_BITS - 16);
#endif
  if (mono > 32767) mono = 32767;
  if (mono < -32768) mono = -32768;
  int16_t sample16 = (int16_t)mono;
  int16_t stereo[2] = {sample16, sample16};
  i2s.write((uint8_t*)stereo, sizeof(stereo));
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();

  Serial.println();
  Serial.println("[TEST] === CLAVIER TEST INIT ===");
  Serial.print("[TEST] stage=");
  Serial.println(TEST_STAGE);

  analogReadResolution(12);

  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[TEST] ERROR: I2S init failed");
    while (1) {
    }
  }

  gSquareOsc.setFreq((float)gSquareFreqHz);

#if TEST_ENABLE_LEDS
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
  Serial.println("[TEST] leds enabled");
#endif

#if TEST_ENABLE_DISPLAY
  Wire.begin(OLED_SDA, OLED_SCL, 400000);
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  delay(20);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 16, "CLAVIER TEST");
  u8g2.drawStr(0, 32, "display enabled");
  u8g2.sendBuffer();
  Serial.println("[TEST] display enabled");
#endif

  buttonsWire.begin(I2C_SDA, I2C_SCL, 400000);
  buttonsWire.setTimeOut(20);
  delay(50);

#if TEST_ENABLE_BUTTONS
  if (!mcp1.begin_I2C(0x21, &buttonsWire)) {
    Serial.println("[TEST] ERROR: MCP1 init failed");
    while (1) {
    }
  }
  if (!mcp2.begin_I2C(0x23, &buttonsWire)) {
    Serial.println("[TEST] ERROR: MCP2 init failed");
    while (1) {
    }
  }
  if (!mcpExtra.begin_I2C(0x26, &buttonsWire)) {
    Serial.println("[TEST] ERROR: MCPExtra init failed");
    while (1) {
    }
  }

  for (int i = 0; i < 16; i++) {
    mcp1.pinMode(i, INPUT_PULLUP);
    mcp2.pinMode(i, INPUT_PULLUP);
  }
  for (int i = 0; i < EXTRA_BUTTONS; i++) {
    mcpExtra.pinMode(extraMcpPins[i], INPUT_PULLUP);
  }

  scanButtonsOnce();
  Serial.println("[TEST] buttons enabled");
#endif

  xTaskCreatePinnedToCore(buttonScanTask, "buttonScan", 4096, nullptr, 1, &buttonScanTaskHandle, 1);
  Serial.println("[TEST] button task enabled");

#if TEST_ENABLE_POTS || TEST_ENABLE_LEDS || TEST_ENABLE_DISPLAY || !TEST_ENABLE_BUTTONS
  readPotsOnce();
  xTaskCreatePinnedToCore(uiTask, "uiTask", 6144, nullptr, 1, &uiTaskHandle, 1);
  Serial.println("[TEST] ui task enabled");
#endif

  Serial.println("[TEST] starting Mozzi square");
  Serial.flush();
  startMozzi(CONTROL_RATE);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  audioHook();
  delay(0);
}