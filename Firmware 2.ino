#include <Wire.h>
#include <EEPROM.h>
#include <Keypad.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

/*
  Keep Talking and Everybody Explodes - Bomb Firmware v7.1 CLEAN FLASH MARKER
  Target: Arduino Uno R3 / ATmega328P

  REQUIRED LIBRARIES:
    - Keypad
    - hd44780 by Bill Perry

  EXACT PINOUT
  ============================================================

  LCD1602 I2C:
    VCC -> 5V
    GND -> GND
    SDA -> A4
    SCL -> A5

  4x4 keypad:
    keypad physical pin 8 -> D2
    keypad physical pin 7 -> D3
    keypad physical pin 6 -> D4
    keypad physical pin 5 -> D5
    keypad physical pin 4 -> D6
    keypad physical pin 3 -> D7
    keypad physical pin 2 -> D8
    keypad physical pin 1 -> D9

  Random-message button:
    A0 -> button -> GND

  +1-second button:
    A1 -> button -> GND

  Passive 3.0/3.3V buzzer:
    3.3V -> buzzer +
    buzzer - -> 330 ohm -> D10

  Cuttable wire module:
    RED wire:
      A2 -> RED sacrificial wire -> GND

    BLUE wire:
      A3 -> BLUE sacrificial wire -> GND

    YELLOW wire:
      D11 -> YELLOW sacrificial wire -> GND

    Correct wire in this build:
      RED / A2

  D12:
    organizer reset jumper
    connect D12 -> GND after BOOM to rearm

  D13:
    unused

  EEPROM LAYOUT
  ============================================================
    0x000-0x001 : boot counter
    0x002-0x003 : explosion counter
    0x004-0x005 : defuse counter
    0x006       : persistent explosion lock
    0x180+      : REAL EEPROM CTF flag

  The firmware NEVER reads the real flag at 0x180.

  WIRE MODULE BEHAVIOR
  ============================================================
    RED    / A2  = correct wire -> enables 7351#
    BLUE   / A3  = wrong wire -> BOOM
    YELLOW / D11 = wrong wire -> BOOM

    All three wires must initially connect their assigned input pin to GND.
    They carry only Arduino input-sense current; never put external power
    on the cuttable wires.
*/

// ============================================================
// LCD / KEYPAD
// ============================================================

hd44780_I2Cexp lcd;

constexpr uint8_t LCD_COLS = 16;
constexpr uint8_t LCD_ROWS = 2;

constexpr byte ROWS = 4;
constexpr byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {2, 3, 4, 5};
byte colPins[COLS] = {6, 7, 8, 9};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// ============================================================
// PINS / GAME CONSTANTS
// ============================================================

constexpr uint8_t RANDOM_BUTTON_PIN = A0;
constexpr uint8_t TIME_BUTTON_PIN   = A1;
constexpr uint8_t BUZZER_PIN        = 10;

// Organizer-only hardware reset jumper.
// After BOOM, temporarily connect D12 directly to GND.
constexpr uint8_t RESET_JUMPER_PIN  = 12;

// Three low-voltage continuity loops.
// Intact wire  -> input is LOW because the wire connects the pin to GND.
// Cut wire     -> INPUT_PULLUP makes the input HIGH.
constexpr uint8_t WIRE_RED_PIN    = A2;
constexpr uint8_t WIRE_BLUE_PIN   = A3;
constexpr uint8_t WIRE_YELLOW_PIN = 11;

constexpr uint8_t WIRE_COUNT = 3;

const uint8_t WIRE_PINS[WIRE_COUNT] = {
  WIRE_RED_PIN,
  WIRE_BLUE_PIN,
  WIRE_YELLOW_PIN
};

// 0 = RED, 1 = BLUE, 2 = YELLOW
// Change this one value if you want a different correct wire.
constexpr uint8_t CORRECT_WIRE_INDEX = 0;

constexpr uint32_t START_TIME_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t WRONG_CODE_PENALTY_MS = 15UL * 1000UL;
constexpr uint32_t TICK_INTERVAL_MS = 15UL * 1000UL;

constexpr uint8_t TIME_BUTTON_BOOM_COUNT = 100;

// ============================================================
// EDIT THIS ONE VALUE FOR EACH PHYSICAL CHALLENGE
// ============================================================
//
// Example:
//   #define BOMB_CODE "7351"
//
// Changing this updates BOTH:
//   - the actual keypad defuse code
//   - the plaintext Flash marker: "BOMB Code: 7351"
//
#define BOMB_CODE "7351"

// Physical bomb code used by the keypad logic.
const char DEFUSE_CODE[] = BOMB_CODE;

// Easter eggs: deliberately plaintext in Flash.
const char SLOT_CODE[]  = "0777";
const char LOST_CODE[]  = "1057";
const char LEET_CODE[]  = "1337";
const char SIXTY7_CODE[] = "6767";
const char BRUTE_CODE[]  = "0001";

// ============================================================
// FLASH-DUMP HINTS
// ============================================================
//
// These strings are deliberately present as plaintext in program Flash.
// They are never shown on the LCD. `strings flash.bin` should reveal them.

const char flashHint0[] PROGMEM =
  "Are you faster than the Flash?";

