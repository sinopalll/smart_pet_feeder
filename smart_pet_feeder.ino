#include "secrets.h"
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <HX711.h>
#include <Servo.h>
#include <time.h>
#include <EEPROM.h>

#define BLYNK_TEMPLATE_ID   "TMPL6BR0CA82x"
#define BLYNK_TEMPLATE_NAME "smart pet feeder"
#define BLYNK_AUTH_TOKEN    SECRET_BLYNK_TOKEN
#define BLYNK_PRINT Serial

// 2. KONEKSI WI-FI
char ssid[] = SECRET_WIFI_SSID;
char pass[] = SECRET_WIFI_PASS;

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
const int EEPROM_ADDR_TZ = 4;     // Alamat EEPROM untuk Zona Waktu
int zonaWaktu = 7;                // Default awal: 7 (WIB / UTC+7)
int modeSebelumnya = -1; // Menyimpan mode terakhir yang dicetak

int modeOperasi = MODE_MANUAL; // Default awal adalah manual
float calibration_factor = 392.0;
const float BATAS_BERAT_MINIMUM = 20.0; // Berat minimum pakan dipiring (gr)
const float TINGGI_WADAH_CM = 20.0; // Tinggi wadah pakan utama (cm)

int jadwalJam[MAKSIMAL_JADWAL];
int jadwalMenit[MAKSIMAL_JADWAL];
bool sudahMakan[MAKSIMAL_JADWAL];
int jumlahJadwal = 0; // Menyimpan info berapa banyak jadwal yang aktif

// Cek apakah tahun sudah di atas 2023 (berarti NTP sudah sukses sinkron)
bool waktuValid() {
  return time(nullptr) > 1700000000; 
}

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

  myServo.write(0);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // --- PENGATURAN ZONA WAKTU DINAMIS ---
  EEPROM.get(EEPROM_ADDR_TZ, zonaWaktu); // Baca zona waktu dari memori
  
  // Validasi: Jika memori kosong/error, paksa ke default UTC+7
  if (zonaWaktu < -12 || zonaWaktu > 14) { 
    zonaWaktu = 7; 
  }
  
  // Terapkan zona waktu ke NTP (1 jam = 3600 detik)
  configTime(zonaWaktu * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print(F("[-] Sistem waktu disinkronkan ke zona: UTC"));
  if (zonaWaktu >= 0) Serial.print(F("+"));
  Serial.println(zonaWaktu);

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

// 1. Baca Jarak Ultrasonik
  long jarak_cm = bacaJarakUltrasonik();

  // Hanya hitung persentase jika ultrasonik berhasil membaca (bukan -1)
  if (jarak_cm != -1.0) {
    float sisa_pakan = TINGGI_WADAH_CM - jarak_cm;
    int persentase = (sisa_pakan / TINGGI_WADAH_CM) * 100;
    
    if (persentase < 0) persentase = 0;
    if (persentase > 100) persentase = 100;
    
    Blynk.virtualWrite(V0, persentase);
  } else {
    Serial.println(F("[ERROR] Ultrasonik gagal merespons!"));
  }

  // Kirim nilai persentase ke Blynk (V0)
  Blynk.virtualWrite(V0, persentase);

  // 2. Baca Berat
    float berat_gram = bacaBeratLoadCell();
    
    // Pastikan sensor tidak error sebelum mengirim data ke Blynk
    if (berat_gram != -1.0) {
      // Kirim data dengan presisi 1 angka di belakang koma, hapus (int)
      Blynk.virtualWrite(V1, berat_gram); 
    }

  // --- ANTI-SPAM LOG SERIAL ---
    static int modeSebelumnya = -1;
    if (modeOperasi != modeSebelumnya) {
      Serial.print(F("[MODE] Aktif: "));
      if (modeOperasi == MODE_MANUAL) Serial.println(F("MANUAL"));
      else if (modeOperasi == MODE_SMART) Serial.println(F("SMART"));
      else if (modeOperasi == MODE_SCHEDULE) Serial.println(F("SCHEDULE"));
      modeSebelumnya = modeOperasi; // Simpan status terakhir
    }

  // 3. Logika Mode
  switch (modeOperasi) {
    case MODE_MANUAL:
      Blynk.virtualWrite(V2, "Mode Manual");
      break;

    case MODE_SMART:
      if (berat_gram < BATAS_BERAT_MINIMUM) {
        
        // PENGAMAN 1: Cek isi tabung. Jangan buka servo jika tabung habis!
        if (persentase <= 5) { 
          Blynk.virtualWrite(V2, "Tabung Habis!");
          // BLYNK_LOG_EVENT("pakan_habis"); // Bisa diaktifkan nanti kalau sudah buat event di Web
        } 
        // PENGAMAN 2: Jeda waktu (Cooldown) agar tidak overfeeding
        else {
          static unsigned long waktuTerakhirSmart = 0;
          unsigned long jeda_ms = 30 * 60 * 1000UL; // Jeda 30 menit (dalam milidetik)

          // Cek apakah sudah lewat 30 menit dari pemberian terakhir (atau baru pertama kali nyala)
          if (millis() - waktuTerakhirSmart >= jeda_ms || waktuTerakhirSmart == 0) {
            Serial.println(F("[TRIGGER SMART] Wadah kosong, mengisi ulang..."));
            beriPakan();
            waktuTerakhirSmart = millis(); // Catat waktu pemberian pakan terakhir
          } else {
            Blynk.virtualWrite(V2, "Jeda Waktu");
          }
        }
      } else {
        Blynk.virtualWrite(V2, "Pakan Cukup");
      }
      break;

    case MODE_SCHEDULE:
      Blynk.virtualWrite(V2, "Standby Jadwal");
      break;
  }
}

