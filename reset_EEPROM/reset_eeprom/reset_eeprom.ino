#include <EEPROM.h>

#define EEPROM_SIZE 512

void resetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);

  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }

  EEPROM.commit();
  EEPROM.end();

  Serial.println("EEPROM berhasil dibersihkan!");
}

void setup() {
  Serial.begin(115200);

  resetEEPROM();
}

void loop() {
}