const char flashHint1[] PROGMEM =
  "The program image is not the whole memory.";

const char flashHint2[] PROGMEM =
  "Some things survive a reflash.";

const char flashHint3[] PROGMEM =
  "Persistent state has to live somewhere.";

const char flashHint4[] PROGMEM =
  "One kilobyte can hide a lot.";

const char flashHint5[] PROGMEM =
  "If the bootloader lies, ask the chip itself.";


/*
  Intentionally recoverable from a raw Flash dump.
  This is the physical defuse code, not the real EEPROM CTF flag.
  Edit BOMB_CODE above and this marker changes automatically.
*/
const char bombCodeFlashMarker[] PROGMEM =
  "BOMB Code: " BOMB_CODE;

const char buildInfo[] PROGMEM =
  "WIRE MODULE: DISABLED";
const char* const flashHints[] PROGMEM = {
  flashHint0,
  flashHint1,
  flashHint2,
  flashHint3,
  flashHint4,
  flashHint5
};

constexpr uint8_t FLASH_HINT_COUNT =
  sizeof(flashHints) / sizeof(flashHints[0]);

volatile uint8_t flashHintKeepSink = 0;

// ============================================================
// OBFUSCATED ON-SCREEN DEFUSE FLAG
// ============================================================
//
// Decodes to:
//   OmniCTF{b0mb_d3fus3d!_gg}
//
// No plaintext copy of that flag is stored in program Flash.
// This is obfuscation, not cryptographic secrecy.

const uint8_t DEFUSE_REWARD_ENC[] PROGMEM = {
  0xE8, 0xA1, 0x9F, 0x7F, 0x78,
  0x34, 0xC3, 0xD1, 0xAD, 0xC4,
  0x74, 0x5C, 0x3C, 0xEC, 0x9E,
  0xB4, 0x82, 0x6F, 0x72, 0x02,
  0xAA, 0xEF, 0xB2, 0x9D, 0x62
};

constexpr uint8_t DEFUSE_REWARD_LEN =
  sizeof(DEFUSE_REWARD_ENC);

uint8_t rewardKey(uint8_t i) {
  return (uint8_t)(0xA7u + (uint8_t)(i * 37u));
}

void decodeDefuseReward(char *out) {
  for (uint8_t i = 0; i < DEFUSE_REWARD_LEN; i++) {
    uint8_t b = pgm_read_byte(&DEFUSE_REWARD_ENC[i]);
    out[i] = (char)(b ^ rewardKey(i));
  }

  out[DEFUSE_REWARD_LEN] = '\0';
}

void wipeBuffer(volatile char *p, size_t n) {
  while (n--) {
    *p++ = 0;
  }
}

// ============================================================
// EEPROM STATE / PRIVATE RESET DIGEST
// ============================================================

constexpr int BOOT_COUNT_ADDR      = 0x000;
constexpr int EXPLOSION_COUNT_ADDR = 0x002;
constexpr int DEFUSE_COUNT_ADDR    = 0x004;
constexpr int LOCK_ADDR            = 0x006;

// One-shot marker written by the provisioner.
// When the bomb firmware sees this value on its first boot, it clears the
// marker and performs one watchdog reset. The second boot then continues
// normally, so there is no reset loop.
constexpr int POST_FLASH_RESET_ADDR = 0x007;
constexpr uint8_t POST_FLASH_RESET_MAGIC = 0xC7;

// Value written to EEPROM when the bomb enters the persistent BOOM state.
// D12 -> GND clears LOCK_ADDR back to 0x00.
constexpr uint8_t LOCK_MAGIC = 0xA5;

// Reset is hardware-only in this build.

uint16_t bootCount = 0;
uint16_t explosionCount = 0;
uint16_t defuseCount = 0;

// ============================================================
// RUNTIME STATE
// ============================================================

uint32_t remainingMs = START_TIME_MS;
uint32_t lastTimerUpdate = 0;
uint32_t lastTickMs = 0;

bool exploded = false;
bool defused = false;
bool lockedOut = false;

uint8_t timeButtonPresses = 0;

// ============================================================
// BUTTON STATE
// ============================================================

// Keep helper signatures limited to built-in types. This avoids the
// Arduino IDE 1.8.x automatic-prototype bug with user-defined structs.
constexpr uint8_t BUTTON_RANDOM = 0;
constexpr uint8_t BUTTON_TIME   = 1;
constexpr uint8_t BUTTON_COUNT  = 2;

const uint8_t BUTTON_PINS[BUTTON_COUNT] = {
  RANDOM_BUTTON_PIN,
  TIME_BUTTON_PIN
};

bool buttonStableState[BUTTON_COUNT];
bool buttonLastReading[BUTTON_COUNT];
uint32_t buttonLastChangeMs[BUTTON_COUNT];

constexpr uint16_t DEBOUNCE_MS = 35;

bool wireStableState[WIRE_COUNT];
bool wireLastReading[WIRE_COUNT];
uint32_t wireLastChangeMs[WIRE_COUNT];

constexpr uint16_t WIRE_DEBOUNCE_MS = 750;
constexpr uint16_t WIRE_STARTUP_STABILIZE_MS = 800;
constexpr uint16_t WIRE_ARM_STABLE_MS = 1200;

