#include <Arduino.h>
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
// KONFIGURASI TERSIMPAN (EEPROM)
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
bool          sedangMemberi  = false;  // dari buka sampai verifikasi selesai
bool          katupTerbuka   = false;  // hanya selama fase buka (berat dipantau)
unsigned long waktuMulaiBuka = 0;
float         beratAwalPorsi = NAN;

// Cooldown berbasis millis (nilai awal membuat cooldown tidak aktif saat boot)
unsigned long waktuSmartTerakhir  = 0UL - JEDA_SMART_MS;
unsigned long waktuManualTerakhir = 0UL - JEDA_MANUAL_MS;

String        statusTerakhir = "";
unsigned long tahanSampai = 0;
String        alertSensorTerakhir = "";

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

void simpanOffset() {
  EEPROM.put(EEPROM_ADDR_OFFSET, scale.get_offset());
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

// Format teks jadwal untuk sinkronisasi tampilan Blynk (mis. "07:00, 13:30")
String jadwalSebagaiTeks() {
  String hasil;
  char buf[16];
  for (int i = 0; i < cfg.jumlah; i++) {
    snprintf(buf, sizeof(buf), "%02d:%02d", cfg.jadwal[i] / 60, cfg.jadwal[i] % 60);
    if (i) hasil += ", ";
    hasil += buf;
  }
  return hasil;
}

// "HH:MM" -> menit sejak 00:00, atau -1 jika format salah
int parseMenit(String teks) {
  teks.trim();
  int jam, menit;
  if (sscanf(teks.c_str(), "%d:%d", &jam, &menit) != 2 ||
      jam < 0 || jam > 23 || menit < 0 || menit > 59) return -1;
  return jam * 60 + menit;
}

// Indeks 'menit' di dalam daftar jadwal, atau -1
int cariJadwal(const int16_t* daftar, int n, int menit) {
  for (int i = 0; i < n; i++) if (daftar[i] == menit) return i;
  return -1;
}

// ------------------------------------------------------------
// SENSOR
// ------------------------------------------------------------
// Membaca jarak ultrasonik (cm). Menggunakan median filter 3 sampel
// dan jeda 40ms antar-ping untuk meredam gema pantulan di dalam wadah silinder.
// Mengembalikan -1.0 jika sensor tidak merespons (timeout).
float bacaJarakUltrasonik() {
  float bacaan[3];
  int count = 0;

  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    // Timeout 25000 us (~4.2 meter)
    long durasi = pulseIn(PIN_ECHO, HIGH, 25000);
    if (durasi > 0) {
      float d = durasi * 0.0343f / 2.0f;
      // Filter jarak fisik valid (jarak <= batas minimal di-clamp 100% oleh hitungPersen)
      if (d > 0.5f) { 
        bacaan[count++] = d;
      }
    }
    if (i < 2) delay(40); // Jeda redaman gema pantulan tabung
  }

  if (count == 0) return -1.0f;

  // Sorting sampel valid
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (bacaan[i] > bacaan[j]) {
        float temp = bacaan[i];
        bacaan[i] = bacaan[j];
        bacaan[j] = temp;
      }
    }
  }

  // Median filter untuk menolak noise/glitch
  if (count == 3) return bacaan[1];
  if (count == 2) return (bacaan[0] + bacaan[1]) / 2.0f;
  return bacaan[0];
}

// Menghitung persentase sisa pakan (0..100%). Return -1 jika pembacaan tidak valid.
int hitungPersen(float jarak_cm) {
  if (jarak_cm < 0.0f) return -1;
  
  // Tabung penuh atau masuk zona blind spot sensor (<= 4 cm)
  if (jarak_cm <= JARAK_MINIMAL_TABUNG_CM) return 100; 
  // Tabung kosong atau jarak melebihi tinggi dasar wadah
  if (jarak_cm >= TINGGI_WADAH_CM) return 0;           

  float rentang = TINGGI_WADAH_CM - JARAK_MINIMAL_TABUNG_CM;
  if (rentang <= 0.0f) return 0;

  // Pemetaan persentase linier
  int persen = lroundf((TINGGI_WADAH_CM - jarak_cm) / rentang * 100.0f);
  return constrain(persen, 0, 100);
}

