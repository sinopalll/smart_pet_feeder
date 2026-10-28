
#include <HX711.h>

#define PIN_HX711_DT  14  // D5 / GPIO14
#define PIN_HX711_SCK 12  // D6 / GPIO12

HX711 scale;

void setup() {
  Serial.begin(115200);
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
}

void loop() {
  if (scale.is_ready()) {
    long total = 0;

    for (int i = 0; i < 5; i++) {
      total += scale.read();
      delay(200);
    }

    long nilaiTare = total / 5;

    Serial.println(nilaiTare);
  } else {
    Serial.println("HX711 tidak siap!");
  }

  delay(1000);
}