bool correctWireCut = false;
bool wireModuleArmed = false;
uint32_t allWiresIntactSince = 0;

// ============================================================
// KEYPAD INPUT
// ============================================================

char enteredCode[5] = {'\0', '\0', '\0', '\0', '\0'};
uint8_t enteredLength = 0;


// ============================================================
// SCREEN STATE
// ============================================================

enum ScreenMode {
  SCREEN_NORMAL,
  SCREEN_TEMP,
  SCREEN_DEFUSED,
  SCREEN_EXPLODED
};

ScreenMode screenMode = SCREEN_NORMAL;

uint32_t tempScreenUntil = 0;
uint32_t lastDisplayedSecond = 0xFFFFFFFFUL;
bool displayDirty = true;

// ============================================================
// RANDOM / TROLL MESSAGES
// ============================================================

const char msg0[]  PROGMEM = "DONT PANIC";
const char msg1[]  PROGMEM = "KEEP TALKING";
const char msg2[]  PROGMEM = "STATE RESTORED";
const char msg3[]  PROGMEM = "PERSISTENCE OK";
const char msg4[]  PROGMEM = "CLOCK DRIFT";
const char msg5[]  PROGMEM = "FACTORY MODE";
const char msg6[]  PROGMEM = "MEMORY STABLE";
const char msg7[]  PROGMEM = "ARE YOU SURE?";
const char msg8[]  PROGMEM = "TRY HARDER";
const char msg9[]  PROGMEM = "PRESS FASTER";
const char msg10[] PROGMEM = "67";
const char msg11[] PROGMEM = "GOOD LUCK :)";
const char msg12[] PROGMEM = "NO USER DATA";
const char msg13[] PROGMEM = "TICK TOCK...";
const char msg14[] PROGMEM = "NICE TRY";
const char msg15[] PROGMEM = "SKILL ISSUE";
const char msg16[] PROGMEM = "THIS IS FINE";
const char msg17[] PROGMEM = "NO REFUNDS";
const char msg18[] PROGMEM = "BOMB.EXE OK";
const char msg19[] PROGMEM = "PANIC LATER";
const char msg20[] PROGMEM = "WRONG WAY?";
const char msg21[] PROGMEM = "PRESS EVERYTHING";
const char msg22[] PROGMEM = "TRUST THE TIMER";
const char msg23[] PROGMEM = "STILL ALIVE?";
const char msg24[] PROGMEM = "NOT SUSPICIOUS";
const char msg25[] PROGMEM = "TOTALLY SAFE";
const char msg26[] PROGMEM = "WHAT COULD GO";
const char msg27[] PROGMEM = "POSSIBLY WRONG?";
const char msg28[] PROGMEM = "CHECK THE CLOCK";
const char msg29[] PROGMEM = "NO WIRES HERE";
const char msg30[] PROGMEM = "FEELS SAFE";
const char msg31[] PROGMEM = "ERROR: SUCCESS";

const char* const randomMessages[] PROGMEM = {
  msg0, msg1, msg2, msg3, msg4, msg5, msg6, msg7,
  msg8, msg9, msg10, msg11, msg12, msg13, msg14, msg15,
  msg16, msg17, msg18, msg19, msg20, msg21, msg22, msg23,
  msg24, msg25, msg26, msg27, msg28, msg29, msg30, msg31
};

constexpr uint8_t RANDOM_MESSAGE_COUNT =
  sizeof(randomMessages) / sizeof(randomMessages[0]);

// ============================================================
// LCD HELPERS
// ============================================================

void printPadded(uint8_t row, const char *text) {
  lcd.setCursor(0, row);

  uint8_t i = 0;

  while (i < LCD_COLS && text[i] != '\0') {
    lcd.write(text[i]);
    i++;
  }

  while (i < LCD_COLS) {
    lcd.write(' ');
    i++;
  }
}

void printCentered(uint8_t row, const char *text) {
  char line[17];

  memset(line, ' ', 16);
  line[16] = '\0';

  size_t len = strlen(text);
  if (len > 16) {
    len = 16;
  }

  uint8_t start = (16 - len) / 2;
  memcpy(line + start, text, len);

  printPadded(row, line);
}

void renderNormalScreen() {
  char line1[17];
  char line2[17];

  uint32_t totalSeconds = (remainingMs + 999UL) / 1000UL;
  uint16_t minutes = totalSeconds / 60UL;
  uint8_t seconds = totalSeconds % 60UL;

  snprintf(
    line1,
    sizeof(line1),
    "TIME %02u:%02u",
    minutes,
    seconds
  );

  char visible[5] = {'_', '_', '_', '_', '\0'};

  for (uint8_t i = 0; i < enteredLength && i < 4; i++) {
    visible[i] = enteredCode[i];
  }

  snprintf(
    line2,
    sizeof(line2),
    "CODE:%s",
    visible
  );

  printPadded(0, line1);
  printPadded(1, line2);

  lastDisplayedSecond = totalSeconds;
  displayDirty = false;
}

void showResetScreen() {
  printPadded(0, "GO TO MAPLELEAF");
  printPadded(1, "FOR RESET");
}

