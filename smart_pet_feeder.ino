#include "secrets.h"
#include "config.h"

#define BLYNK_TEMPLATE_ID   "TMPL6BR0CA82x"
#define BLYNK_TEMPLATE_NAME "smart pet feeder"
#define BLYNK_AUTH_TOKEN    SECRET_BLYNK_TOKEN
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <HX711.h>
#include <Servo.h>
#include <time.h>
#include <EEPROM.h>
#include <cmath>

char ssid[] = SECRET_WIFI_SSID;
char pass[] = SECRET_WIFI_PASS;

#define MODE_MANUAL   1
#define MODE_SMART    2
#define MODE_SCHEDULE 3
#define MAKS_JADWAL   5

// ------------------------------------------------------------
// KONFIGURASI TERSIMPAN (EEPROM): satu struct, satu put/getconst float TARGET_PORSI_GRAM   = 50.0;  // Katup menutup saat pertambahan berat piring mencapai target ini (gram)
// ------------------------------------------------------------
const int      EEPROM_ADDR_OFFSET = 0;   // long   : offset load cell (terpisah dari struct)
const int      EEPROM_ADDR_CFG    = 8;   // Konfig : semua pengaturan lain
const uint32_t EEPROM_MAGIC       = 0xFEED0A03;

struct Konfig {
  uint32_t magic;
  int32_t  mode;
  int32_t  zonaMenit;             // zona waktu, menit dari UTC
  int32_t  jumlah;                // jumlah jadwal aktif
  int16_t  jadwal[MAKS_JADWAL];   // menit sejak 00:00 (mis. 08:30 = 510)
  uint8_t  mask;                  // bit i = jadwal i sudah dieksekusi hari ini
  int32_t  smartN;                // jumlah pemberian Smart hari ini
  uint32_t smartT;                // epoch pemberian Smart terakhir
  int32_t  hari;                  // nomor hari lokal terakhir yang dicatat
} cfg;

HX711 scale;
Servo myServo;
BlynkTimer timer;

// Katup
bool          sedangMemberi = false;   // dari buka sampai verifikasi selesai
bool          katupTerbuka  = false;   // hanya selama fase buka (berat dipantau)
unsigned long waktuMulaiBuka = 0;
float         beratAwalPorsi = NAN;

// Cooldown berbasis millis (nilai awal membuat cooldown tidak aktif saat boot)
unsigned long waktuSmartTerakhir  = 0UL - JEDA_SMART_MS;
unsigned long waktuManualTerakhir = 0UL - JEDA_MANUAL_MS;

String        statusTerakhir = "";
unsigned long tahanSampai = 0;

// ------------------------------------------------------------
// UTILITAS
// ------------------------------------------------------------
bool   waktuValid() { return time(nullptr) > 1700000000; }
time_t waktuLokal() { return time(nullptr) + (time_t)cfg.zonaMenit * 60; }

// Kirim status ke V2 hanya jika berubah. 'penting' = tahan 8 dtk agar tidak tertimpa status rutin.
void setStatus(const String& teks, bool penting = false) {
  if (penting) tahanSampai = millis() + 8000UL;
  else if ((long)(tahanSampai - millis()) > 0) return;
  if (teks == statusTerakhir) return;
  statusTerakhir = teks;
  Blynk.virtualWrite(V2, teks);
}

void simpanKonfigurasi() {
  EEPROM.put(EEPROM_ADDR_CFG, cfg);
  EEPROM.commit();
}

void muatKonfigurasi() {
  EEPROM.get(EEPROM_ADDR_CFG, cfg);
  if (cfg.magic != EEPROM_MAGIC) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = EEPROM_MAGIC;
    cfg.mode = MODE_MANUAL;
    cfg.zonaMenit = ZONA_WAKTU_DEFAULT_MENIT;
    cfg.hari = -1;
    simpanKonfigurasi();
    return;
  }
  if (cfg.mode < MODE_MANUAL || cfg.mode > MODE_SCHEDULE) cfg.mode = MODE_MANUAL;
  if (cfg.zonaMenit < -720 || cfg.zonaMenit > 840) cfg.zonaMenit = ZONA_WAKTU_DEFAULT_MENIT;
  if (cfg.smartN < 0 || cfg.smartN > MAKS_SMART_PER_HARI) cfg.smartN = 0;
  if (cfg.jumlah < 0 || cfg.jumlah > MAKS_JADWAL) cfg.jumlah = 0;
  for (int i = 0; i < cfg.jumlah; i++) {
    if (cfg.jadwal[i] < 0 || cfg.jadwal[i] > 1439) { cfg.jumlah = 0; break; }
  }
}

