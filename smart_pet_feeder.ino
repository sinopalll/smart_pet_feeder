/*
 * ============================================================
 *  SMART PET FEEDER - MULTI-MODE CONTROLLER
 *  Mode: 0 = Manual (Button), 1 = Smart (Load Cell), 2 = Schedule (NTP)
 *  Kelompok 5
 * ============================================================
 */

#define BLYNK_TEMPLATE_ID   "TMPL6BR0CA82x"       // Ganti dengan Template ID Anda
#define BLYNK_TEMPLATE_NAME "smart pet feeder" // Ganti dengan nama Template Anda
#define BLYNK_AUTH_TOKEN    "f5Y8rcJgyBlFJyhH-FIC3-p1ieT1f-_W"  // Ganti dengan Token Perangkat Anda
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <HX711.h>
#include <Servo.h>
#include <time.h>
#include <EEPROM.h>

// 2. KONEKSI WI-FI
char ssid[] = "esp8266";
char pass[] = "q2w3e4r5t6!";

// Pemetaan Pin GPIO
#define PIN_TRIG        13  // D7
#define PIN_ECHO        15  // D8
#define PIN_HX711_DT    14  // D5
#define PIN_HX711_SCK   12  // D6
#define PIN_SERVO       2   // D4

// Definisi Konstanta Mode
#define MODE_MANUAL   1
#define MODE_SMART    2
#define MODE_SCHEDULE 3
#define MAKSIMAL_JADWAL 5

// Variabel Pengatur Sistem
const int EEPROM_ADDR_OFFSET = 0; // Alamat EEPROM untuk menyimpan nilai offset Load Cell
long loadcell_offset = 0;         // Variabel untuk menampung offset

int modeOperasi = MODE_MANUAL; // Default awal adalah manual
float calibration_factor = 392.0;
const float BATAS_BERAT_MINIMUM = 20.0;

int jadwalJam[MAKSIMAL_JADWAL];
int jadwalMenit[MAKSIMAL_JADWAL];
bool sudahMakan[MAKSIMAL_JADWAL];
int jumlahJadwal = 0; // Menyimpan info berapa banyak jadwal yang aktif

HX711 scale;
Servo myServo;
BlynkTimer timer;

// Prototipe Fungsi
long bacaJarakUltrasonik();
float bacaBeratLoadCell();
void loopPengawasan();
void cekJadwalMakan();
void beriPakan();

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(F("\n=== Smart Pet Feeder - Multi Mode ==="));

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  // Mulai EEPROM dengan alokasi 512 byte
  EEPROM.begin(512); 
  
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_scale(calibration_factor);

  // Baca nilai offset terakhir dari EEPROM
  EEPROM.get(EEPROM_ADDR_OFFSET, loadcell_offset);

  // Jika nilainya wajar (bukan -1 atau not a number), gunakan offset tersebut
  if (loadcell_offset != 0 && loadcell_offset != -1) {
    scale.set_offset(loadcell_offset);
    Serial.print(F("[-] Offset Load Cell dimuat dari memori: "));
    Serial.println(loadcell_offset);
  } else {
    // Jika EEPROM masih kosong (alat pertama kali di-flash), lakukan tare awal
    Serial.println(F("[-] Memori kosong, melakukan Tare awal..."));
    scale.tare();
    loadcell_offset = scale.get_offset(); // Ambil angka nol-nya
    EEPROM.put(EEPROM_ADDR_OFFSET, loadcell_offset); // Simpan
    EEPROM.commit(); // Eksekusi simpan ke memori
  }

  myServo.attach(PIN_SERVO);
  myServo.write(0);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // NTP Time Sync (WIB = UTC+7)
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");

  // Timer: Pembacaan sensor dan telemetri (tiap 4 detik)
  timer.setInterval(4000L, loopPengawasan);

  // Timer: Pengecekan jam untuk Mode Schedule (tiap 15 detik)
  timer.setInterval(15000L, cekJadwalMakan);

  Serial.println(F("[-] Sistem siap beroperasi."));
}

void loop() {
  Blynk.run();
  timer.run();
}