// ============================================================
// FORCE FLASH HINTS TO REMAIN LINKED
// ============================================================

void keepFlashHints() {
  uint8_t sum = 0;

  for (uint8_t i = 0; i < FLASH_HINT_COUNT; i++) {
    PGM_P p = (PGM_P)pgm_read_ptr(&flashHints[i]);

    // Touch multiple bytes so the strings/table are real references.
    sum ^= pgm_read_byte(p);
    sum ^= pgm_read_byte(p + 1);
  }

  // Touch every byte of the deliberately recoverable physical code marker.
  for (
    uint8_t i = 0;
    i < sizeof(bombCodeFlashMarker) - 1;
    i++
  ) {
    sum ^= pgm_read_byte(&bombCodeFlashMarker[i]);
  }

  flashHintKeepSink = sum;
}

// ============================================================
// BUZZER
// ============================================================
//
// 3.3V -> buzzer +
// buzzer - -> 330R -> D10
//
// D10 LOW   = current flows
// D10 INPUT = high impedance
// D10 is never driven HIGH.

void buzzerOff() {
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, INPUT);
}

void buzz(uint16_t frequency, uint16_t durationMs) {
  if (frequency == 0 || durationMs == 0) {
    return;
  }

  uint32_t halfPeriodUs = 500000UL / frequency;
  uint32_t cycles =
    ((uint32_t)frequency * durationMs) / 1000UL;

  for (uint32_t i = 0; i < cycles; i++) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(halfPeriodUs);

    digitalWrite(BUZZER_PIN, LOW);
    pinMode(BUZZER_PIN, INPUT);
    delayMicroseconds(halfPeriodUs);
  }

  buzzerOff();
}

void beepKey() {
  buzz(1850, 12);
}

void beepRandom() {
  uint8_t p = random(0, 4);

  if (p == 0) {
    buzz(700, 35);
  } else if (p == 1) {
    buzz(1200, 25);
    delay(25);
    buzz(1700, 25);
  } else if (p == 2) {
    buzz(1900, 18);
    delay(20);
    buzz(900, 40);
  } else {
    buzz(500, 25);
    delay(20);
    buzz(500, 25);
  }
}

void beepWrong() {
  buzz(420, 100);
  delay(55);
  buzz(330, 130);
}

void beepLost() {
  buzz(1100, 50);
  delay(45);
  buzz(850, 70);
  delay(45);
  buzz(620, 100);
}

void beepLeet() {
  buzz(900, 35);
  delay(35);
  buzz(1300, 35);
  delay(35);
  buzz(1800, 90);
}

void beepJackpot() {
  buzz(880, 60);
  delay(35);
  buzz(1175, 60);
  delay(35);
  buzz(1568, 80);
  delay(45);
  buzz(2093, 180);
}

void beepDefused() {
  buzz(784, 65);
  delay(35);
  buzz(988, 65);
  delay(35);
  buzz(1319, 65);
  delay(35);
  buzz(1568, 80);
  delay(35);
  buzz(2093, 220);
}

void beepExplosion() {
  buzz(950, 160);
  delay(70);
  buzz(1450, 160);
  delay(70);
  buzz(650, 250);
  delay(70);
  buzz(2100, 420);
}

void beepResetAccepted() {
  buzz(800, 60);
  delay(35);
  buzz(1200, 60);
  delay(35);
  buzz(1800, 120);
}

// ============================================================
// BUTTON HELPERS
// ============================================================

void initButton(uint8_t buttonIndex) {
  if (buttonIndex >= BUTTON_COUNT) return;

  uint8_t pin = BUTTON_PINS[buttonIndex];
  pinMode(pin, INPUT_PULLUP);

  buttonStableState[buttonIndex] = digitalRead(pin);
  buttonLastReading[buttonIndex] = buttonStableState[buttonIndex];
  buttonLastChangeMs[buttonIndex] = millis();
}

bool pressed(uint8_t buttonIndex) {
  if (buttonIndex >= BUTTON_COUNT) return false;

  uint8_t pin = BUTTON_PINS[buttonIndex];
  bool reading = digitalRead(pin);

  if (reading != buttonLastReading[buttonIndex]) {
    buttonLastReading[buttonIndex] = reading;
    buttonLastChangeMs[buttonIndex] = millis();
  }

  if ((millis() - buttonLastChangeMs[buttonIndex]) >= DEBOUNCE_MS) {
    if (reading != buttonStableState[buttonIndex]) {
      buttonStableState[buttonIndex] = reading;
      if (buttonStableState[buttonIndex] == LOW) return true;
    }
  }

  return false;
}

// ============================================================
// CUTTABLE WIRE MODULE
// ============================================================

void initWireMonitor(uint8_t wireIndex) {
  if (wireIndex >= WIRE_COUNT) return;

  uint8_t pin = WIRE_PINS[wireIndex];
  pinMode(pin, INPUT_PULLUP);

  wireStableState[wireIndex] = digitalRead(pin);
  wireLastReading[wireIndex] = wireStableState[wireIndex];
  wireLastChangeMs[wireIndex] = millis();
}

