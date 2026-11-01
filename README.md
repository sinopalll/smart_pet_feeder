# Smart Pet Feeder IoT (NodeMCU ESP8266 + Blynk)

Sistem pemberi pakan hewan peliharaan otomatis berbasis **NodeMCU V3 CH340 (ESP8266)** yang terintegrasi dengan **Blynk IoT**. Dirancang untuk mempermudah pemantauan dan pemberian pakan jarak jauh dengan tiga mode operasi: *Manual*, *Smart* (berbasis berat piring), dan *Schedule* (berbasis jadwal waktu).

## Daftar Isi

- [Fitur Utama](#fitur-utama)
- [Komponen Perangkat Keras](#komponen-perangkat-keras)
- [Pemetaan Pin (Wiring)](#pemetaan-pin-wiring)
- [Struktur File Project](#struktur-file-project)
- [Library yang Dibutuhkan](#library-yang-dibutuhkan)
- [Instalasi & Konfigurasi](#instalasi--konfigurasi)
- [Kalibrasi Load Cell](#kalibrasi-load-cell)
- [Konfigurasi Blynk IoT](#konfigurasi-blynk-iot)
- [Cara Penggunaan](#cara-penggunaan)
- [File Pendukung](#file-pendukung)
- [Troubleshooting](#troubleshooting)
- [Catatan Penting](#catatan-penting)

## Fitur Utama

- **Tiga Mode Operasi**
  - **Manual (V6 = 1)** – Memberi pakan langsung melalui tombol di aplikasi Blynk dengan jeda 10 detik antar penekanan.
  - **Smart (V6 = 2)** – Otomatis memberi pakan jika berat di piring kurang dari batas minimum (default `20 gram`). Dilengkapi *cooldown* 30 menit, batas maksimal 6 kali per hari, serta reset hitungan harian berdasarkan waktu lokal.
  - **Schedule (V6 = 3)** – Memberi pakan otomatis berdasarkan jadwal waktu 24-jam yang bisa diatur lewat aplikasi Blynk (maksimal 5 jadwal dalam satu hari).
- **Pemberian Pakan Berbasis Berat** – Katup servo akan terbuka dan menutup secara otomatis berdasarkan pertambahan berat di piring (`TARGET_PORSI_GRAM - MARGIN_JATUHAN_GRAM`), bukan durasi tetap.
- **Multi-Jadwal Dinamis** – Atur jadwal dengan format `HH:MM, HH:MM, ...` langsung dari kolom input di Blynk.
- **Kalibrasi Permanen di EEPROM** – Nilai *offset* Load Cell (titik 0) tersimpan di EEPROM, sehingga tidak perlu dikalibrasi ulang saat mati listrik.
- **Pengaman Sistem** – Proteksi tabung hampir habis, piring sudah penuh, deteksi pakan macet, *timeout* katup (batas keras 5 detik), serta *fallback* durasi jika Load Cell tidak merespons saat katup terbuka.
- **Sinkronisasi Waktu (NTP)** – Menggunakan `pool.ntp.org` & `time.nist.gov` dengan offset zona waktu manual (default `UTC+7 / WIB`).
- **Non-Blocking** – Seluruh proses (WiFi, Blynk, servo, timer) dirancang agar tidak saling memblokir, sehingga penutupan katup tetap aman.
- **Telemetri Real-Time** – Menampilkan sisa pakan di tabung (%), berat piring (gram), status sistem, mode aktif, serta kekuatan sinyal WiFi (RSSI).

## Komponen Perangkat Keras

| Komponen | Keterangan |
| --- | --- |
| **Mikrokontroler** | NodeMCU V3 CH340 (ESP8266) |
| **Sensor Jarak** | HC-SR04 Ultrasonic Sensor (untuk menghitung sisa pakan di tabung) |
| **Sensor Berat** | Load Cell + HX711 Module (untuk mengukur berat pakan di piring) |
| **Aktuator** | Servo Motor SG90 (untuk membuka/menutup katup keluar pakan) |
| **Power Supply** | Rekomendasi **5V eksternal** untuk Servo SG90 (untuk mencegah *brownout* pada ESP8266) |

## Pemetaan Pin (Wiring)

| Komponen | Pin Modul | Pin NodeMCU (ESP8266) | Keterangan |
| --- | --- | --- | --- |
| **HC-SR04** | TRIG | `D7` (GPIO 13) | Trigger |
| **HC-SR04** | ECHO | `D8` (GPIO 15) | Echo |
| **HX711** | DT | `D5` (GPIO 14) | Data |
| **HX711** | SCK | `D6` (GPIO 12) | Clock |
| **Servo SG90** | Signal (PWM) | `D4` (GPIO 2) | Kontrol katup |

> **Penting:** Pastikan VCC/GND Servo terhubung ke sumber 5V eksternal dengan ground yang sama dengan NodeMCU.

## Struktur File Project

```text
smart_pet_feeder/
├── smart_pet_feeder.ino   # Sketsa utama (logika mode, Blynk, timer, EEPROM, servo)
├── config.h               # Konfigurasi pin, threshold, timing, kalibrasi & parameter sistem
├── secrets.h              # Kredensial WiFi & Blynk Auth Token
├── README.md              # Dokumentasi project
├── test_load_cell/        # Helper untuk mendapatkan nilai raw HX711 saat kalibrasi
└── reset_EEPROM/          # Utility untuk menghapus seluruh isi EEPROM
```

## Library yang Dibutuhkan

Install melalui **Arduino IDE > Sketch > Include Library > Manage Libraries**:

| Library | Author | Kegunaan |
| --- | --- | --- |
| [`Blynk`](https://docs.blynk.io/) | Volodymyr Shymanskyy | Koneksi & komunikasi dengan aplikasi Blynk IoT |
| [`HX711 Arduino Library`](https://github.com/bogde/HX711) | Bogdan Necula | Pembacaan nilai dari modul HX711 |
| [`Servo`](https://www.arduino.cc/reference/en/libraries/servo/) | Arduino | Pengendalian sudut Servo SG90 |

> `ESP8266WiFi`, `EEPROM`, `time.h` sudah tersedia secara bawaan saat menggunakan **ESP8266 Arduino Core**.

## Instalasi & Konfigurasi

### 1. Persiapan Environment

1. Instal [Arduino IDE](https://www.arduino.cc/en/software).
2. Tambahkan **ESP8266 Board Package** melalui `File > Preferences > Additional Boards Manager URLs` dengan: 

   ```text
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Install `esp8266` core melalui `Tools > Board > Boards Manager`.
4. Pilih board: `Tools > Board > ESP8266 Boards > NodeMCU 1.0 (ESP-12E Module)`.
5. Install library yang dibutuhkan seperti pada tabel di atas.

### 2. Konfigurasi Kredensial

Buka file `secrets.h` dan sesuaikan dengan milik kamu:

```cpp
#define SECRET_BLYNK_TOKEN "YOUR_BLYNK_AUTH_TOKEN"
#define SECRET_WIFI_SSID   "YOUR_WIFI_SSID"
#define SECRET_WIFI_PASS   "YOUR_WIFI_PASSWORD"
```

Pada `smart_pet_feeder.ino`, nilai Blynk diambil dari `secrets.h`:

```cpp
#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "smart pet feeder"
#define BLYNK_AUTH_TOKEN    SECRET_BLYNK_TOKEN
```

> **Catatan:** File `secrets.h` saat ini berisi nilai contoh. Sangat disarankan untuk **tidak mempublikasikan file ini** jika repo di-fork ke repository publik.

### 3. Upload Firmware

1. Buka `smart_pet_feeder.ino` di Arduino IDE.
2. Pastikan port COM/Serial sudah terpilih sesuai NodeMCU kamu.
3. Klik `Upload`.
4. Buka Serial Monitor (`115200 baud`) untuk melihat log inisialisasi sistem.

## Kalibrasi Load Cell

Agar pembacaan berat akurat, lakukan kalibrasi berikut:

### Langkah 1: Dapatkan nilai raw

1. Buka sketch `test_load_cell/test_load_cell.ino`
2. Upload ke NodeMCU
3. Buka Serial Monitor dengan baudrate `115200`
4. Letakkan **Load Cell tanpa beban apapun**. Amati nilai rata-rata yang muncul
5. Catat nilai tersebut

### Langkah 2: Atur faktor kalibrasi

1. Buka `config.h`
2. Ubah nilai `DEFAULT_CALIBRATION` sesuai hasil kalibrasi alat kamu:

   ```cpp
   const float DEFAULT_CALIBRATION = 392.0; // Sesuaikan dengan hasil kalibrasi hardware
   ```
3. Upload kembali `smart_pet_feeder.ino`

### Langkah 3: Tare (Zeroing)

1. Pastikan piring dalam keadaan **benar-benar kosong**.
2. Tekan tombol **Tare (V7)** pada aplikasi Blynk.
3. Nilai *offset* akan otomatis tersimpan ke EEPROM pada alamat `EEPROM_ADDR_OFFSET`.
4. Setelah itu, pembacaan berat akan dimulai dari `0 gram`.

## Konfigurasi Blynk IoT

Buat **Template** dan **Datastream** pada [Blynk Web Console](https://blynk.cloud/) sesuai tabel berikut:

| Virtual Pin | Nama Datastream | Tipe | Keterangan |
| --- | --- | --- | --- |
| **V0** | Sisa Pakan (Tabung) | Integer/Double | Persentase sisa pakan di dalam tabung berdasarkan pembacaan HC-SR04 |
| **V1** | Berat Pakan (Piring) | Integer/Double | Berat aktual pakan yang ada di piring (gram) |
| **V2** | Status Sistem | String | Menampilkan status seperti `Mode Manual`, `Mengisi Pakan...`, `Selesai Mengisi (+X g)`, `Tabung Habis!`, `Piring Penuh`, dll. |
| **V3** | Tombol Manual Feed | Integer (Push) | Tombol untuk memberi pakan secara manual (hanya aktif di Mode Manual) |
| **V4** | Input Jadwal | String | Input jadwal dengan format `HH:MM, HH:MM, ...` (maks. 5 jadwal) |
| **V5** | Zona Waktu (Jam) | Double | Atur offset waktu dalam jam (contoh: `7` untuk WIB, `5.5` untuk IST). Bisa bernilai pecahan. |
| **V6** | Pemilihan Mode | Integer (Select/Dropdown) | Pilih mode: `1` = Manual, `2` = Smart, `3` = Schedule |
| **V7** | Tare / Kalibrasi Nol | Integer (Push) | Tekan saat piring kosong untuk menyimpan titik nol ke EEPROM |
| **V9** | Kekuatan Sinyal WiFi | Integer (Gauge) | Menampilkan nilai RSSI dalam dBm (`-100` s/d `0`) |

## Cara Penggunaan

### Mode Manual
1. Set `V6` ke mode **Manual (1)**.
2. Tekan tombol **Manual Feed (V3)** untuk mengeluarkan pakan.
3. Terdapat jeda **10 detik** antar penekanan untuk mencegah *spamming* tombol.

### Mode Smart
1. Set `V6` ke mode **Smart (2)**.
2. Sistem akan memantau berat piring tiap 4 detik.
3. Jika berat piring < `20 gram`, tabung tidak kosong, dan syarat lain terpenuhi, katup akan terbuka secara otomatis.
4. Sistem menerapkan *cooldown* **30 menit**, maksimal **6 kali** pemberian per hari, serta menunggu sinkronisasi NTP saat baru dinyalakan.

### Mode Schedule
1. Set `V6` ke mode **Schedule (3)**.
2. Masukkan jadwal pada `V4` dengan format `HH:MM` dipisahkan koma. Contoh: `07:00, 13:00, 19:00`
3. Sistem akan mengeksekusi pemberian pakan **hanya sekali** per jadwal di hari yang sama.
4. Jika saat jadwal tiba kondisi tidak memungkinkan (sedang memberi, tabung habis, atau piring penuh), sistem akan **mencoba kembali** pada pengecekan berikutnya tanpa menghabiskan kuota jadwal hari itu.

## File Pendukung

| File/Folder | Fungsi |
| --- | --- |
| `test_load_cell/test_load_cell.ino` | Membaca nilai mentah dari HX711 untuk membantu menentukan `DEFAULT_CALIBRATION` di `config.h`. |
| `reset_EEPROM/reset_eeprom/reset_eeprom.ino` | Menghapus seluruh isi EEPROM (menulis `0xFF` ke 512 byte). Berguna saat ingin mereset konfigurasi mode, jadwal, offset, atau struktur EEPROM yang bermasalah. |

> Untuk mereset EEPROM, upload sketch `reset_eeprom.ino` terpisah, tunggu pesan `"EEPROM berhasil dibersihkan!"` di Serial Monitor, lalu upload kembali `smart_pet_feeder.ino`.

## Troubleshooting

| Masalah | Kemungkinan Penyebab | Solusi |
| --- | --- | --- |
| **HX711 tidak merespons / berat `NaN`** | Kabel longgar, wiring salah, atau beban tidak stabil | Periksa koneksi DT/SCK, pastikan kabel tidak putus, dan jalankan `test_load_cell.ino` untuk verifikasi. |
| **Servo bergetar / ESP8266 restart saat buka katup** | Tegangan drop saat servo bergerak | **Wajib** gunakan sumber 5V eksternal untuk servo dengan *common ground* ke NodeMCU. |
| **Jadwal tidak berjalan** | Belum tersinkron NTP / WiFi terputus | Pastikan koneksi internet stabil. Tunggu beberapa saat hingga waktu tersinkron (cek log Serial). Hindari WiFi dengan *captive portal*. |
| **Smart Mode tidak memberi pakan** | Berat piring >= 20g, tabung habis, jeda 30 menit, atau sudah 6x/hari | Periksa berat di V1, persentase tabung di V0, serta status di V2. Hitungan harian akan reset otomatis saat berganti hari. |
| **Blynk sering putus** | Sinyal WiFi lemah (`RSSI` mendekati -90 dBm) | Dekatkan alat ke router, gunakan repeater, atau pindah ke jaringan 2.4GHz yang lebih stabil. |
| **"Pakan Macet?" muncul setelah mengisi** | Aliran pakan tersumbat atau keluar terlalu sedikit | Bersihkan saluran katup/wadah keluar pakan. Pastikan tidak ada pakan yang menggumpal di sekitar mekanisme servo. |
| **Setelah flash/reset konfigurasi aneh** | Struktur config berubah / EEPROM lama tidak cocok | Gunakan `reset_EEPROM/reset_eeprom/reset_eeprom.ino` untuk menghapus EEPROM, lalu upload `smart_pet_feeder.ino` kembali. |

## Catatan Penting

- **Kabel Power:** Gunakan kabel USB yang cukup kuat, atau lebih disarankan adaptor 5V terpisah untuk beban servo. Hindari power murni dari USB PC jika servo kaku saat beroperasi.
- **Jaringan WiFi:** **Hindari** WiFi publik seperti kampus, hotel, atau kafe yang menggunakan *captive portal* (halaman login web). Gunakan WiFi rumah/hotspot pribadi.
- **Ukuran Tabung:** Sesuaikan nilai `TINGGI_WADAH_CM` di `config.h` dengan jarak aktual antara sensor HC-SR04 dan dasar tabung saat kondisi **kosong**. Kesalahan nilai ini akan membuat persentase sisa pakan di V0 kurang akurat.
- **Keamanan Kredensial:** Jangan upload `secrets.h` dengan token asli ke repository publik. Pertimbangkan untuk menambahkan `secrets.h` ke `.gitignore` jika kamu berencana mem-push project ini ke GitHub.
- **EEPROM:** Layout EEPROM bersifat tetap (`magic = 0xFEED0A03`). Jika struktur `struct Konfig` diubah, sistem akan otomatis melakukan *reset* konfigurasi untuk menghindari pembacaan data yang tidak valid.
- **Zona Waktu:** `configTime` diset ke UTC, sehingga offset wajib diatur melalui `V5` agar jadwal berjalan sesuai waktu yang diinginkan.
