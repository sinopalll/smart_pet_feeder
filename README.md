# Smart Pet Feeder IoT (NodeMCU ESP8266 + Blynk)

Sistem pemberi pakan hewan peliharaan otomatis berbasis **NodeMCU V3 CH340 (ESP8266)** yang terintegrasi dengan **Blynk IoT**. Dirancang untuk pemantauan dan pemberian pakan jarak jauh dengan tiga mode operasi: *Manual*, *Smart* (berbasis berat piring), dan *Schedule* (berbasis jadwal waktu).

---

## Daftar Isi

- [Fitur Utama](#fitur-utama)
- [Komponen Hardware](#komponen-hardware)
- [Wiring & Pin Mapping](#wiring--pin-mapping)
- [Struktur Direktori](#struktur-direktori)
- [Persiapan Environment PlatformIO](#persiapan-environment-platformio)
- [Instalasi Dependensi](#instalasi-dependensi)
- [Konfigurasi Wi-Fi & Blynk](#konfigurasi-wi-fi--blynk)
- [Build & Upload Firmware](#build--upload-firmware)
- [Kalibrasi Load Cell](#kalibrasi-load-cell)
- [Reset EEPROM](#reset-eeprom)
- [Konfigurasi Blynk IoT](#konfigurasi-blynk-iot)
- [Cara Penggunaan](#cara-penggunaan)
- [Troubleshooting](#troubleshooting)
- [Catatan Penting](#catatan-penting)

---

## Fitur Utama

- **Tiga Mode Operasi**
  - **Manual (V6 = 1)** – Memberi pakan langsung melalui tombol di aplikasi Blynk dengan jeda 10 detik antar penekanan.
  - **Smart (V6 = 2)** – Otomatis memberi pakan jika berat piring < batas minimum (default `20 gram`). Dilengkapi *cooldown* 30 menit, batas 6 kali per hari, dan reset harian berdasarkan waktu lokal.
  - **Schedule (V6 = 3)** – Memberi pakan otomatis sesuai jadwal 24 jam yang dapat diatur dari aplikasi Blynk (maksimal 5 jadwal per hari).
- **Pemberian Pakan Berbasis Berat** – Katup servo terbuka dan menutup otomatis berdasarkan pertambahan berat (`TARGET_PORSI_GRAM - MARGIN_JATUHAN_GRAM`), bukan durasi tetap.
- **Multi-Jadwal Dinamis** – Atur jadwal format `HH:MM, HH:MM, ...` langsung dari Blynk.
- **Kalibrasi Permanen di EEPROM** – Offset Load Cell disimpan di EEPROM, tidak perlu kalibrasi ulang saat restart.
- **Pengaman Sistem** – Proteksi tabung hampir habis, piring sudah penuh, deteksi pakan macet, *timeout* katup (batas keras 5 detik), dan *fallback* durasi jika Load Cell tidak merespons.
- **Sinkronisasi Waktu NTP** – Menggunakan `pool.ntp.org` & `time.nist.gov` dengan offset zona waktu manual (default `UTC+7 / WIB`).
- **Non-Blocking** – Seluruh proses (Wi-Fi, Blynk, servo, timer) dirancang agar tidak saling memblokir.
- **Telemetri Real-Time** – Sisa pakan tabung (%), berat piring (gram), status sistem, mode aktif, dan kekuatan sinyal Wi-Fi (RSSI).

---

## Komponen Hardware

| Komponen | Keterangan |
|---|---|
| **Mikrokontroler** | NodeMCU V3 CH340 (ESP8266) |
| **Sensor Jarak** | HC-SR04 Ultrasonic — mengukur sisa pakan di tabung |
| **Sensor Berat** | Load Cell + Modul HX711 — mengukur berat pakan di piring |
| **Aktuator** | Servo SG90 — membuka/menutup katup pakan |
| **Power Supply** | 5V eksternal untuk Servo SG90 (mencegah *brownout* ESP8266) |

---

## Wiring & Pin Mapping

Pin dikonfigurasi di [`include/config.h`](include/config.h).

| Komponen | Pin Modul | Pin NodeMCU | GPIO | Keterangan |
|---|---|---|---|---|
| **HC-SR04** | TRIG | `D7` | GPIO 13 | Trigger ultrasonik |
| **HC-SR04** | ECHO | `D8` | GPIO 15 | Echo ultrasonik — **wajib pembagi tegangan 5V → 3.3V** |
| **HX711** | DT | `D5` | GPIO 14 | Data load cell |
| **HX711** | SCK | `D6` | GPIO 12 | Clock load cell |
| **Servo SG90** | Signal | `D4` | GPIO 2 | PWM kontrol katup |

> **Penting:** Hubungkan VCC/GND Servo ke sumber 5V eksternal dengan *common ground* ke NodeMCU. Jangan mengandalkan pin 5V dari USB NodeMCU.

---

## Struktur Direktori

```text
smart_pet_feeder/
├── include/
│   ├── config.h          # Pin, konstanta fisik, threshold, timing sistem
│   └── secrets.h         # Kredensial Wi-Fi & Blynk (tidak masuk Git)
├── src/
│   ├── main.cpp          # Firmware utama: mode, Blynk, timer, EEPROM, servo
│   ├── reset_eeprom/
│   │   └── reset_eeprom.cpp   # Utilitas: hapus seluruh EEPROM
│   └── test_load_cell/
│       └── test_load_cell.cpp # Utilitas: baca nilai raw HX711 untuk kalibrasi
├── platformio.ini        # Konfigurasi multi-environment PlatformIO
└── README.md             # Dokumentasi proyek
```

---

## Persiapan Environment PlatformIO

### Prasyarat

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install ekstensi **PlatformIO IDE** dari marketplace VS Code.
3. Atau install PlatformIO Core (CLI) secara langsung:

   ```bash
   pip install platformio
   ```

4. Buka folder proyek di VS Code atau navigasi ke direktori proyek via terminal.

### Environment yang Tersedia

Proyek ini memiliki tiga environment terpisah di `platformio.ini`:

| Environment | Kegunaan | Kapan Digunakan |
|---|---|---|
| `nodemcu` *(default)* | Firmware utama Smart Pet Feeder | Upload ke perangkat untuk operasional |
| `test_load_cell` | Kalibrasi HX711 | Sebelum/saat kalibrasi hardware |
| `reset_eeprom` | Hapus konfigurasi EEPROM | Saat reset konfigurasi atau pergantian hardware |

---

## Instalasi Dependensi

PlatformIO mengunduh dependensi secara otomatis saat build pertama kali. Dependensi per environment:

| Environment | Library | Versi |
|---|---|---|
| `nodemcu` | `bogde/HX711` | `^0.7.5` |
| `nodemcu` | `blynkkk/Blynk` | `^1.3.2` |
| `test_load_cell` | `bogde/HX711` | `^0.7.5` |
| `reset_eeprom` | *(built-in EEPROM)* | — |

Library bawaan ESP8266 yang sudah tersedia: `ESP8266WiFi`, `EEPROM`, `Servo`, `time.h`.

Untuk mengunduh dependensi secara manual tanpa build:

```bash
pio pkg install -e nodemcu
pio pkg install -e test_load_cell
```

---

## Konfigurasi Wi-Fi & Blynk

Buat file `include/secrets.h` dengan isi berikut (file ini **tidak masuk Git**):

```cpp
#pragma once

#define SECRET_BLYNK_TOKEN  "YOUR_BLYNK_AUTH_TOKEN"
#define SECRET_WIFI_SSID    "YOUR_WIFI_SSID"
#define SECRET_WIFI_PASS    "YOUR_WIFI_PASSWORD"
```

Kemudian di `src/main.cpp`, konfigurasikan Template ID dan Template Name sesuai proyek Blynk Anda:

```cpp
#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_TEMPLATE_NAME"
```

> **Catatan:** Jangan pernah menyimpan nilai token, SSID, atau password secara langsung di file yang masuk Git.

---

## Build & Upload Firmware

### Firmware Utama (env: nodemcu)

```bash
# Build
pio run -e nodemcu

# Build & Upload ke perangkat
pio run -e nodemcu --target upload

# Upload & buka Serial Monitor (115200 baud)
pio run -e nodemcu --target upload && pio device monitor
```

### Serial Monitor Saja

```bash
pio device monitor -e nodemcu
```

---

## Kalibrasi Load Cell

Kalibrasi dilakukan satu kali saat pertama kali merakit hardware, atau saat mengganti Load Cell.

### Langkah 1 — Upload program kalibrasi

```bash
pio run -e test_load_cell --target upload
```

### Langkah 2 — Baca nilai raw HX711

```bash
pio device monitor -e test_load_cell
```

- Letakkan Load Cell **tanpa beban apapun**.
- Amati nilai rata-rata yang tercetak di Serial Monitor tiap 1 detik.
- Catat nilai tersebut.

### Langkah 3 — Atur faktor kalibrasi

Buka [`include/config.h`](include/config.h) dan sesuaikan:

```cpp
const float DEFAULT_CALIBRATION = 392.0;  // Ganti dengan nilai dari langkah 2
```

### Langkah 4 — Upload firmware utama kembali

```bash
pio run -e nodemcu --target upload
```

### Langkah 5 — Tare (Zeroing)

1. Pastikan piring dalam keadaan **kosong**.
2. Tekan tombol **Tare (V7)** di aplikasi Blynk.
3. Nilai offset akan tersimpan otomatis ke EEPROM.
4. Pembacaan berat dimulai dari `0 gram`.

---

## Reset EEPROM

Gunakan saat konfigurasi korup, setelah mengubah struktur `Konfig`, atau untuk reset penuh ke kondisi awal.

### Upload program reset

```bash
pio run -e reset_eeprom --target upload
```

### Verifikasi

```bash
pio device monitor -e reset_eeprom
```

Tunggu hingga Serial Monitor menampilkan:
```
EEPROM berhasil dibersihkan!
```

### Upload firmware utama kembali

```bash
pio run -e nodemcu --target upload
```

> **Perhatian:** Reset EEPROM menghapus offset kalibrasi Load Cell, jadwal, mode, dan semua konfigurasi tersimpan. Lakukan tare ulang setelah reset.

---

## Konfigurasi Blynk IoT

Buat **Template** dan **Datastream** di [Blynk Web Console](https://blynk.cloud/) sesuai tabel berikut:

| Virtual Pin | Nama | Tipe Data | Keterangan |
|---|---|---|---|
| **V0** | Sisa Pakan (Tabung) | Integer | Persentase sisa pakan dari pembacaan HC-SR04 (0–100%) |
| **V1** | Berat Pakan (Piring) | Double | Berat aktual pakan di piring (gram) |
| **V2** | Status Sistem | String | Status: `Mode Manual`, `Mengisi Pakan...`, `Selesai Mengisi (+X g)`, `Tabung Habis!`, `Piring Penuh`, dll. |
| **V3** | Tombol Manual Feed | Integer (Push) | Tombol beri pakan manual (aktif hanya di Mode Manual) |
| **V4** | Input Jadwal | String | Format `HH:MM, HH:MM, ...` — maks. 5 jadwal |
| **V5** | Zona Waktu (Jam) | Double | Offset waktu dalam jam (contoh: `7` untuk WIB, `5.5` untuk IST) |
| **V6** | Pilihan Mode | Integer | `1` = Manual, `2` = Smart, `3` = Schedule |
| **V7** | Tare / Kalibrasi Nol | Integer (Push) | Tekan saat piring kosong — simpan titik nol ke EEPROM |
| **V8** | Alert Sensor | String | Peringatan error sensor (ultrasonik/load cell/Wi-Fi lemah) |
| **V9** | Kekuatan Sinyal Wi-Fi | Integer (Gauge) | RSSI dalam dBm (`-100` s/d `0`) |

---

## Cara Penggunaan

### Mode Manual

1. Set **V6** ke `1` (Manual).
2. Tekan tombol **Manual Feed (V3)** untuk memberi pakan.
3. Terdapat jeda **10 detik** antar penekanan untuk mencegah spam.

### Mode Smart

1. Set **V6** ke `2` (Smart).
2. Sistem memantau berat piring setiap 4 detik.
3. Jika berat < `20 gram`, tabung tidak kosong, dan syarat terpenuhi, katup terbuka otomatis.
4. Constraints: *cooldown* **30 menit**, maksimal **6 kali/hari**, menunggu sinkron NTP saat baru dinyalakan.

### Mode Schedule

1. Set **V6** ke `3` (Schedule).
2. Masukkan jadwal di **V4**: format `HH:MM` dipisah koma. Contoh: `07:00, 13:00, 19:00`.
3. Sistem mengeksekusi pemberian pakan **satu kali** per jadwal per hari.
4. Jika saat jadwal kondisi tidak memungkinkan (katup sibuk/tabung habis/piring penuh), sistem **mencoba lagi** pada pengecekan berikutnya tanpa menghabiskan kuota.

### Pengaturan Zona Waktu

Atur offset waktu melalui **V5** (contoh: `7.0` untuk WIB, `7.5` untuk WITA, `8.0` untuk WIT).

---

## Troubleshooting

| Masalah | Kemungkinan Penyebab | Solusi |
|---|---|---|
| **HX711 tidak merespons / berat `NaN`** | Kabel longgar atau wiring salah | Periksa koneksi DT/SCK, jalankan `test_load_cell` untuk verifikasi |
| **Servo bergetar / ESP8266 restart saat buka katup** | Tegangan drop saat servo aktif | Gunakan sumber 5V eksternal untuk servo dengan *common ground* ke NodeMCU |
| **Jadwal tidak berjalan** | NTP belum sinkron / Wi-Fi terputus | Pastikan koneksi internet stabil, tunggu beberapa detik, hindari jaringan *captive portal* |
| **Smart Mode tidak memberi pakan** | Berat ≥ 20 g, tabung habis, jeda 30 menit, atau sudah 6×/hari | Periksa V0 (tabung), V1 (berat), V2 (status) |
| **Blynk sering putus** | Sinyal Wi-Fi lemah (RSSI mendekati -90 dBm) | Dekatkan perangkat ke router atau gunakan repeater Wi-Fi |
| **"Pakan Macet?" setelah mengisi** | Aliran pakan tersumbat | Bersihkan saluran katup/wadah, pastikan pakan tidak menggumpal |
| **Konfigurasi aneh setelah flash** | Struktur EEPROM tidak cocok | Jalankan `reset_eeprom`, lalu upload `nodemcu` kembali |
| **Alert sensor di V8** | Ultrasonik / HX711 tidak merespons | Periksa wiring, pastikan tidak ada komponen longgar |

---

## Catatan Penting

- **Power Servo:** Gunakan adaptor 5V eksternal, bukan pin 5V dari USB NodeMCU. Servo yang mengambil daya dari NodeMCU dapat menyebabkan *brownout* dan restart tiba-tiba.
- **Jaringan Wi-Fi:** Hindari Wi-Fi publik (*captive portal* seperti hotspot kampus/kafe). Gunakan Wi-Fi rumah atau hotspot pribadi.
- **Ukuran Tabung:** Sesuaikan `TINGGI_WADAH_CM` di `include/config.h` dengan jarak aktual antara sensor HC-SR04 dan dasar tabung saat **kosong**. Nilai yang salah mengakibatkan persentase sisa pakan tidak akurat.
- **EEPROM Layout:** Layout EEPROM bersifat tetap (`magic = 0xFEED0A03`). Perubahan pada `struct Konfig` di `main.cpp` akan menyebabkan sistem reset otomatis ke konfigurasi default.
- **Zona Waktu:** `configTime` diset ke UTC. Offset zona waktu wajib diatur melalui **V5** agar jadwal berjalan sesuai waktu lokal.
- **Keamanan Kredensial:** File `include/secrets.h` sudah dikecualikan dari Git. Jangan pernah menuliskan token atau password langsung di file source code yang masuk ke repository.