bool wireJustChangedToCut(uint8_t wireIndex) {
  if (wireIndex >= WIRE_COUNT) return false;

  uint8_t pin = WIRE_PINS[wireIndex];
  bool reading = digitalRead(pin);

  if (reading != wireLastReading[wireIndex]) {
    wireLastReading[wireIndex] = reading;
    wireLastChangeMs[wireIndex] = millis();
  }

  if ((millis() - wireLastChangeMs[wireIndex]) >= WIRE_DEBOUNCE_MS) {
    if (reading != wireStableState[wireIndex]) {
      wireStableState[wireIndex] = reading;
      if (wireStableState[wireIndex] == HIGH) return true;
    }
  }

  return false;
}

bool isWireIntact(
  uint8_t wireIndex
) {
  if (wireIndex >= WIRE_COUNT) {
    return false;
  }

  return
    digitalRead(
      WIRE_PINS[wireIndex]
    ) == LOW;
}

bool allWiresIntact() {
  for (
    uint8_t i = 0;
    i < WIRE_COUNT;
    i++
  ) {
    if (!isWireIntact(i)) {
      return false;
    }
  }

  return true;
}

bool wireStableAtLevel(
  uint8_t wireIndex,
  uint8_t level,
  uint16_t durationMs
) {
  if (wireIndex >= WIRE_COUNT) {
    return false;
  }

  uint8_t pin = WIRE_PINS[wireIndex];
  uint32_t startMs = millis();

  while ((millis() - startMs) < durationMs) {
    if (digitalRead(pin) != level) {
      return false;
    }
    delay(2);
  }

  return true;
}

void showCorrectWireAccepted() {
  showTemporary(
    "WIRE ACCEPTED",
    "CODE ENABLED",
    1100
  );

  buzz(900, 45);
  delay(35);
  buzz(1400, 45);
  delay(35);
  buzz(1900, 100);
}

void handleWireCut(
  uint8_t wireIndex
) {
  if (
    lockedOut ||
    exploded ||
    defused
  ) {
    return;
  }

  if (
    wireIndex ==
    CORRECT_WIRE_INDEX
  ) {
    if (!correctWireCut) {
      correctWireCut = true;
      showCorrectWireAccepted();
    }

    return;
  }

  // Any wrong wire immediately triggers BOOM.
  explode();
}

void checkWires() {
  if (!wireModuleArmed || lockedOut || exploded || defused) {
    return;
  }

  for (uint8_t i = 0; i < WIRE_COUNT; i++) {
    if (wireJustChangedToCut(i)) {
      handleWireCut(i);

      if (lockedOut || exploded) {
        return;
      }
    }
  }
}

void evaluateInitialWireState() {
  // Never explode during startup.
  //
  // With INPUT_PULLUP + pin -> wire -> GND:
  //   LOW  = intact
  //   HIGH = open/cut
  //
  // The module becomes armed only after ALL THREE wires have remained
  // continuously LOW for WIRE_ARM_STABLE_MS. Until then, the LCD asks
  // the player/organizer to check the wires.

  delay(WIRE_STARTUP_STABILIZE_MS);

  correctWireCut = false;
  wireModuleArmed = false;
  allWiresIntactSince = 0;

  for (uint8_t i = 0; i < WIRE_COUNT; i++) {
    bool reading = digitalRead(WIRE_PINS[i]);

    wireStableState[i] = reading;
    wireLastReading[i] = reading;
    wireLastChangeMs[i] = millis();
  }
}

bool allWiresCurrentlyIntact() {
  for (uint8_t i = 0; i < WIRE_COUNT; i++) {
    if (digitalRead(WIRE_PINS[i]) != LOW) {
      return false;
    }
  }

  return true;
}

void updateWireArming() {
  if (wireModuleArmed || lockedOut || exploded || defused) {
    return;
  }

  if (!allWiresCurrentlyIntact()) {
    allWiresIntactSince = 0;

    printPadded(0, "CHECK ALL WIRES");
    printPadded(1, "CONNECT TO GND");
    screenMode = SCREEN_TEMP;
    tempScreenUntil = millis() + 350;

    return;
  }

  if (allWiresIntactSince == 0) {
    allWiresIntactSince = millis();
    return;
  }

  if (
    millis() - allWiresIntactSince >=
    WIRE_ARM_STABLE_MS
  ) {
    wireModuleArmed = true;

    for (uint8_t i = 0; i < WIRE_COUNT; i++) {
      wireStableState[i] = LOW;
      wireLastReading[i] = LOW;
      wireLastChangeMs[i] = millis();
    }

    showTemporary(
      "WIRES ARMED",
      "CUT CORRECT ONE",
      900
    );

    buzz(950, 50);
    delay(35);
    buzz(1450, 70);
  }
}

// ============================================================
// EEPROM COUNTERS / PERSISTENT LOCK
// ============================================================

