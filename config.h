#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * KONFIGURASI SMART PET FEEDER
 * Ubah nilai-nilai di bawah ini sesuai dengan spesifikasi alat Anda
 * ================================================================= */

// ------------------------------------------------------------
// 1. PEMETAAN PIN GPIO ESP8266 (NodeMCU)
// ------------------------------------------------------------
#define PIN_TRIG        13  // D7 - Pin Trigger Sensor Ultrasonik
#define PIN_ECHO        15  // D8 - Pin Echo Sensor Ultrasonik
#define PIN_HX711_DT    14  // D5 - Pin Data (DT) Load Cell HX711
#define PIN_HX711_SCK   12  // D6 - Pin Clock (SCK) Load Cell HX711
#define PIN_SERVO       2   // D4 - Pin Data Motor Servo

// ------------------------------------------------------------
// 2. DIMENSI FISIK & SERVO
// ------------------------------------------------------------
const float TINGGI_WADAH_CM     = 20.0;  // Jarak dari sensor ultrasonik ke dasar tabung kosong (cm)
const int   SERVO_TUTUP         = 0;     // Sudut servo saat katup menutup rapat
const int   SERVO_BUKA          = 90;    // Sudut servo saat katup terbuka untuk menuang pakan
const unsigned long JEDA_GERAK_SERVO = 500UL; // Waktu tunggu mekanis servo berputar (milidetik)

// ------------------------------------------------------------
// 3. PARAMETER PAKAN & LOAD CELL
// ------------------------------------------------------------
const float DEFAULT_CALIBRATION = 392.0; // Faktor kalibrasi HX711 (cari dengan sketch test_load_cell)
const float BATAS_BERAT_MINIMUM = 20.0;  // Smart Mode aktif jika berat piring di bawah nilai ini (gram)
const float TARGET_PORSI_GRAM   = 55.0;  // Katup menutup saat pertambahan berat piring mencapai target ini (gram)
const float MARGIN_JATUHAN_GRAM = 5.0;   // Katup ditutup sedikit lebih awal karena pakan masih jatuh (gram)
const float BATAS_PIRING_PENUH_GRAM = 200.0; // Semua mode menolak membuka katup jika piring sudah >= nilai ini (gram)
const float MINIMAL_PORSI_VALID_GRAM = 5.0;  // Jika pertambahan berat < nilai ini setelah menuang, dianggap pakan macet
const int   BATAS_TABUNG_HABIS_PERSEN = 5;   // Semua mode menolak membuka katup jika sisa tabung <= nilai ini (%)

// ------------------------------------------------------------
// 4. BATASAN SISTEM (KEAMANAN & ANTI-SPAM)
// ------------------------------------------------------------
const unsigned long MAKSIMAL_WAKTU_BUKA     = 5000UL;  // Batas keras katup terbuka jika target porsi tak tercapai (5 detik)
const unsigned long DURASI_BUKA_CADANGAN_MS = 2000UL;  // Dipakai HANYA jika load cell error: buka selama durasi ini (harus < MAKSIMAL_WAKTU_BUKA)
const unsigned long INTERVAL_PANTAU_PORSI_MS = 150UL;  // Seberapa sering berat dicek saat katup terbuka
const unsigned long JEDA_SMART_MS     = 30UL * 60UL * 1000UL; // Jeda cooldown Smart Mode (30 menit)
const int   MAKS_SMART_PER_HARI       = 6;                    // Batas maksimal Smart Mode memberi makan per hari
const unsigned long JEDA_MANUAL_MS    = 10UL * 1000UL;        // Jeda cooldown tombol Manual (10 detik)

// Smart Mode menunggu sinkron NTP sebanyak ini setelah boot (agar cooldown & batas harian
// yang tersimpan bisa diperiksa). Lewat dari itu, Smart Mode jalan tanpa jam (offline).
const unsigned long WAKTU_TUNGGU_NTP_MS = 2UL * 60UL * 1000UL;

// ------------------------------------------------------------
// 5. WAKTU
// ------------------------------------------------------------
const int ZONA_WAKTU_DEFAULT_MENIT = 7 * 60; // WIB = UTC+7 (dalam menit dari UTC)

#endif
