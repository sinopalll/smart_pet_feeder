# Smart Pet Feeder IoT - Multi-Mode Controller 🐾

Sistem pemberi pakan hewan peliharaan otomatis berbasis **NodeMCU ESP8266** dan terintegrasi dengan platform **Blynk IoT**. Proyek ini dirancang untuk memudahkan pemantauan dan pemberian pakan hewan secara jarak jauh dengan tiga mode operasi: Manual, Cerdas (berbasis berat), dan Penjadwalan Dinamis.

---

## ✨ Fitur Utama

* **Tiga Mode Operasi (Multi-Mode):**
* **Manual:** Keluarkan pakan kapan saja melalui tombol di aplikasi Blynk.
* **Smart:** Pengisian otomatis jika berat pakan di wadah kurang dari batas minimum (20 gram).
* **Schedule:** Pemberian pakan otomatis berdasarkan waktu yang disinkronisasi langsung dari server NTP (Network Time Protocol).


* **Multi-Jadwal Dinamis:** Mendukung banyak jadwal makan dalam satu hari hanya dengan mengetikkan waktu (contoh: `07:30, 12:00, 18:00`) pada aplikasi.
* **Kalibrasi Memori Permanen (EEPROM):** Alat menyimpan kalibrasi nol (Tare) timbangan ke memori EEPROM. Timbangan tidak akan *error* atau reset ke 0 gram meskipun terjadi mati listrik.
* **Telemetri Lengkap:** Memantau sisa volume pakan di tangki (Ultrasonik), berat wadah pakan (Load Cell), dan kekuatan sinyal WiFi (RSSI dBm) secara *real-time*.

---

## 🛠️ Perangkat Keras (Hardware)

* Mikrokontroler: **NodeMCU V3 CH340 (ESP8266)**
* Sensor Volume: **Ultrasonik HC-SR04**
* Sensor Berat: **Load Cell + Modul Penguat HX711**
* Aktuator Katup: **Motor Servo SG90**
* *Power Supply* eksternal 5V (direkomendasikan) untuk Servo guna mencegah *drop* tegangan pada ESP8266.

### Pemetaan Pin (Wiring)

| Komponen | Pin Modul | Pin ESP8266 (NodeMCU) | Keterangan |
| --- | --- | --- | --- |
| **Ultrasonik (HC-SR04)** | TRIG | **D7 (GPIO 13)** | Sinyal Trigger |
|  | ECHO | **D8 (GPIO 15)** | Sinyal Echo |
| **Load Cell (HX711)** | DT | **D5 (GPIO 14)** | Data |
|  | SCK | **D6 (GPIO 12)** | Clock |
| **Motor Servo** | PWM / Data | **D4 (GPIO 2)** | Sinyal Kontrol Katup |

---

## 📱 Konfigurasi Blynk IoT (Virtual Pins)

Konfigurasikan Datastream pada **Blynk Web Console** sesuai dengan tabel berikut:

| Virtual Pin | Nama Datastream | Tipe Data | Pengaturan Tambahan |
| --- | --- | --- | --- |
| **V0** | Sisa Pakan (Jarak) | Integer / Double | - |
| **V1** | Berat Pakan | Integer / Double | - |
| **V2** | Status Katup | String | - |
| **V3** | Tombol Manual | Integer | Mode *Widget*: PUSH |
| **V4** | Input Jadwal | String | Mode *Widget*: Text Input |
| **V6** | Pemilihan Mode | Integer | 1=Manual, 2=Smart, 3=Schedule |
| **V7** | Kalibrasi / Tare | Integer | Mode *Widget*: PUSH |
| **V9** | Kekuatan Sinyal WiFi | Integer | MIN: `-100`, MAX: `0`, Satuan: `dBm` |

---

## 🚀 Panduan Penggunaan & Instalasi

### 1. Instalasi Kode

1. Buka kode menggunakan **Arduino IDE**.
2. Pastikan pustaka berikut sudah terinstal melalui *Library Manager*:
* `Blynk` (oleh Volodymyr Shymanskyy)
* `HX711 Arduino Library` (oleh Bogdan Necula)
* `Servo`


3. Masukkan `BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME`, dan `BLYNK_AUTH_TOKEN` Anda pada tiga baris paling atas kode.
4. Sesuaikan `ssid` dan `pass` dengan jaringan WiFi Anda.
5. *Upload* kode ke NodeMCU ESP8266.

### 2. Cara Kalibrasi Wadah (Tare)

Saat pertama kali digunakan, pastikan wadah pakan dalam keadaan **benar-benar kosong**. Tekan tombol **Tare (V7)** di aplikasi Blynk. Sistem akan menyimpan berat kosong ini sebagai titik `0 gram` secara permanen.

### 3. Cara Mengatur Jadwal Makan

1. Ubah selektor mode (V6) ke **Schedule**.
2. Pada kolom **Input Jadwal (V4)**, ketik waktu makan menggunakan format 24-jam dengan pemisah tanda koma.
3. Contoh penulisan: `07:00, 13:30, 20:15`
4. Alat akan otomatis memecah teks tersebut dan mengeksekusi pemberian pakan pada jam-jam yang ditentukan (Sistem membaca waktu zona WIB / UTC+7).

---

## 📝 Catatan Penting

* Hindari menggunakan WiFi yang membutuhkan login halaman web (Captive Portal), seperti WiFi kampus atau kafe. Gunakan Hotspot pribadi atau Router rumah standar.
* Jika sinyal WiFi di dashboard (V9) menunjukkan angka mendekati `-90 dBm`, pertimbangkan untuk mendekatkan alat ke *router* agar jadwal tidak terlewat akibat terputusnya koneksi.