String jadwalSebagaiTeks() {   // contoh: "07:00, 13:30"
  String hasil;
  char buf[8];
  for (int i = 0; i < cfg.jumlah; i++) {
    snprintf(buf, sizeof(buf), "%02d:%02d", cfg.jadwal[i] / 60, cfg.jadwal[i] % 60);
    if (i) hasil += ", ";
    hasil += buf;
  }
  return hasil;
}

// ------------------------------------------------------------
// SENSOR
// ------------------------------------------------------------const float TARGET_PORSI_GRAM   = 50.0;  // Katup menutup saat pertambahan berat piring mencapai target ini (gram)
// Jarak (cm), atau -1 jika sensor tidak merespons
float bacaJarakUltrasonik() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long durasi = pulseIn(PIN_ECHO, HIGH, 30000);
  return durasi == 0 ? -1.0f : durasi * 0.0343f / 2.0f;
}

// Sisa pakan di tabung, 0..100 %
int hitungPersen(float jarak_cm) {
  int persen = lroundf((TINGGI_WADAH_CM - jarak_cm) / TINGGI_WADAH_CM * 100.0f);
  return constrain(persen, 0, 100);
}

// Berat piring (gram), atau NAN jika HX711 tidak merespons
float bacaBeratLoadCell() {
  if (!scale.wait_ready_timeout(1000)) {
    Serial.println(F("[ERROR] HX711 tidak merespons!"));
    return NAN;
  }
  return fmaxf(scale.get_units(5), 0.0f);
}

// ------------------------------------------------------------
// KATUP SERVO (non-blocking, berdasarkan berat)
// ------------------------------------------------------------
void selesaiMemberi() {
  myServo.detach();
  sedangMemberi = false;

  float akhir = std::isnan(beratAwalPorsi) ? NAN : bacaBeratLoadCell();
  if (std::isnan(akhir)) { setStatus("Selesai Mengisi", true); return; }

  float tambah = akhir - beratAwalPorsi;
  Serial.printf("[AKSI] Katup tertutup, pertambahan berat: %.1f g\n", tambah);
  setStatus(tambah < MINIMAL_PORSI_VALID_GRAM
              ? String("Pakan Macet?")
              : "Selesai Mengisi (+" + String((int)lroundf(tambah)) + " g)", true);
}

void tutupKatup() {
  myServo.write(SERVO_TUTUP);
  katupTerbuka = false;
  timer.setTimeout(JEDA_GERAK_SERVO, selesaiMemberi);   // tunggu servo selesai bergerak
}

// Dipanggil tiap INTERVAL_PANTAU_PORSI_MS; menutup katup saat porsi tercapai / waktu habis
void pantauPorsi() {
  if (!katupTerbuka) return;

  unsigned long lama = millis() - waktuMulaiBuka;
  bool tutup = lama >= MAKSIMAL_WAKTU_BUKA;

  if (!tutup) {
    if (std::isnan(beratAwalPorsi)) {
      tutup = lama >= DURASI_BUKA_CADANGAN_MS;   // load cell error: pakai durasi
    } else if (scale.is_ready()) {
      tutup = fmaxf(scale.get_units(1), 0.0f) - beratAwalPorsi >= TARGET_PORSI_GRAM - MARGIN_JATUHAN_GRAM;
    }
  }
  if (tutup) tutupKatup();
}

