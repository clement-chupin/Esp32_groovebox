#pragma once

#include <Arduino.h>

// ==================== SAFE MODE ====================
// Active pour tester l'audio seul sans I2C/SPI/LEDs
#define SAFE_MODE 0  // 0 = normal, 1 = désactive touches/LEDs/écran

// ==================== SAMPLE RATE ====================
#define SAMPLE_RATE_HZ  16384  // meilleure qualite audio avec optimisations DSP

// ==================== MOZZI CONFIG ====================
#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_I2S_DAC
#define MOZZI_AUDIO_RATE SAMPLE_RATE_HZ
#define MOZZI_AUDIO_BITS 16
#define CONTROL_RATE 256

// Debug potards: desactive par defaut pour eviter les saccades audio dues au port serie.
#define ENABLE_POT_DEBUG 0
#define ENABLE_DRUM_DEBUG 0

// ==================== PINS ====================
#define LED_PIN        23
#define I2C_SDA        21
#define I2C_SCL        22
#define OLED_SDA       26
#define OLED_SCL       27
#define OLED_I2C_ADDR  0x3C
#define I2S_LRC        19
#define I2S_BCLK       18
#define I2S_DIN         5

// Mozzi ESP32 native I2S pin mapping
#define MOZZI_I2S_PIN_WS   I2S_LRC
#define MOZZI_I2S_PIN_BCK  I2S_BCLK
#define MOZZI_I2S_PIN_DATA I2S_DIN

#define POT_VOLUME     34   // volume maître
#define POT_PARAM_1    35   // niveau effet / amplitude drum
#define POT_ENV        32   // parametre effet 1
#define POT_BPM        33   // parametre effet 2
#define POT_OCTAVE     2    // octave

// ==================== MATRIX / LEDS ====================
#define ROWS            4
#define COLS            8
#define MAIN_BUTTONS    (ROWS * COLS)
#define EXTRA_BUTTONS   4
#define TOTAL_BUTTONS   (MAIN_BUTTONS + EXTRA_BUTTONS)
#define LEDS_PER_BUTTON 3
#define LED_COUNT       (TOTAL_BUTTONS * LEDS_PER_BUTTON)
#define BRIGHTNESS      200

// ==================== AUDIO ====================
#define VOICE_COUNT     8        // polyphonie
#define DRUM_ROWS       4        // kick / snare / hh / clap
#define DRUM_MAX_STEPS  16

// ==================== MODES ====================
enum AppMode : uint8_t {
  MODE_INSTRUMENT = 0,
  MODE_DRUMBOX = 1,
  MODE_DRUM_INSTRUMENT = 2,
  MODE_MASTER = 3
};
#define APP_MODE_COUNT 4

// ==================== EXTRA BUTTONS ====================
const uint8_t extraMcpPins[EXTRA_BUTTONS] = {8, 11, 13, 15};
enum ExtraButtonRole : uint8_t {
  EXTRA_MODE_INSTRUMENT = 0,
  EXTRA_MODE_DRUMBOX    = 1,
  EXTRA_DRUM_PLAY       = 2,
  EXTRA_DRUM_CLEAR      = 3
};

// ==================== POTENTIOMETERS ====================
#define POT_COUNT 5
extern const uint8_t POT_PINS[5];
extern const char* potNames[5];
constexpr uint16_t BPM_MIN = 1;
constexpr uint16_t BPM_MAX = 240;
constexpr uint16_t BPM_DEFAULT = 100;
constexpr float POT_BPM_ALPHA_IDLE = 0.08f;
constexpr float POT_BPM_ALPHA_ACTIVE = 0.18f;
constexpr float POT_BPM_ALPHA_FAST = 0.36f;
constexpr int POT_BPM_DELTA_ACTIVE = 12;
constexpr int POT_BPM_DELTA_FAST = 64;

// ==================== FORMES D'ONDES ====================
extern const char* shapeNames[];
#define BASE_SHAPE_COUNT 25
#define ENABLE_SAMPLE_INSTRUMENT_ENGINE 0
#define ENABLE_SOUND_SYNTH_BANKS 0
#define ENABLE_BURROUGHS_KICK_SAMPLE 0
#define SOUND_SYNTH_BANK_COUNT 3
#define SOUND_SYNTH_SHAPE_FIRST BASE_SHAPE_COUNT