// Membaca berat wadah makan (gram). Return NAN jika modul HX711 timeout/tidak merespons.
float bacaBeratLoadCell() {
  if (!scale.wait_ready_timeout(1000)) return NAN;
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
  if (std::isnan(berat))                   return "Sensor Berat Error";
  if (berat >= BATAS_BERAT_MINIMUM)        return "Pakan Cukup";
  if (persen < 0)                          return "Sensor Tabung Error";
  if (persen <= BATAS_TABUNG_HABIS_PERSEN) return "Tabung Habis!";
  if (cfg.smartN >= MAKS_SMART_PER_HARI)   return "Batas Harian Tercapai";
  if (smartMasihJeda())                    return "Jeda Waktu";
  return nullptr;
}

void cekJadwalMakan() {
  if (!waktuValid()) return;
  periksaGantiHari();
  if (cfg.mode != MODE_SCHEDULE) return;

  int menitSekarang = (int)((waktuLokal() % 86400) / 60);
  for (int i = 0; i < cfg.jumlah; i++) {
    // Jika ditolak (katup sibuk/tabung habis/piring penuh), dicoba lagi pada siklus berikutnya
    if (cfg.jadwal[i] == menitSekarang && !((cfg.mask >> i) & 1) && beriPakan("SCHEDULE")) {
      cfg.mask |= (uint8_t)(1 << i);
      simpanKonfigurasi();
    }
  }
}

// ------------------------------------------------------------
// PENGAWASAN & TELEMETRI
// ------------------------------------------------------------
void loopPengawasan() {
  long rssi = WiFi.RSSI();
  Blynk.virtualWrite(V9, rssi);
  if (sedangMemberi) return;   // pemantauan berat ditangani pantauPorsi()

  String alert;

  float jarak = bacaJarakUltrasonik();
  int persen = jarak >= 0 ? hitungPersen(jarak) : -1;
  if (persen >= 0) Blynk.virtualWrite(V0, persen);
  else             alert += "ULTRASONIK: Tidak merespons. ";

  float berat = bacaBeratLoadCell();
  if (!std::isnan(berat)) Blynk.virtualWrite(V1, berat);
  else                    alert += "LOADCELL: Tidak merespons. ";

  if (rssi <= -90) alert += "WIFI: Sinyal lemah (" + String(rssi) + " dBm). ";

  if (alert.length()) Serial.println("[ERROR] " + alert);

  // Kirim hanya jika berbeda dari sebelumnya (string kosong = membersihkan widget)
  if (alert != alertSensorTerakhir) {
    alertSensorTerakhir = alert;
    Blynk.virtualWrite(V8, alert);
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
  statusTerakhir = "";         // paksa status & peringatan terkini terkirim ulang
  alertSensorTerakhir = "";
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
  const int     jumlahLama = cfg.jumlah;
  const uint8_t maskLama   = cfg.mask;
  cfg.jumlah = 0;
  cfg.mask = 0;

  bool terpotong = false;
  const int panjang = teks.length();
  for (int awal = 0, akhir; awal <= panjang; awal = akhir + 1) {
    akhir = teks.indexOf(',', awal);
    if (akhir < 0) akhir = panjang;

    int m = parseMenit(teks.substring(awal, akhir));
    if (m < 0 || cariJadwal(cfg.jadwal, cfg.jumlah, m) >= 0) continue;   // salah format / ganda
    if (cfg.jumlah >= MAKS_JADWAL) { terpotong = true; break; }

    int lama = cariJadwal(jadwalLama, jumlahLama, m);
    if (lama >= 0 && ((maskLama >> lama) & 1)) cfg.mask |= (uint8_t)(1 << cfg.jumlah);
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
  simpanOffset();
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
    simpanOffset();
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

  timer.setInterval(INTERVAL_PENGAWASAN_MS, loopPengawasan);
  timer.setInterval(INTERVAL_JADWAL_MS, cekJadwalMakan);
  timer.setInterval(INTERVAL_PANTAU_PORSI_MS, pantauPorsi);

  Serial.println(F("[-] Sistem siap."));
}

void loop() {
  jagaKoneksiBlynk();
  timer.run();
}