// Pengaman universal untuk SEMUA mode. Return true jika katup benar-benar dibuka.
bool beriPakan(const char* sumber) {
  if (sedangMemberi) return false;

  float jarak = bacaJarakUltrasonik();
  if (jarak >= 0 && hitungPersen(jarak) <= BATAS_TABUNG_HABIS_PERSEN) {
    setStatus("Tabung Habis!", true);
    return false;
  }

  float berat = bacaBeratLoadCell();
  if (!std::isnan(berat) && berat >= BATAS_PIRING_PENUH_GRAM) {
    setStatus("Piring Penuh", true);
    return false;
  }

  sedangMemberi = katupTerbuka = true;
  beratAwalPorsi = berat;               // NAN -> pakai durasi cadangan
  waktuMulaiBuka = millis();
  setStatus("Mengisi Pakan...", true);
  Serial.printf("[AKSI] Buka katup (%s)\n", sumber);

  myServo.attach(PIN_SERVO);
  myServo.write(SERVO_BUKA);
  return true;
}

// ------------------------------------------------------------
// SMART MODE & SCHEDULE
// ------------------------------------------------------------
bool smartMasihJeda() {
  if (millis() - waktuSmartTerakhir < JEDA_SMART_MS) return true;
  if (!waktuValid() || cfg.smartT == 0) return false;
  uint32_t sekarang = (uint32_t)time(nullptr);
  return sekarang >= cfg.smartT && sekarang - cfg.smartT < JEDA_SMART_MS / 1000UL;
}

// Reset status harian saat hari lokal berganti (tersimpan di EEPROM, aman dari reboot)
void periksaGantiHari() {
  if (!waktuValid()) return;
  int32_t hari = (int32_t)(waktuLokal() / 86400);
  if (hari == cfg.hari) return;
  cfg.mask = 0;
  cfg.smartN = 0;
  cfg.hari = hari;
  simpanKonfigurasi();
}

// Alasan Smart Mode menunda pemberian; nullptr = boleh memberi pakan
const char* alasanSmartTunda(float berat, int persen) {
  if (!waktuValid() && millis() < WAKTU_TUNGGU_NTP_MS) return "Menunggu Sinkron Waktu";
  if (std::isnan(berat))                  return "Sensor Berat Error";
  if (berat >= BATAS_BERAT_MINIMUM)       return "Pakan Cukup";
  if (persen < 0)                         return "Sensor Tabung Error";
  if (persen <= BATAS_TABUNG_HABIS_PERSEN) return "Tabung Habis!";
  if (cfg.smartN >= MAKS_SMART_PER_HARI)  return "Batas Harian Tercapai";
  if (smartMasihJeda())                   return "Jeda Waktu";
  return nullptr;
}

void cekJadwalMakan() {
  if (!waktuValid()) return;
  periksaGantiHari();
  if (cfg.mode != MODE_SCHEDULE) return;

  int menitSekarang = (int)((waktuLokal() % 86400) / 60);
  for (int i = 0; i < cfg.jumlah; i++) {
    // Jika ditolak (katup sibuk/tabung habis/piring penuh), dicoba lagi 15 dtk berikutnya
    if (cfg.jadwal[i] == menitSekarang && !((cfg.mask >> i) & 1) && beriPakan("SCHEDULE")) {
      cfg.mask |= (uint8_t)(1 << i);
      simpanKonfigurasi();
    }
  }
}