#if ENABLE_SOUND_SYNTH_BANKS
#define SHAPE_COUNT (BASE_SHAPE_COUNT + SOUND_SYNTH_BANK_COUNT)
#else
#define SHAPE_COUNT BASE_SHAPE_COUNT
#endif

// ==================== ENVELOPPES ====================
struct EnvPreset {
  const char* name;
  uint16_t attackMs, decayMs;
  uint8_t  sustainLevel;
  uint16_t releaseMs;
  uint8_t curve;
};

extern const EnvPreset envPresets[];
#define ENV_PRESET_COUNT 12

enum EnvMode : uint8_t {
  ENV_MODE_NORMAL = 0,
  ENV_MODE_FAST   = 1,
  ENV_MODE_PLUCK  = 2,
  ENV_MODE_PAD    = 3,
  ENV_MODE_PIANO  = 4,
  ENV_MODE_PIANO2 = 5,
  ENV_MODE_PIANO3 = 6,
  ENV_MODE_FADE   = 7,
  ENV_MODE_PITCH  = 8,
  ENV_MODE_SLIDE  = 9,
  ENV_MODE_CRYSTAL = 10,
  ENV_MODE_CRYSTAL2 = 11
};

// ==================== EFFETS ====================
extern const char* effectNames[];
#define EFFECT_COUNT 28
#define DELAY_BUFFER_SIZE 4096  // reduced from 16384 to save RAM (~24KB saved)

// LFO Modulator modes for effects
enum LFOModMode : uint8_t {
  LFO_MODE_OFF = 0,
  LFO_MODE_SINE = 1,
  LFO_MODE_SQUARE = 2
};
extern uint8_t effectLFOMode[EFFECT_COUNT];  // which mode modulates each effect

// ==================== ARPÉGIATEUR ====================
struct ArpPreset { 
  const char* name; 
  const int8_t steps[4]; 
  uint8_t stepCount; 
};

extern const ArpPreset arpPresets[];
#define ARP_PRESET_COUNT 4

// ==================== DRUM BANKS ====================
struct DrumBank {
  const char* name;
  float kickStartHz;
  float kickEndHz;
  float kickTauMs;
  float snareStartHz;
  float snareEndHz;
  float snareTauMs;
  uint8_t kickDecay;
  uint8_t snareDecay;
  uint8_t hhDecay;
  uint8_t clapDecay;
  uint8_t snareNoise;
  uint8_t hhNoise;
  uint8_t clapNoise;
  float hhStartHz;
  float hhEndHz;
  float hhTauMs;
  float clapStartHz;
  float clapEndHz;
  float clapTauMs;
  uint8_t mixLevel;
  uint16_t hueBase;
};

extern const DrumBank drumBanks[];
#define DRUM_BANK_BASE_COUNT 6
#define SOUND_DRUM_BANK_COUNT 8
#define SOUND_DRUM_BANK_FIRST DRUM_BANK_BASE_COUNT
#define DRUM_BANK_COUNT (DRUM_BANK_BASE_COUNT + SOUND_DRUM_BANK_COUNT)

// ==================== SCALES ====================
extern const char* scaleNames[];
extern const int8_t scaleMap[4][16];

// ==================== DRUM SHOW PRESETS ====================
extern const char* drumShowDivNames[4];
extern const bool drumShowLoops[4][DRUM_ROWS][COLS];

// ==================== TIMINGS ====================
#define UI_TICKS_LEDS  8       // 256/8 = 32 Hz LED request (~31ms)
#define UI_TICKS_DISP  26    // ~10 Hz display request at CONTROL_RATE=256

constexpr TickType_t BUTTON_SCAN_TASK_DELAY_TICKS = 2;   // Scan boutons ~2ms
constexpr TickType_t UI_TASK_DELAY_TICKS = 2;  // UI ~2ms entre chaque cycle
constexpr TickType_t BUTTON_SCAN_STANDBY_DELAY_TICKS = 500 / portTICK_PERIOD_MS;
constexpr unsigned long STANDBY_TIMEOUT_MS = 120000UL;

// ==================== DISPLAY ====================
const int DISPLAY_X_SHIFT = -0;  // Décalage écran pour corriger le rognage horizontal

// ==================== HOLD RESET ====================
const unsigned long HOLD_RESET_MS = 500;  // Long-press reset (500ms) for instrument/arp/effect selectors