// ------------------------------------------------------------
// LOGIKA MODE SCHEDULE (WAKTU NTP)
// ------------------------------------------------------------
void cekJadwalMakan() {
  // 1. JANGAN LAKUKAN APA-APA JIKA WAKTU BELUM SINKRON
  if (!waktuValid()) return; 

  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  
  // 2. LOGIKA RESET JADWAL YANG LEBIH AMAN (Berdasarkan Pergantian Hari)
  static int hariTerakhir = -1;
  if (hariTerakhir != -1 && p_tm->tm_yday != hariTerakhir) {
    // Hari berganti, reset semua memori jadwal
    for (int i = 0; i < MAKSIMAL_JADWAL; i++) {
      sudahMakan[i] = false;
    }
    Serial.println(F("[JADWAL] Hari berganti, memori jadwal di-reset."));
  }
  hariTerakhir = p_tm->tm_yday; // Simpan memori hari ini

  // 3. (Lanjutkan dengan logika loop for pengecekan jam & menit Anda...)
}

// ------------------------------------------------------------
// EKSEKUSI KATUP SERVO
// ------------------------------------------------------------
void beriMakan() {
  Serial.println(F("[-] Membuka katup pakan..."));
  myServo.attach(2);
  myServo.write(90);
  delay(2000);
  myServo.write(0);
  delay(500);
  myServo.detach();
}

// ------------------------------------------------------------
// VIRTUAL PIN HANDLER (BLYNK)
// ------------------------------------------------------------

BLYNK_CONNECTED() {
  Serial.println(F("[NETWORK] Berhasil terhubung ke WiFi dan Server Blynk!"));
  Blynk.virtualWrite(V5, zonaWaktu);
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

// V5: Pengaturan Zona Waktu
BLYNK_WRITE(V5) {
  zonaWaktu = param.asInt();
  
  // Terapkan langsung ke sistem NTP
  configTime(zonaWaktu * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  // Simpan ke EEPROM agar permanen
  EEPROM.put(EEPROM_ADDR_TZ, zonaWaktu);
  EEPROM.commit();
  
  Serial.print(F("[CONFIG] Zona Waktu berhasil diubah ke: UTC"));
  if (zonaWaktu >= 0) Serial.print(F("+"));
  Serial.println(zonaWaktu);
}

// V6: Pemilihan Mode oleh Pengguna
BLYNK_WRITE(V6) {
  modeOperasi = param.asInt();
  Serial.print(F("[CONFIG] Mode berganti ke: "));
  if (modeOperasi == MODE_MANUAL) Serial.println(F("MANUAL"));
  else if (modeOperasi == MODE_SMART) Serial.println(F("SMART (Load Cell)"));
  else if (modeOperasi == MODE_SCHEDULE) Serial.println(F("SCHEDULE (NTP)"));
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
float bacaJarakUltrasonik() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  
  // Beri timeout 30.000 mikrodetik (30ms)
  long durasi = pulseIn(PIN_ECHO, HIGH, 30000); 
  
  if (durasi == 0) return -1.0; // Beri sinyal error -1 jika gagal baca
  
  return durasi * 0.0343 / 2.0; // Kembalikan nilai jarak dalam float (presisi tinggi)
}

float bacaBeratLoadCell() {
  // Tunggu maksimal 1000 milidetik (1 detik) agar alat tidak hang
  if (scale.wait_ready_timeout(1000)) { 
    return scale.get_units(5); // Ambil rata-rata 5 pembacaan
  } else {
    Serial.println(F("[ERROR] HX711 tidak merespons! (Kabel longgar/putus)"));
    return -1.0; // Kembalikan nilai -1.0 sebagai tanda error
  }
}