#ifndef CONFIG_H
#define CONFIG_H

/* =================================================================
 * KONFIGURASI SMART PET FEEDER
 * Ubah nilai-nilai di bawah ini sesuai dengan spesifikasi alat Anda
 * ================================================================= */

// ------------------------------------------------------------
// 1. PEMETAAN PIN GPIO ESP8266 (NodeMCU)
// ------------------------------------------------------------
#define PIN_TRIG        13  // D7 - Trigger sensor ultrasonik
#define PIN_ECHO        15  // D8 - Echo sensor ultrasonik (wajib pembagi tegangan 5V -> 3.3V)
#define PIN_HX711_DT    14  // D5 - Data (DT) load cell HX711
#define PIN_HX711_SCK   12  // D6 - Clock (SCK) load cell HX711
#define PIN_SERVO       2   // D4 - Data servo

// ------------------------------------------------------------
// 2. DIMENSI FISIK & SERVO
// ------------------------------------------------------------
const float TINGGI_WADAH_CM          = 20.0;  // Jarak sensor ultrasonik ke dasar tabung kosong (cm)
const float JARAK_MINIMAL_TABUNG_CM  = 4.0;   // Blind spot sensor ultrasonik (jarak <= 4 cm dianggap 100% penuh)
const int   SERVO_TUTUP              = 0;     // Sudut servo saat katup tertutup
const int   SERVO_BUKA               = 90;    // Sudut servo saat katup terbuka
const unsigned long JEDA_GERAK_SERVO = 500UL; // Waktu tunggu servo berputar (ms)

// ------------------------------------------------------------
// 3. PARAMETER PAKAN & LOAD CELL
// ------------------------------------------------------------
const float DEFAULT_CALIBRATION      = 392.0; // Faktor kalibrasi HX711
const float BATAS_BERAT_MINIMUM      = 20.0;  // Smart Mode aktif jika berat piring di bawah nilai ini (g)
constexpr float TARGET_PORSI_GRAM      = 55.0;  // Katup menutup saat pertambahan berat mencapai target (g)
constexpr float MARGIN_JATUHAN_GRAM     = 5.0;   // Tutup lebih awal karena pakan masih jatuh (g)
const float BATAS_PIRING_PENUH_GRAM  = 200.0; // Semua mode menolak buka katup jika piring >= nilai ini (g)
const float MINIMAL_PORSI_VALID_GRAM = 5.0;   // Pertambahan < nilai ini setelah menuang = pakan macet (g)
const int   BATAS_TABUNG_HABIS_PERSEN = 5;    // Semua mode menolak buka katup jika sisa tabung <= nilai ini (%)

// ------------------------------------------------------------
// 4. BATASAN SISTEM (KEAMANAN & ANTI-SPAM)
// ------------------------------------------------------------
const unsigned long MAKSIMAL_WAKTU_BUKA      = 5000UL;  // Batas keras katup terbuka (ms)
const unsigned long DURASI_BUKA_CADANGAN_MS  = 2000UL;  // Dipakai HANYA jika load cell error (ms)
const unsigned long INTERVAL_PANTAU_PORSI_MS = 150UL;   // Frekuensi cek berat saat katup terbuka (ms)
const unsigned long INTERVAL_PENGAWASAN_MS   = 4000UL;  // Frekuensi sensor + telemetri (ms)
const unsigned long INTERVAL_JADWAL_MS       = 15000UL; // Frekuensi cek jadwal makan (ms)
const unsigned long JEDA_SMART_MS            = 30UL * 60UL * 1000UL; // Cooldown Smart Mode (30 menit)
const unsigned long JEDA_MANUAL_MS           = 10UL * 1000UL;        // Cooldown tombol Manual (10 detik)
const int           MAKS_SMART_PER_HARI      = 6;                    // Batas pemberian Smart Mode per hari

// Smart Mode menunggu sinkron NTP selama ini setelah boot. Lewat itu, Smart Mode jalan tanpa jam.
const unsigned long WAKTU_TUNGGU_NTP_MS = 2UL * 60UL * 1000UL;

// ------------------------------------------------------------
// 5. WAKTU
// ------------------------------------------------------------
const int ZONA_WAKTU_DEFAULT_MENIT = 7 * 60; // WIB = UTC+7 (menit dari UTC)

// ------------------------------------------------------------
// Pemeriksaan konfigurasi saat kompilasi
// ------------------------------------------------------------
static_assert(DURASI_BUKA_CADANGAN_MS < MAKSIMAL_WAKTU_BUKA, "DURASI_BUKA_CADANGAN_MS harus < MAKSIMAL_WAKTU_BUKA");
static_assert(TARGET_PORSI_GRAM > MARGIN_JATUHAN_GRAM,       "TARGET_PORSI_GRAM harus > MARGIN_JATUHAN_GRAM");

#endif