// ------------------------------------------------------------
// LOGIKA PENGAWASAN DAN SIKLUS SENSOR
// ------------------------------------------------------------
void loopPengawasan() {
  long kuatSinyal = WiFi.RSSI(); // Membaca kekuatan sinyal dalam satuan dBm
  Blynk.virtualWrite(V9, kuatSinyal); 

  // 1. Baca Ultrasonik (Selalu aktif)
  long jarak_cm = bacaJarakUltrasonik();
  Blynk.virtualWrite(V0, jarak_cm);

  // 2. Baca Berat (Selalu aktif untuk update indikator di Dashboard Blynk)
  float berat_gram = bacaBeratLoadCell();
  Blynk.virtualWrite(V1, (int)berat_gram); 

  // 3. Logika Mode
  switch (modeOperasi) {
    case MODE_MANUAL:
      Serial.println(F("[MODE] MANUAL: Menunggu pemicu tombol Blynk."));
      Blynk.virtualWrite(V2, "Mode Manual");
      break;

    case MODE_SMART:
      Serial.print(F("[MODE] SMART: Berat terbaca: "));
      Serial.print((int)berat_gram);
      Serial.println(F(" gr"));

      // Hanya dieksekusi otomatis jika mode SMART
      if (berat_gram < BATAS_BERAT_MINIMUM) {
        Serial.println(F("[TRIGGER SMART] Wadah kosong, mengisi ulang..."));
        beriPakan();
      } else {
        Blynk.virtualWrite(V2, "Pakan Cukup");
      }
      break;

    case MODE_SCHEDULE:
      Serial.println(F("[MODE] SCHEDULE: Berjalan sesuai pengaturan waktu NTP."));
      Blynk.virtualWrite(V2, "Standby Jadwal");
      break;
  }
}

// ------------------------------------------------------------
// LOGIKA MODE SCHEDULE (WAKTU NTP)
// ------------------------------------------------------------
void cekJadwalMakan() {
  if (modeOperasi != MODE_SCHEDULE) return;

  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);

  int jamSekarang = p_tm->tm_hour;
  int menitSekarang = p_tm->tm_min;

  // Reset status harian setiap jam 00:00 (Tengah malam)
  if (jamSekarang == 0 && menitSekarang == 0) {
    for (int i = 0; i < jumlahJadwal; i++) {
      sudahMakan[i] = false; 
    }
  }

  // Cek secara berurutan, apakah jam & menit saat ini cocok dengan salah satu jadwal?
  for (int i = 0; i < jumlahJadwal; i++) {
    if (jadwalJam[i] != -1 && jadwalMenit[i] != -1) {
      if (jamSekarang == jadwalJam[i] && menitSekarang == jadwalMenit[i]) {
        if (!sudahMakan[i]) { // Pastikan belum diberi makan pada jadwal ini
          Serial.print(F("[TRIGGER SCHEDULE] Waktu makan ke-"));
          Serial.print(i + 1);
          Serial.println(F(" tercapai!"));
          
          beriPakan();
          sudahMakan[i] = true; // Tandai bahwa jadwal ini sudah dieksekusi hari ini
        }
      }
    }
  }
}

// ------------------------------------------------------------
// EKSEKUSI KATUP SERVO
// ------------------------------------------------------------
void beriPakan() {
  Blynk.virtualWrite(V2, "Mengisi Pakan...");
  Serial.println(F("[AKSI] Membuka katup servo..."));

  myServo.write(90);
  delay(2000); // Durasi jatuh pakan
  myServo.write(0);

  Serial.println(F("[AKSI] Katup tertutup."));
  Blynk.virtualWrite(V2, "Selesai Mengisi");
  delay(2000); // Jeda stabilisasi
}

// ------------------------------------------------------------
// VIRTUAL PIN HANDLER (BLYNK)
// ------------------------------------------------------------

BLYNK_CONNECTED() {
  Serial.println(F("[NETWORK] Berhasil terhubung ke WiFi dan Server Blynk!"));
  
  // Mengirim teks "ONLINE" ke Virtual Pin V8 saat terhubung
  Blynk.virtualWrite(V8, "ONLINE");
  
  // (Opsional) Sinkronisasi status tombol/jadwal dari server ke alat
  Blynk.syncVirtual(V4, V5, V6); 
}