// ------------------------------------------------------------
// PENGAWASAN & TELEMETRI (tiap 4 detik)
// ------------------------------------------------------------
void loopPengawasan() {
  Blynk.virtualWrite(V9, (long)WiFi.RSSI());
  if (sedangMemberi) return;   // pemantauan berat ditangani pantauPorsi()

  String alert = "";

  // 1) Cek Ultrasonik
  float jarak = bacaJarakUltrasonik();
  int persen = jarak >= 0 ? hitungPersen(jarak) : -1;
  if (persen >= 0) Blynk.virtualWrite(V0, persen);
  else {
    Serial.println(F("[ERROR] Ultrasonik gagal merespons!"));
    alert += "ULTRASONIK: Tidak merespons. ";
  }

  // 2) Cek Load Cell
  float berat = bacaBeratLoadCell();
  if (!std::isnan(berat)) {
    Blynk.virtualWrite(V1, berat);
  } else {
    Serial.println(F("[ERROR] HX711 tidak merespons!"));
    alert += "LOADCELL: Tidak merespons. ";
  }

  // 3) Opsional: peringatan sinyal lemah
  long rssi = WiFi.RSSI();
  if (rssi <= -90) {
    alert += "WIFI: Sinyal lemah (" + String(rssi) + " dBm). ";
  }

  // Kirim hanya jika ada masalah DAN berbeda dari sebelumnya
  if (alert != alertSensorTerakhir) {
    alertSensorTerakhir = alert;
    // Jika normal, kirim string kosong biar widget jadi bersih
    Blynk.virtualWrite(V10, alertSensorTerakhir);
  }
  
  switch (cfg.mode) {
    case MODE_MANUAL:
      setStatus("Mode Manual");
      break;

    case MODE_SCHEDULE:
      setStatus(waktuValid() ? "Standby: " + String(cfg.jumlah) + " jadwal"
                             : String("Menunggu Sinkron Waktu"));
      break;

    case MODE_SMART: {
      periksaGantiHari();
      const char* tunda = alasanSmartTunda(berat, persen);
      if (tunda) { setStatus(tunda); break; }

      Serial.println(F("[SMART] Wadah kosong, mengisi ulang..."));
      if (beriPakan("SMART")) {
        waktuSmartTerakhir = millis();
        if (waktuValid()) cfg.smartT = (uint32_t)time(nullptr);
        cfg.smartN++;
        simpanKonfigurasi();   // simpan segera agar cooldown selamat dari reboot
      }
      break;
    }
  }
}

// ------------------------------------------------------------
// BLYNK
// ------------------------------------------------------------
BLYNK_CONNECTED() {
  statusTerakhir = "";   // paksa status terkini terkirim ulang
  Blynk.virtualWrite(V5, cfg.zonaMenit / 60.0f);
  Blynk.virtualWrite(V6, cfg.mode);
  if (cfg.jumlah > 0) Blynk.virtualWrite(V4, jadwalSebagaiTeks());
}

// V3: tombol pakan manual
BLYNK_WRITE(V3) {
  if (param.asInt() != 1) return;
  if (cfg.mode != MODE_MANUAL) { setStatus("Bukan Mode Manual", true); return; }
  if (millis() - waktuManualTerakhir < JEDA_MANUAL_MS) { setStatus("Tunggu Sebentar", true); return; }
  if (beriPakan("MANUAL")) waktuManualTerakhir = millis();
}

// V4: jadwal lewat text input, contoh "08:00, 13:30, 19:00" (maks. 5)
BLYNK_WRITE(V4) {
  String teks = param.asStr();

  // Simpan jadwal lama agar jadwal identik tidak dieksekusi ulang hari ini
  int16_t jadwalLama[MAKS_JADWAL];
  memcpy(jadwalLama, cfg.jadwal, sizeof(jadwalLama));
  int     jumlahLama = cfg.jumlah;
  uint8_t maskLama   = cfg.mask;
  cfg.jumlah = 0;
  cfg.mask = 0;

  bool terpotong = false;
  for (int awal = 0; awal <= (int)teks.length(); ) {
    int koma = teks.indexOf(',', awal);
    if (koma < 0) koma = teks.length();
    String waktu = teks.substring(awal, koma);
    waktu.trim();
    awal = koma + 1;

    int jam = -1, menit = -1;
    if (sscanf(waktu.c_str(), "%d:%d", &jam, &menit) != 2 ||
        jam < 0 || jam > 23 || menit < 0 || menit > 59) continue;   // format salah / kosong

    int m = jam * 60 + menit;
    bool ganda = false;
    for (int i = 0; i < cfg.jumlah; i++) if (cfg.jadwal[i] == m) ganda = true;
    if (ganda) continue;
    if (cfg.jumlah >= MAKS_JADWAL) { terpotong = true; break; }

    for (int j = 0; j < jumlahLama; j++) {
      if (jadwalLama[j] == m && ((maskLama >> j) & 1)) cfg.mask |= (uint8_t)(1 << cfg.jumlah);
    }
    cfg.jadwal[cfg.jumlah++] = m;
  }

  simpanKonfigurasi();
  Serial.printf("[JADWAL] Total jadwal aktif: %d\n", cfg.jumlah);

  if (cfg.jumlah > 0) {
    Blynk.virtualWrite(V4, jadwalSebagaiTeks());   // tampilkan versi yang sudah dirapikan
    setStatus("Jadwal Tersimpan: " + String(cfg.jumlah) + (terpotong ? " (maks 5)" : ""), true);
  } else {
    setStatus("Format Jadwal Salah", true);
  }
}

