#include <EEPROM.h>

/*
  Keep Talking and Everybody Explodes
  Automatic EEPROM Flasher / Provisioner v6.5

  Arduino Uno R3 / ATmega328P

  EDIT ONLY THIS STRING:
*/

const char EEPROM_FLAG[] = "OmniCTF{f0und_th3_2nd_fl4g!}";

/*
  Usage:
    1. Edit EEPROM_FLAG above.
    2. Upload this sketch normally.
    3. Open Serial Monitor at 115200 if you want verification.
    4. Wait for "PROVISIONING COMPLETE".
    5. Upload bomb_firmware_v6_5_post_flash_reset.ino normally.

  EEPROM layout:
    0x000-0x001 : boot counter
    0x002-0x003 : explosion counter
    0x004-0x005 : defuse counter
    0x006       : persistent explosion lock
    0x180+      : real EEPROM CTF flag
*/

const int BOOT_COUNT_ADDR      = 0x000;
const int EXPLOSION_COUNT_ADDR = 0x002;
const int DEFUSE_COUNT_ADDR    = 0x004;
const int LOCK_ADDR            = 0x006;

// Tells the bomb firmware to perform one extra watchdog reset on first boot.
const int POST_FLASH_RESET_ADDR = 0x007;
const uint8_t POST_FLASH_RESET_MAGIC = 0xC7;

const int FLAG_ADDR = 0x180;
const int FLAG_REGION_SIZE = 128;

void clearGameState() {
  uint16_t zero16 = 0;

  EEPROM.put(BOOT_COUNT_ADDR, zero16);
  EEPROM.put(EXPLOSION_COUNT_ADDR, zero16);
  EEPROM.put(DEFUSE_COUNT_ADDR, zero16);

  // Start unlocked.
  EEPROM.update(LOCK_ADDR, 0x00);

  // Arm a one-shot reset for the bomb firmware's first boot.
  EEPROM.update(
    POST_FLASH_RESET_ADDR,
    POST_FLASH_RESET_MAGIC
  );
}

bool writeFlag() {
  size_t len = strlen(EEPROM_FLAG);

  if (len == 0 || len >= FLAG_REGION_SIZE) {
    return false;
  }

  for (int i = 0; i < FLAG_REGION_SIZE; i++) {
    EEPROM.update(FLAG_ADDR + i, 0x00);
  }

  for (size_t i = 0; i < len; i++) {
    EEPROM.update(
      FLAG_ADDR + i,
      (uint8_t)EEPROM_FLAG[i]
    );
  }

  EEPROM.update(FLAG_ADDR + len, 0x00);

  for (size_t i = 0; i < len; i++) {
    if (
      EEPROM.read(FLAG_ADDR + i) !=
      (uint8_t)EEPROM_FLAG[i]
    ) {
      return false;
    }
  }

  return EEPROM.read(FLAG_ADDR + len) == 0x00;
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println(
    F("=== KEEP TALKING AUTO FLASHER v6.5 ===")
  );

  if (strlen(EEPROM_FLAG) >= FLAG_REGION_SIZE) {
    Serial.println(F("ERROR: EEPROM_FLAG is too long."));
    return;
  }

  Serial.println(F("Clearing game state..."));
  clearGameState();

  Serial.println(F("Writing EEPROM flag..."));
  bool flagOk = writeFlag();

  Serial.println();

  Serial.print(F("Flag verify: "));
  Serial.println(flagOk ? F("OK") : F("FAILED"));

  Serial.println();

  if (flagOk) {
    Serial.println(F("PROVISIONING COMPLETE"));
    Serial.println(F("Now upload the bomb firmware normally."));
  } else {
    Serial.println(F("PROVISIONING FAILED"));
  }
}

void loop() {
}
