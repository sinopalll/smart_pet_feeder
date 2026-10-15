// #include <HX711.h>

// #define PIN_HX711_DT  14   // D5
// #define PIN_HX711_SCK 12   // D6

// HX711 scale;

// void setup() {
//   Serial.begin(115200);

//   scale.begin(PIN_HX711_DT, PIN_HX711_SCK);

//   Serial.println();
//   Serial.println("=== KALIBRASI LOAD CELL ===");
//   Serial.println("Pastikan wadah/load cell KOSONG.");
//   Serial.println();

//   delay(3000);

//   scale.set_scale();

//   scale.tare(20);

//   Serial.println("TARE selesai.");
//   Serial.println();
//   Serial.println("Letakkan beban yang beratnya diketahui.");
//   Serial.println("Contoh: 200 gram.");
//   Serial.println();
// }

// void loop() {

//   if (scale.is_ready()) {

//     long raw = scale.get_units(10);

//     Serial.print("RAW / UNITS = ");
//     Serial.println(raw);

//   } else {

//     Serial.println("HX711 tidak siap.");

//   }

//   delay(1000);
// }