// V5: zona waktu dalam jam (boleh pecahan: 7 = WIB, 5.5 = India)
BLYNK_WRITE(V5) {
  int menit = (int)lroundf(param.asFloat() * 60.0f);
  if (menit < -720 || menit > 840) {
    Blynk.virtualWrite(V5, cfg.zonaMenit / 60.0f);   // kembalikan tampilan ke nilai lama
    return;
  }
  cfg.zonaMenit = menit;
  simpanKonfigurasi();
}

// V6: pilihan mode
BLYNK_WRITE(V6) {
  int mode = param.asInt();
  if (mode < MODE_MANUAL || mode > MODE_SCHEDULE) {
    Blynk.virtualWrite(V6, cfg.mode);
    return;
  }
  cfg.mode = mode;
  simpanKonfigurasi();
  Serial.printf("[CONFIG] Mode: %d\n", mode);
}

// V7: tare manual (pastikan piring KOSONG)
BLYNK_WRITE(V7) {
  if (param.asInt() != 1) return;
  if (sedangMemberi) { setStatus("Tare Ditolak (Katup Aktif)", true); return; }
  if (!scale.wait_ready_timeout(1000)) { setStatus("HX711 Error", true); return; }

  scale.tare();
  long offset = scale.get_offset();
  EEPROM.put(EEPROM_ADDR_OFFSET, offset);
  EEPROM.commit();
  setStatus("Kalibrasi Selesai", true);
}

// ------------------------------------------------------------
// SETUP & LOOP
// ------------------------------------------------------------
// Sambung ulang Blynk tanpa memblokir (dan tidak saat katup terbuka)
void jagaKoneksiBlynk() {
  static unsigned long terakhirCoba = 0;

  if (WiFi.status() != WL_CONNECTED) return;
  if (Blynk.connected()) { Blynk.run(); return; }

  // Blynk.connect() bisa memblokir hingga 3 dtk -> bisa menunda penutupan katup
  if (sedangMemberi) return;

  if (terakhirCoba == 0 || millis() - terakhirCoba >= 10000UL) {
    terakhirCoba = millis();
    Blynk.connect(3000);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== Smart Pet Feeder ==="));

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  EEPROM.begin(512);

  // Load cell: pakai offset tersimpan, atau tare awal jika alat baru di-flash
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_scale(DEFAULT_CALIBRATION);
  long offset;
  EEPROM.get(EEPROM_ADDR_OFFSET, offset);
  if (offset != 0 && offset != -1) {
    scale.set_offset(offset);
  } else if (scale.wait_ready_timeout(2000)) {
    scale.tare();
    offset = scale.get_offset();
    EEPROM.put(EEPROM_ADDR_OFFSET, offset);
    EEPROM.commit();
  } else {
    Serial.println(F("[ERROR] HX711 tidak merespons, Tare awal dilewati."));
  }

  muatKonfigurasi();   // sebelum Blynk terhubung, agar nilai di aplikasi benar

  // Pastikan katup tertutup saat menyala
  myServo.attach(PIN_SERVO);
  myServo.write(SERVO_TUTUP);
  delay(JEDA_GERAK_SERVO);
  myServo.detach();

  // Wi-Fi & Blynk tidak memblokir: alat tetap jalan walau Wi-Fi mati
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // UTC; zona waktu diterapkan manual

  timer.setInterval(4000L, loopPengawasan);
  timer.setInterval(15000L, cekJadwalMakan);
  timer.setInterval((long)INTERVAL_PANTAU_PORSI_MS, pantauPorsi);

  Serial.println(F("[-] Sistem siap."));
}

void loop() {
  jagaKoneksiBlynk();
  timer.run();
}
