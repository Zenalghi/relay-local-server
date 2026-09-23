# R-Sync ESP32 Local Server Firmware ⚡

Dokumentasi resmi firmware controller gabungan **4 Relay + 6 Servo (3 Wall Switch) + Timer Engine + Scheduler** berbasis ESP32 untuk proyek **R-Sync (Relay-Sync)**. Firmware ini bertindak sebagai *standalone local server* berkinerja tinggi yang mengeksekusi kontrol perangkat keras, penjadwalan otomatis berbasis NTP, pewaktu *countdown*, antarmuka layar OLED I2C, serta REST API asinkron untuk aplikasi multiplatform R-Sync.

- **Repositori Firmware ESP32**: [https://github.com/Zenalghi/relay-local-server](https://github.com/Zenalghi/relay-local-server)
- **Repositori Aplikasi Flutter**: [https://github.com/Zenalghi/r_sync_app](https://github.com/Zenalghi/r_sync_app)

---

## 📋 Daftar Isi
1. [Fitur Utama](#-fitur-utama)
2. [Pinout & Skema Koneksi Hardware](#-pinout--skema-koneksi-hardware)
3. [Spesifikasi REST API](#-spesifikasi-rest-api)
4. [Fitur Timer Pewaktu & Penjadwalan NTP](#-fitur-timer-pewaktu--penjadwalan-ntp)
5. [Antarmuka Layar OLED SSD1306](#-antarmuka-layar-oled-ssd1306)
6. [Manajemen Wi-Fi & Captive Portal](#-manajemen-wi-fi--captive-portal)
7. [Keamanan OTA Password & secrets.ini](#-keamanan-ota-password--secretsini)
8. [Panduan Kompilasi & Flash PlatformIO](#-panduan-kompilasi--flash-platformio)

---

## ⚡ Fitur Utama

- **4-Channel Relay Control**: Mengontrol 4 beban AC/DC (GPIO 4, 16, 17, 5) dengan konfigurasi logika *Active LOW* / *Active HIGH* yang dapat diubah via API.
- **6-Servo Wall Switch Driver (3 Saklar Tembok)**:
  - Saklar Tembok A: Servo ON (`GPIO 14`), Servo OFF (`GPIO 27`)
  - Saklar Tembok B: Servo ON (`GPIO 26`), Servo OFF (`GPIO 25`)
  - Saklar Tembok C: Servo ON (`GPIO 33`), Servo OFF (`GPIO 32`)
  - Kalibrasi sudut interaktif: `restAngle` (default 90° - netral mengambang) & `pressAngle` (default 0° - menekan saklar).
  - *Boot Self-Test*: Gerakan uji coba 3x saat ESP32 pertama kali menyala untuk mendeteksi kemacetan mekanis.
- **10-Slot Countdown Timer Engine**:
  - Hitung mundur hingga HH:MM:SS dengan pilihan target bebas (Relay 1..4 & Saklar A..C) dan aksi target (ON/OFF).
  - Opsi *"Lakukan Kebalikan saat Mulai & Selesai"*: Langsung me-trigger status kebalikan saat timer dimulai, lalu me-trigger status target saat hitungan habis.
- **7-Channel Smart Scheduler**:
  - 4 slot jadwal harian per channel untuk 4 Relay dan 3 Saklar Tembok.
- **Asynchronous Web Server (ESPAsyncWebServer)**:
  - Melayani request HTTP tanpa *blocking* loop utama, dilengkapi *CORS Headers* untuk klien Web.
- **Smart Capability Discovery API (`GET /api/capabilities`)**:
  - Mengirimkan kapasitas dan ketersediaan fitur hardware secara dinamis ke klien Flutter.
- **OLED Display Multi-Halaman (128x64 SSD1306)**:
  - 4 halaman informasi (Status, Jadwal Relay, Jadwal Saklar, Timer Aktif).
- **Keamanan OTA Password (`secrets.ini`)**:
  - Pembaruan firmware nirkabel terlindungi password dengan otentikasi *MD5 Digest Challenge-Response*.

---

## 🔌 Pinout & Skema Koneksi Hardware (ESP32 DevKit V1)

| Komponen | Pin ESP32 | Mode / Keterangan |
| :--- | :--- | :--- |
| **Relay Channel 1** | `GPIO 4` (D4) | Output Relay 1 |
| **Relay Channel 2** | `GPIO 16` (RX2) | Output Relay 2 |
| **Relay Channel 3** | `GPIO 17` (TX2) | Output Relay 3 |
| **Relay Channel 4** | `GPIO 5` (D5) | Output Relay 4 |
| **Switch A (Servo ON / OFF)** | `GPIO 14` / `GPIO 27` | Servo Saklar Tembok A |
| **Switch B (Servo ON / OFF)** | `GPIO 26` / `GPIO 25` | Servo Saklar Tembok B |
| **Switch C (Servo ON / OFF)** | `GPIO 33` / `GPIO 32` | Servo Saklar Tembok C |
| **OLED SSD1306 SDA** | `GPIO 21` (D21) | I2C Data (Alamat I2C: `0x3C`) |
| **OLED SSD1306 SCL** | `GPIO 22` (D22) | I2C Clock |
| **Tombol BOOT** | `GPIO 0` | Input Pull-Up (Tekan singkat = ganti hal OLED; Tahan 10s = toggle polaritas; Tahan saat boot = reset WiFi) |

---

## 📡 Spesifikasi REST API

### 1. `GET /api/capabilities`
Mendapatkan kapabilitas hardware dan fitur yang didukung ESP32.
```json
{
  "device_name": "R-Sync ESP32 Server",
  "version": "2.0.0",
  "relays_count": 4,
  "switches_count": 3,
  "servos_count": 6,
  "timer_feature": true,
  "max_timers": 10,
  "scheduler_feature": true,
  "max_schedules_per_channel": 4,
  "servo_config_feature": true
}
```

### 2. `GET /api/status`
Mendapatkan status real-time IP, Wi-Fi, waktu NTP, status relay 1..4, status saklar A..C, sudut servo, dan daftar timer aktif.

### 3. `POST /api/relay`
Mengontrol relay 1..4. Payload: `{"channel": 1, "state": "ON"}`.

### 4. `POST /api/switch`
Mengontrol saklar tembok A..C (index 0=A, 1=B, 2=C). Payload: `{"switch": 0, "state": "ON"}`.

### 5. `POST /api/servo/test`
Memicu gerakan uji coba 3x untuk seluruh servo.

### 6. `POST /api/servo/config`
Mengalibrasi sudut servo. Payload: `{"restAngle": 90, "pressAngle": 0, "pressDurationMs": 400}`.

### 7. `POST /api/timer/add`
Menambahkan timer countdown baru. Payload:
```json
{
  "durationSec": 300,
  "invertOnStartEnd": true,
  "targetAction": "ON",
  "targetRelays": [true, false, false, false],
  "targetSwitches": [false, false, false]
}
```

### 8. `POST /api/timer/control`
Mengontrol timer aktif. Payload: `{"id": 1, "command": "pause"}` *(pilihan command: `"pause"`, `"resume"`, `"cancel"`)*.

---

## 🔒 Keamanan OTA Password & `secrets.ini`

Firmware ini menggunakan otentikasi password pada skema upload OTA (*Over-The-Air*).

### Konfigurasi Password OTA:
1. Buat / edit file `secrets.ini` di direktori utama projek:
```ini
[secrets]
ota_password = change-me
```
2. File `secrets.ini` tercantum pada `.gitignore` sehingga password rahasia kamu tidak akan terunggah ke repositori publik.

---

## 🛠️ Panduan Kompilasi & Flash PlatformIO

### 1. Upload Pertama Kali via Kabel USB Serial
Saat pertama kali memasang firmware baru atau baru saja mengubah `ota_password` pada `secrets.ini`, kamu **WAJIB** melakukan flash via kabel USB ke PC:

```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe run -e usb -t upload
```

### 2. Upload Nirkabel via Wi-Fi (OTA)
Setelah partisi dual-bank dan password OTA tersimpan pada ESP32, pembaruan selanjutnya dapat dilakukan secara nirkabel via Wi-Fi:

```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe run -t upload
```

### 3. Buka Serial Monitor (115200 Baud):
```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe device monitor
```

---
*Dikembangkan dengan ❤️ untuk ekosistem open-source **R-Sync** oleh **Zenalghi** ([Firmware ESP32](https://github.com/Zenalghi/relay-local-server) • [Aplikasi Flutter](https://github.com/Zenalghi/r_sync_app)).*