void loadPersistentState() {
  EEPROM.get(BOOT_COUNT_ADDR, bootCount);
  EEPROM.get(EXPLOSION_COUNT_ADDR, explosionCount);
  EEPROM.get(DEFUSE_COUNT_ADDR, defuseCount);

  if (bootCount == 0xFFFF) bootCount = 0;
  if (explosionCount == 0xFFFF) explosionCount = 0;
  if (defuseCount == 0xFFFF) defuseCount = 0;

  bootCount++;
  EEPROM.put(BOOT_COUNT_ADDR, bootCount);

  lockedOut = (EEPROM.read(LOCK_ADDR) == LOCK_MAGIC);

  if (lockedOut) {
    exploded = true;
  }
}

// ============================================================
// TEMPORARY SCREENS
// ============================================================

void showTemporary(
  const char *line1,
  const char *line2,
  uint16_t durationMs
) {
  printPadded(0, line1);
  printPadded(1, line2);

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + durationMs;
}

// ============================================================
// EXPLOSION / DEFUSE
// ============================================================

void explode() {
  if (lockedOut || exploded || defused) {
    return;
  }

  exploded = true;
  lockedOut = true;
  screenMode = SCREEN_EXPLODED;

  explosionCount++;
  EEPROM.put(EXPLOSION_COUNT_ADDR, explosionCount);

  // Persist lock BEFORE the effects, so power loss cannot bypass it.
  EEPROM.update(LOCK_ADDR, LOCK_MAGIC);

  printCentered(0, "!!! BOOM !!!");
  printCentered(1, "YOU EXPLODED");

  beepExplosion();

  delay(1200);

  showResetScreen();
}

void showDefuseReward() {
  char reward[DEFUSE_REWARD_LEN + 1];
  decodeDefuseReward(reward);

  char line1[17];
  char line2[17];

  memset(line1, 0, sizeof(line1));
  memset(line2, 0, sizeof(line2));

  memcpy(line1, reward, 16);
  line1[16] = '\0';

  strncpy(line2, reward + 16, 16);
  line2[16] = '\0';

  printPadded(0, line1);
  printPadded(1, line2);

  wipeBuffer((volatile char *)reward, sizeof(reward));
  wipeBuffer((volatile char *)line1, sizeof(line1));
  wipeBuffer((volatile char *)line2, sizeof(line2));
}

void defuseBomb() {
  if (lockedOut || exploded || defused) {
    return;
  }

  defused = true;
  screenMode = SCREEN_DEFUSED;

  defuseCount++;
  EEPROM.put(DEFUSE_COUNT_ADDR, defuseCount);

  printCentered(0, "BOMB DEFUSED");
  printCentered(1, "GG!");

  beepDefused();

  delay(1100);

  showDefuseReward();
}

// ============================================================
// RANDOM BUTTON / +1 SECOND BUTTON
// ============================================================

void randomButtonAction() {
  if (random(0, 6) == 0) {
    char line2[17];

    snprintf(
      line2,
      sizeof(line2),
      "BOOT #%u",
      bootCount
    );

    showTemporary(
      "STATE RESTORED",
      line2,
      1200
    );

    beepRandom();
    return;
  }

  char message[17];

  uint8_t index = random(RANDOM_MESSAGE_COUNT);

  strcpy_P(
    message,
    (PGM_P)pgm_read_ptr(&randomMessages[index])
  );

  showTemporary(
    message,
    "TICK TOCK...",
    1050
  );

  beepRandom();
}

void timeButtonAction() {
  timeButtonPresses++;

  // Hidden 100-press trap.
  if (timeButtonPresses >= TIME_BUTTON_BOOM_COUNT) {
    explode();
    return;
  }

  remainingMs += 1000UL;

  showTemporary(
    "+1 SECOND",
    "TIME EXTENDED",
    420
  );

  buzz(1450, 22);
}

// ============================================================
// EASTER EGG CODES
// ============================================================

void runSlots() {
  printCentered(0, "SLOT MACHINE");

  for (uint8_t i = 0; i < 15; i++) {
    char line[17];

    uint8_t a = random(0, 10);
    uint8_t b = random(0, 10);
    uint8_t c = random(0, 10);

    snprintf(
      line,
      sizeof(line),
      "   [ %u %u %u ]",
      a,
      b,
      c
    );

    printPadded(1, line);

    buzz(900 + (i * 35), 16);
    delay(65 + (i * 5));
  }

  printPadded(1, "   [ 7 ? ? ]");
  buzz(1200, 55);
  delay(260);

  printPadded(1, "   [ 7 7 ? ]");
  buzz(1500, 55);
  delay(260);

  printPadded(1, "   [ 7 7 7 ]");
  beepJackpot();

  delay(500);

  printCentered(0, "JACKPOT!");
  printCentered(1, "777");

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + 1500;
}

void showLostMessage() {
  printPadded(0, "Are you lost?");
  printPadded(1, "I'm 1057 too!");

  beepLost();

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + 2200;
}

void showLeetMessage() {
  printCentered(0, "YOU ARE");
  printCentered(1, "31337!");

  beepLeet();

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + 1800;
}