// V6: Pemilihan Mode oleh Pengguna
BLYNK_WRITE(V6) {
  modeOperasi = param.asInt();
  Serial.print(F("[CONFIG] Mode berganti ke: "));
  if (modeOperasi == MODE_MANUAL) Serial.println(F("MANUAL"));
  else if (modeOperasi == MODE_SMART) Serial.println(F("SMART (Load Cell)"));
  else if (modeOperasi == MODE_SCHEDULE) Serial.println(F("SCHEDULE (NTP)"));
}

// V3: Pemicu Manual (Push Button)
BLYNK_WRITE(V3) {
  int nilaiTombol = param.asInt();
  if (nilaiTombol == 1) {
    if (modeOperasi == MODE_MANUAL) {
      Serial.println(F("[MANUAL] Tombol ditekan, pakan dikeluarkan."));
      beriPakan();
    } else {
      Serial.println(F("[WARNING] Tombol manual diabaikan (Alat tidak berada di Mode Manual)."));
    }
  }
}

// V4: Pengaturan Multi-Jadwal Menggunakan Text Input (Contoh: "08:00, 13:30, 19:00")
BLYNK_WRITE(V4) {
  String textStr = param.asStr();
  Serial.print(F("[JADWAL] Teks diterima: ")); 
  Serial.println(textStr);
  
  // 1. Bersihkan memori jadwal sebelumnya
  jumlahJadwal = 0;
  for(int i = 0; i < MAKSIMAL_JADWAL; i++){
    jadwalJam[i] = -1;
    jadwalMenit[i] = -1;
    sudahMakan[i] = false;
  }
  
  // 2. Pecah teks berdasarkan tanda koma (,)
  int lastIndex = 0;
  for (int i = 0; i <= textStr.length(); i++) {
    // Jika menemukan koma ATAU mencapai akhir teks
    if (textStr[i] == ',' || i == textStr.length()) {
      String waktu = textStr.substring(lastIndex, i);
      waktu.trim(); // Hapus spasi kosong jika ada
      
      // Jika formatnya benar (ada tanda titik dua ':')
      if (waktu.length() >= 3 && waktu.indexOf(':') > 0) {
        int titikDua = waktu.indexOf(':');
        jadwalJam[jumlahJadwal] = waktu.substring(0, titikDua).toInt();
        jadwalMenit[jumlahJadwal] = waktu.substring(titikDua + 1).toInt();
        sudahMakan[jumlahJadwal] = false;
        
        Serial.print(F(" -> Jadwal tersimpan: "));
        Serial.print(jadwalJam[jumlahJadwal]); Serial.print(F(":")); Serial.println(jadwalMenit[jumlahJadwal]);
        
        jumlahJadwal++;
        if (jumlahJadwal >= MAKSIMAL_JADWAL) break; // Batasi maksimal 5 jadwal
      }
      lastIndex = i + 1; // Geser pencarian untuk teks berikutnya
    }
  }
  Serial.print(F("[JADWAL] Total jadwal aktif: ")); Serial.println(jumlahJadwal);
}

// V7: Tombol Kalibrasi Nol / Tare Manual (Mode PUSH)
BLYNK_WRITE(V7) {
  int nilaiTombol = param.asInt();
  if (nilaiTombol == 1) {
    Serial.println(F("[KALIBRASI] Melakukan Tare..."));
    Blynk.virtualWrite(V2, "Kalibrasi Wadah...");

    scale.tare(); // Mengenolkan timbangan

    // Ambil nilai titik nol yang baru dan simpan ke EEPROM
    loadcell_offset = scale.get_offset();
    EEPROM.put(EEPROM_ADDR_OFFSET, loadcell_offset);
    EEPROM.commit(); // Wajib agar tersimpan permanen di memori

    Serial.print(F("[KALIBRASI] Selesai. Offset baru: "));
    Serial.println(loadcell_offset);
    Blynk.virtualWrite(V2, "Kalibrasi Selesai");
  }
}

// ------------------------------------------------------------
// FUNGSI SENSOR
// ------------------------------------------------------------
long bacaJarakUltrasonik() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long durasi = pulseIn(PIN_ECHO, HIGH, 30000);
  return (durasi * 0.034 / 2);
}

float bacaBeratLoadCell() {
  float berat = scale.get_units(5);
  if (berat < 0) berat = 0;
  return berat;
}