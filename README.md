# R-Sync ESP32 Local Server Firmware ⚡

Dokumentasi resmi firmware controller relay berbasis ESP32 untuk proyek **R-Sync (Relay-Sync)**. Firmware ini bertindak sebagai *standalone local server* berkinerja tinggi yang mengeksekusi kontrol perangkat keras, penjadwalan otomatis berbasis NTP, antarmuka layar OLED I2C, serta REST API asinkron untuk aplikasi multiplatform R-Sync.

- **Repositori Firmware ESP32**: [https://github.com/Zenalghi/relay-local-server](https://github.com/Zenalghi/relay-local-server)
- **Repositori Aplikasi Flutter**: [https://github.com/Zenalghi/r_sync_app](https://github.com/Zenalghi/r_sync_app)

---

## 📋 Daftar Isi
1. [Fitur Utama](#-fitur-utama)
2. [Pinout & Skema Koneksi Hardware](#-pinout--skema-koneksi-hardware)
3. [Spesifikasi REST API](#-spesifikasi-rest-api)
4. [Logika Penjadwalan & Rekonsiliasi Waktu](#-logika-penjadwalan--rekonsiliasi-waktu)
5. [Antarmuka Layar OLED SSD1306](#-antarmuka-layar-oled-ssd1306)
6. [Manajemen Wi-Fi & Captive Portal](#-manajemen-wi-fi--captive-portal)
7. [Over-The-Air (OTA) & Skema Partisi](#-over-the-air-ota--skema-partisi)
8. [Panduan Kompilasi & Flash PlatformIO](#-panduan-kompilasi--flash-platformio)

---

## ⚡ Fitur Utama

- **Dual-Channel Active-LOW Relay**: Mengontrol 2 beban AC/DC secara terisolasi (GPIO 26 & 25).
- **Asynchronous Web Server (ESPAsyncWebServer)**: Mampu melayani ratusan request per detik tanpa *blocking* loop utama, dilengkapi *CORS Headers* untuk klien Web.
- **Sinkronisasi Jam Internet NTP**: Sinkron ke `pool.ntp.org` dengan offset GMT+7 (Waktu Indonesia Barat / WIB).
- **Penyimpanan Permanen Non-Volatile (NVS Preferences)**: Jadwal relay tersimpan aman di flash internal ESP32 dan tidak akan hilang saat mati lampu.
- **Smart State Reconciliation**: Saat ESP32 baru menyala atau jam NTP tersinkronisasi, relay otomatis menyesuaikan status berdasarkan jadwal terakhir yang seharusnya aktif.
- **OLED Display (128x64 SSD1306)**:
  - **Halaman 1 (Device Status)**: Status Wi-Fi, IP, Jam Digital Realtime, dan Status Relay 1 & 2.
  - **Halaman 2 (Scheduler List)**: Tabel Grid 2x2 simetris untuk jadwal aktif Relay 1 & 2.
  - **Footer Global**: Menampilkan branding `R-Sync` di baris terbawah pada semua layar.
- **WiFiManager Captive Portal**:
  - Konfigurasi Wi-Fi via browser di `192.168.4.1` (SSID: `R-Sync`) dengan logo custom SVG.
  - Toleransi mati lampu (*Blackout Tolerance*): Menunggu router menyala hingga 60 detik sebelum membuka portal.
  - Tombol Fisik BOOT (GPIO 0): Reset Wi-Fi instan (0 detik) saat dicolokkan ke daya.
  - Auto-reboot otomatis setelah menyimpan Wi-Fi baru.
- **Wireless OTA Update**: Mendukung upload firmware nirkabel via LAN (`ArduinoOTA`).

---

## 🔌 Pinout & Skema Koneksi Hardware

| Komponen | Pin ESP32 (DevKit v1) | Mode / Keterangan |
| :--- | :--- | :--- |
| **Relay Channel 1** | `GPIO 26` | Output (Active LOW: `LOW = ON`, `HIGH = OFF`) |
| **Relay Channel 2** | `GPIO 25` | Output (Active LOW: `LOW = ON`, `HIGH = OFF`) |
| **Tombol BOOT / Switch Page** | `GPIO 0` | Input Pull-Up (Tekan saat boot = Reset Wi-Fi; Tekan saat running = Ganti Halaman OLED) |
| **OLED SSD1306 SDA** | `GPIO 21` | I2C Data (Alamat I2C: `0x3C`) |
| **OLED SSD1306 SCL** | `GPIO 22` | I2C Clock |
| **VCC & GND** | `3V3 / 5V` & `GND` | Catu daya modul relay & OLED |

---

## 📡 Spesifikasi REST API

Server mendengarkan koneksi HTTP pada port default `80`.

### 1. `GET /api/status`
Mengambil status lengkap sistem, koneksi jaringan, jam real-time, status relay, dan seluruh daftar jadwal.

- **Response `200 OK`**:
```json
{
  "ip": "192.168.100.205",
  "wifi": "Connected",
  "time": "2026-09-13 02:45:00",
  "relay1": "ON",
  "relay2": "OFF",
  "displayPage": 0,
  "jobs1": [
    { "h": 6, "m": 30, "a": "OFF", "e": true },
    { "h": 17, "m": 45, "a": "ON", "e": true },
    { "h": 0, "m": 0, "a": "OFF", "e": false },
    { "h": 0, "m": 0, "a": "OFF", "e": false }
  ],
  "jobs2": [
    { "h": 2, "m": 10, "a": "OFF", "e": true },
    { "h": 2, "m": 11, "a": "ON", "e": true },
    { "h": 6, "m": 29, "a": "OFF", "e": true },
    { "h": 17, "m": 40, "a": "ON", "e": true }
  ]
}
```

---

### 2. `POST /api/relay`
Mengontrol status relay secara manual (mengaktifkan mode *Manual Override*).

- **Payload JSON**:
```json
{
  "channel": 1,
  "state": "ON"
}
```
*(Nilai `channel`: `1` atau `2`. Nilai `state`: `"ON"` atau `"OFF"`)*.

- **Response `200 OK`**:
```json
{ "status": "OK" }
```

---

### 3. `POST /api/schedule`
Menyimpan dan menyinkronkan daftar jadwal otomatis ke penyimpanan flash NVS ESP32.

- **Payload JSON**:
```json
{
  "channel": 1,
  "jobs": [
    { "h": 6, "m": 30, "a": "OFF", "e": true },
    { "h": 17, "m": 45, "a": "ON", "e": true }
  ]
}
```

- **Response `200 OK`**:
```json
{ "status": "OK" }
```

---

### 4. `POST /api/display`
Mengganti halaman aktif pada layar fisik OLED ESP32 secara jarak jauh (*remote*).

- **Payload JSON**:
```json
{ "page": 1 }
```
*(Nilai `page`: `0` untuk Status Utama, `1` untuk Tabel Scheduler, atau tanpa field untuk toggle halaman)*.

- **Response `200 OK`**:
```json
{ "status": "OK", "displayPage": 1 }
```

---

### 5. `POST /api/wifi/reset`
Menghapus kredensial Wi-Fi tersimpan dari NVS dan me-restart ESP32 seketika ke mode Captive Portal AP (`R-Sync` di `192.168.4.1`).

- **Response `200 OK`**:
```json
{
  "status": "OK",
  "message": "Resetting WiFi credentials. Opening Portal 'R-Sync'..."
}
```

---

## ⏰ Logika Penjadwalan & Rekonsiliasi Waktu

1. **Evaluasi Tiap Menit (`checkSchedules`)**:
   Setiap perubahan menit, ESP32 mencocokkan waktu sistem dengan seluruh slot jadwal yang aktif (`enabled == true`). Jika cocok, pin relay dieksekusi dan flag `manualOverride` di-reset ke `false`.
2. **Prioritas Manual Override**:
   Jika pengguna menekan tombol saklar manual di dashboard aplikasi (atau via API), relay segera berubah status dan jadwal otomatis diabaikan sampai giliran jadwal berikutnya tiba.
3. **Rekonsiliasi Status Pasca Boot / NTP Sync (`reconcileState`)**:
   Saat pertama kali jam tersinkronisasi dengan NTP internet, ESP32 menghitung selisih menit mundur terhadap seluruh jadwal aktif hari itu untuk menentukan apakah relay saat ini seharusnya dalam kondisi ON atau OFF.

---

## 🖥️ Antarmuka Layar OLED SSD1306

Layar berukuran 128x64 piksel dibagi menjadi 3 zona visual:
1. **Header (Y = 0–9)**: Judul layar di tengah dengan garis pemisah horizontal.
2. **Body (Y = 11–52)**: Data utama (informasi status atau tabel 2x2).
3. **Footer (Y = 56–63)**: Teks branding **`R-Sync`** di tengah bawah.

### Halaman 1: Device Status
```text
        - DEVICE STATUS -
───────────────────────────────────
WiFi : 192.168.100.205
Time : 17:30:45
Relay 1: ON
Relay 2: OFF
              R-Sync
```

### Halaman 2: Scheduler List (Grid 2x2)
```text
        - SCHEDULER LIST -
───────────────────────────────────
R1: 06:30-          17:45+

───────────────────────────────────
R2: 02:10-          02:11+
    06:29-          17:40+
              R-Sync
```
*(Keterangan: `+` = Action ON, `-` = Action OFF)*.

---

## 📶 Manajemen Wi-Fi & Captive Portal

1. **Auto-Connect dengan Toleransi Mati Lampu (60 Detik)**:
   Router ISP rumah (IndiHome/FirstMedia dll.) umumnya butuh waktu 45–60 detik untuk menyalakan pemancar Wi-Fi setelah listrik padam. `wm.setConnectTimeout(60)` memastikan ESP32 tidak langsung terburu-buru membuka portal saat router masih proses *booting*.
2. **Instant Reset via Tombol BOOT (0 Detik)**:
   Tahan tombol BOOT (GPIO 0) saat menyambungkan kabel daya untuk membuka Captive Portal secara instan.
3. **Captive Portal Otomatis**:
   Jika Wi-Fi rumah tidak ditemukan, ESP32 memancarkan hotspot Wi-Fi bernama `R-Sync`. Hubungkan smartphone ke SSID tersebut, buka browser ke `192.168.4.1`, pilih Wi-Fi baru, masukkan password, lalu klik **Save**. ESP32 akan reboot otomatis secara bersih.

---

## 🚀 Over-The-Air (OTA) & Skema Partisi

Firmware menggunakan skema partisi **Dual-Bank OTA** `min_spiffs.csv` pada flash 4MB:
- **`app0`**: 1.90 MB (Partisi Aplikasi Aktif)
- **`app1`**: 1.90 MB (Partisi Cadangan untuk Menerima Update OTA)
- **`spiffs`**: 128 KB
- **`nvs`**: 20 KB (Kredensial Wi-Fi & Jadwal)

### Konfigurasi OTA di `platformio.ini`:
```ini
upload_protocol = espota
upload_port = 192.168.100.205   ; Ganti dengan IP lokal ESP32 Anda
```

Saat proses OTA berlangsung, layar OLED menampilkan status:
```text
        - SYSTEM UPDATE -
───────────────────────────────────
      [==============    ]
         Progress: 75%
              R-Sync
```

---

## 🛠️ Panduan Kompilasi & Flash PlatformIO

### Kebutuhan:
- Visual Studio Code dengan ekstensi **PlatformIO IDE**.
- Kabel Data USB (Micro-USB ke PC).

### Perintah PlatformIO (Terminal):
```powershell
# 1. Kompilasi Firmware
pio run

# 2. Flash via Kabel USB (Wajib untuk inisialisasi partisi pertama kali)
pio run -t upload --upload-port COM11

# 3. Flash via OTA (Setelah partisi dual-bank terpasang)
pio run -t upload

# 4. Buka Serial Monitor (Baud rate 115200)
pio device monitor
```

---
*Dikembangkan dengan ❤️ untuk ekosistem open-source **R-Sync** oleh **Zenalghi** ([Firmware ESP32](https://github.com/Zenalghi/relay-local-server) • [Aplikasi Flutter](https://github.com/Zenalghi/r_sync_app)).*