void runSixtySevenAnimation() {
  // "67" meme gesture:
  // both arms out, alternating up/down like a seesaw.
  //
  // 16x2 LCD, so the pose is represented with a stick figure
  // whose left/right forearms alternate height.

  const char *topFrames[] = {
    " __\\O       6  ",
    "    O/__     7  ",
    " __\\O       6  ",
    "    O/__     7  ",
    " __\\O/__   67  "
  };

  const char *bottomFrames[] = {
    "    |\\__     7 ",
    " __/|        6 ",
    "    |\\__     7 ",
    " __/|        6 ",
    "    |           "
  };

  constexpr uint8_t FRAME_COUNT =
    sizeof(topFrames) / sizeof(topFrames[0]);

  // Repeat the left-up/right-down <-> left-down/right-up motion.
  for (uint8_t loop = 0; loop < 3; loop++) {
    for (uint8_t i = 0; i < FRAME_COUNT - 1; i++) {
      printPadded(0, topFrames[i]);
      printPadded(1, bottomFrames[i]);

      buzz(
        (i & 1) ? 1250 : 950,
        28
      );

      delay(190);
    }
  }

  // Finish with both arms out and the punchline.
  printPadded(0, topFrames[FRAME_COUNT - 1]);
  printPadded(1, bottomFrames[FRAME_COUNT - 1]);

  buzz(1450, 65);
  delay(45);
  buzz(1800, 95);

  delay(450);

  printCentered(0, "SIX");
  printCentered(1, "SEVEN");

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + 1000;
}

void showBruteForceMessage() {
  printPadded(0, "Brute forcing?");
  printPadded(1, "Only 9998 to go.");

  buzz(420, 65);
  delay(50);
  buzz(390, 65);

  screenMode = SCREEN_TEMP;
  tempScreenUntil = millis() + 2400;
}

// ============================================================
// NORMAL KEYPAD
// ============================================================

void clearCode() {
  enteredLength = 0;
  memset(enteredCode, 0, sizeof(enteredCode));
  displayDirty = true;
}

void wrongCode() {
  if (remainingMs > WRONG_CODE_PENALTY_MS) {
    remainingMs -= WRONG_CODE_PENALTY_MS;
  } else {
    remainingMs = 0;
  }

  showTemporary(
    "WRONG CODE",
    "-15 SECONDS",
    900
  );

  beepWrong();

  if (remainingMs == 0) {
    explode();
  }
}

void submitCode() {
  if (enteredLength != 4) {
    clearCode();
    wrongCode();
    return;
  }

  if (strcmp(enteredCode, DEFUSE_CODE) == 0) {
    clearCode();

    if (!correctWireCut) {
      showTemporary(
        "WIRE REQUIRED",
        "CUT ONE FIRST",
        1000
      );

      buzz(300, 90);
      delay(45);
      buzz(300, 90);

      return;
    }

    defuseBomb();
    return;
  }

  if (strcmp(enteredCode, SLOT_CODE) == 0) {
    clearCode();
    runSlots();
    return;
  }

  if (strcmp(enteredCode, LOST_CODE) == 0) {
    clearCode();
    showLostMessage();
    return;
  }

  if (strcmp(enteredCode, LEET_CODE) == 0) {
    clearCode();
    showLeetMessage();
    return;
  }

  if (strcmp(enteredCode, SIXTY7_CODE) == 0) {
    clearCode();
    runSixtySevenAnimation();
    return;
  }

  if (strcmp(enteredCode, BRUTE_CODE) == 0) {
    clearCode();
    showBruteForceMessage();
    return;
  }

  clearCode();
  wrongCode();
}

void handleNormalKeypad() {
  char key = keypad.getKey();

  if (!key) {
    return;
  }

  if (key >= '0' && key <= '9') {
    if (enteredLength < 4) {
      enteredCode[enteredLength++] = key;
      enteredCode[enteredLength] = '\0';

      displayDirty = true;
      beepKey();
    }

    return;
  }

  if (key == '*') {
    clearCode();
    buzz(850, 25);
    return;
  }

  if (key == '#') {
    submitCode();
    return;
  }

  if (
    key == 'A' ||
    key == 'B' ||
    key == 'C' ||
    key == 'D'
  ) {
    uint8_t troll = random(0, 4);

    if (troll == 0) {
      showTemporary(
        "MODULE LOCKED",
        "ACCESS DENIED",
        700
      );
    } else if (troll == 1) {
      showTemporary(
        "WRONG MODULE",
        "NICE BUTTON",
        700
      );
    } else if (troll == 2) {
      showTemporary(
        "NOT THAT ONE",
        "TRY AGAIN :)",
        700
      );
    } else {
      showTemporary(
        "VERY COOL",
        "DID NOTHING",
        700
      );
    }

    beepRandom();
  }
}

// ============================================================
// LOCKED-OUT HARDWARE RESET
// ============================================================
//
// After BOOM, all normal controls are ignored.
// The persistent BOOM lock survives reset/power-cycle.
//
// Organizer rearm:
//   connect D12 directly to GND and hold for ~1 second.
//
// D12 uses INPUT_PULLUP:
//   open       = HIGH
//   D12 -> GND = LOW

uint32_t resetJumperLowSince = 0;

void performHardwareReset() {
  printPadded(0, "RESET ACCEPTED");
  printPadded(1, "REARMING...");

  beepResetAccepted();

  EEPROM.update(LOCK_ADDR, 0x00);

  delay(350);

  wdt_enable(WDTO_15MS);

  while (true) {
    // wait for watchdog reset
  }
}

void handleHardwareResetJumper() {
  if (!lockedOut) {
    resetJumperLowSince = 0;
    return;
  }

  if (digitalRead(RESET_JUMPER_PIN) != LOW) {
    resetJumperLowSince = 0;
    return;
  }

  if (resetJumperLowSince == 0) {
    resetJumperLowSince = millis();
    return;
  }

  if ((millis() - resetJumperLowSince) >= 1000UL) {
    performHardwareReset();
  }
}

// ============================================================
// TIMER / PERIODIC TICK
// ============================================================

void updateTimer() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastTimerUpdate;

  lastTimerUpdate = now;

  if (lockedOut || defused || exploded) {
    return;
  }

  if (elapsed >= remainingMs) {
    remainingMs = 0;
    explode();
    return;
  }

  remainingMs -= elapsed;
}

void updatePeriodicTick() {
  if (lockedOut || defused || exploded) {
    return;
  }

  uint32_t now = millis();

  if ((now - lastTickMs) >= TICK_INTERVAL_MS) {
    uint32_t intervals =
      (now - lastTickMs) / TICK_INTERVAL_MS;

    lastTickMs +=
      intervals * TICK_INTERVAL_MS;

    buzz(1800, 28);
  }
}

// ============================================================
// DISPLAY SCHEDULER
// ============================================================

void updateDisplay() {
  if (lockedOut || defused || exploded) {
    return;
  }

  if (screenMode == SCREEN_TEMP) {
    if ((int32_t)(millis() - tempScreenUntil) < 0) {
      return;
    }

    screenMode = SCREEN_NORMAL;
    displayDirty = true;
  }

  uint32_t currentSecond =
    (remainingMs + 999UL) / 1000UL;

  if (
    displayDirty ||
    currentSecond != lastDisplayedSecond
  ) {
    renderNormalScreen();
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup() {
  // Clear watchdog state immediately after a watchdog-triggered reboot.
  MCUSR = 0;
  wdt_disable();

  // One extra automatic reset immediately after provisioning + flashing.
  // The flasher sets this EEPROM marker. We clear it BEFORE resetting,
  // so the next boot continues normally instead of looping forever.
  if (
    EEPROM.read(POST_FLASH_RESET_ADDR) ==
    POST_FLASH_RESET_MAGIC
  ) {
    EEPROM.update(POST_FLASH_RESET_ADDR, 0x00);

    delay(120);

    wdt_enable(WDTO_15MS);

    while (true) {
      // wait for watchdog reset
    }
  }

  initButton(BUTTON_RANDOM);
  initButton(BUTTON_TIME);

  for (uint8_t i = 0; i < WIRE_COUNT; i++) {
    initWireMonitor(i);
  }

  buzzerOff();

  pinMode(RESET_JUMPER_PIN, INPUT_PULLUP);

  lcd.begin(
    LCD_COLS,
    LCD_ROWS
  );

  lcd.backlight();

  // Makes plaintext hint strings survive link-time dead-code removal.
  keepFlashHints();

  // Load persistent BOOM state.
  // IMPORTANT: do NOT clear LOCK_ADDR here.
  // Once exploded, the bomb stays locked across RESET / power-cycle
  // until the organizer uses the D12 -> GND reset jumper.
  loadPersistentState();

  randomSeed(
    micros() ^
    ((uint32_t)bootCount << 8)
  );

  // Persistent BOOM lock survives startup and power-cycle.
  if (lockedOut) {
    showResetScreen();
    return;
  }

  // Validate the physical wire module before arming the normal UI.
  evaluateInitialWireState();

  if (lockedOut || exploded) {
    return;
  }

  printCentered(0, "KEEP TALKING");
  printCentered(1, "EVERYBODY BOOM");

  buzz(900, 60);
  delay(80);
  buzz(1200, 60);

  delay(1200);

  // Player-facing hint before the timer screen.
  printPadded(0, "CONNECT 3 WIRES");
  printPadded(1, "THEN CUT RIGHT");

  buzz(750, 45);
  delay(40);
  buzz(1050, 45);

  delay(1800);

  remainingMs = START_TIME_MS;
  lastTimerUpdate = millis();
  lastTickMs = lastTimerUpdate;

  screenMode = SCREEN_NORMAL;
  displayDirty = true;
}

void loop() {
  // After BOOM, only the physical D12->GND organizer jumper works.
  if (lockedOut) {
    handleHardwareResetJumper();
    return;
  }

  if (defused) {
    return;
  }

  // First arm the wire module only after all three loops are confirmed intact.
  updateWireArming();

  if (!wireModuleArmed) {
    updateTimer();
    updatePeriodicTick();
    updateDisplay();
    return;
  }

  // Once armed, a real cut transition has priority.
  checkWires();

  if (lockedOut || exploded) {
    return;
  }

  updateTimer();
  updatePeriodicTick();

  if (lockedOut || exploded) {
    return;
  }

  handleNormalKeypad();

  if (pressed(BUTTON_RANDOM)) {
    randomButtonAction();
  }

  if (pressed(BUTTON_TIME)) {
    timeButtonAction();
  }

  updateDisplay();
}
