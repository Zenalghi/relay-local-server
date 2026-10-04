# R-Sync ESP32 Local Server Firmware ⚡

Dokumentasi resmi firmware controller gabungan **4 Relay + 6 Servo (3 Wall Switch) + Remote AC Pintar (IR Emitter) + Timer Engine + Scheduler** berbasis ESP32 untuk proyek **R-Sync (Relay-Sync)**. Firmware ini bertindak sebagai *standalone local server* berkinerja tinggi yang mengeksekusi kontrol perangkat keras secara non-blocking, penjadwalan otomatis berbasis NTP, pewaktu *countdown*, antarmuka layar OLED I2C 4-halaman, serta REST API asinkron untuk aplikasi multiplatform R-Sync dan AI Agent.

- **Repositori Firmware ESP32**: [https://github.com/Zenalghi/relay-local-server](https://github.com/Zenalghi/relay-local-server)
- **Repositori Aplikasi Flutter**: [https://github.com/Zenalghi/r_sync_app](https://github.com/Zenalghi/r_sync_app)
- **Dokumentasi AI / Local LLM Tool Calling**: [R-Sync_API_TOOL_CALLING.md](file:///c:/Nova/relay-local-server/R-Sync_API_TOOL_CALLING.md)

---

## 📋 Daftar Isi
1. [Fitur Utama & Arsitektur Sistem](#-fitur-utama--arsitektur-sistem)
2. [Pinout & Skema Koneksi Hardware](#-pinout--skema-koneksi-hardware)
3. [Mekanisme Pengamanan Hardware](#-mekanisme-pengamanan-hardware)
4. [Spesifikasi Lengkap REST API](#-spesifikasi-lengkap-rest-api)
5. [Remote AC Pintar (IR Gree / FLiFE)](#-remote-ac-pintar-ir-gree--flife)
6. [Timer Engine & Penjadwalan NTP](#-timer-engine--penjadwalan-ntp)
7. [Antarmuka Layar OLED SSD1306 & Tombol BOOT](#-antarmuka-layar-oled-ssd1306--tombol-boot)
8. [Manajemen Wi-Fi & Captive Portal](#-manajemen-wi-fi--captive-portal)
9. [Keamanan OTA Password & secrets.ini](#-keamanan-ota-password--secretsini)
10. [Panduan Kompilasi & Flash PlatformIO](#-panduan-kompilasi--flash-platformio)

---

## ⚡ Fitur Utama & Arsitektur Sistem

- **Remote AC Pintar (IR Emitter - Gree / FLiFE Protocol)**:
  - Mengendalikan AC Gree dan FLiFE (model YAW1F) melalui sinyal infrared 38kHz.
  - Kontrol lengkap: Daya ON/OFF, Suhu (16–30°C), Mode (Auto, Cool, Dry, Fan, Heat), Kecepatan Kipas (Auto, Min, Med, Max), Swing Vertikal, Sleep, Turbo, X-Fan (pengering kisi-kisi evaporator), Lampu LED Display, Sensor I-Feel, dan Pemilihan Tampilan Suhu Display.
  - Sifat perintah **idempoten dan non-toggle**: Mengirimkan perintah `power: false` saat AC mati tidak akan membalikkan status AC.
  - Status AC tersimpan secara persisten di ESP32 flash Preferences NVS.
  - Terintegrasi penuh dengan Timer dan Penjadwal harian via parameter `targetAc`.
- **4-Channel Relay Control**:
  - Mengontrol 4 beban AC/DC independen (`GPIO 4, 16, 17, 5`).
  - Polaritas logika *Active LOW* (standar optocoupler) / *Active HIGH* yang dapat dikonfigurasi via API atau tombol fisik.
  - Perlindungan otomatis: Mengubah polaritas seketika memaksa seluruh 4 relay ke kondisi `OFF` sebelum menerapkan logika baru.
- **6-Servo Wall Switch Driver (3 Saklar Mekanis Dinding)**:
  - 3 Saklar Dinding (A, B, C) digerakkan oleh 6 unit motor servo (2 servo per saklar: 1 untuk tekan ON, 1 untuk tekan OFF).
  - Saklar Tembok A: Servo ON (`GPIO 14`), Servo OFF (`GPIO 27`).
  - Saklar Tembok B: Servo ON (`GPIO 26`), Servo OFF (`GPIO 25`).
  - Saklar Tembok C: Servo ON (`GPIO 33`), Servo OFF (`GPIO 32`).
  - **Mekanisme Anti-Brownout (BOD)**: Menggunakan antrean non-blocking FIFO (12 slot). Servo hanya di-attach saat bergerak, menekan selama durasi yang dapat dikalibrasi (default 400ms), kembali ke sudut istirahat (`restAngle` default 90°), lalu **segera di-detach (0 mA holding torque)** agar tidak menarik arus idle yang dapat menyebabkan tegangan 5V drop.
- **Unified Countdown Timer Engine (10 Slot)**:
  - Hitung mundur hingga jam:menit:detik untuk Relay 1..4, Saklar A..C, dan Remote AC (`targetAc`).
  - Mode *"Invert on Start & End"*: Menyalakan perangkat di awal timer dan mematikannya saat countdown selesai (atau sebaliknya).
  - Manajemen status lengkap: Tambah, Update, Pause, Resume, Cancel, Restart, dan Delete. Riwayat timer selesai tersimpan di NVS.
- **Smart NTP Scheduler (10 Slot)**:
  - Penjadwalan harian otomatis berbasis waktu NTP (WIB UTC+7).
  - Rekonsiliasi otomatis (*Auto-Reconciliation*) saat boot atau saat koneksi internet pulih untuk menyelaraskan kondisi fisik dengan jadwal yang telah terlewati.
- **Port Hardware Aktif Dinamis (`/api/hardware/config`)**:
  - Relay 1..4, Saklar A..C, dan Remote AC dapat dinonaktifkan/diaktifkan secara mandiri via API dan tersimpan permanen di NVS.
- **Asynchronous Web Server (ESPAsyncWebServer)**:
  - Pemrosesan HTTP non-blocking dengan dukungan CORS universal (`*`) untuk aplikasi Flutter dan AI Tool Calling.
- **OLED SSD1306 Multi-Page (128x64 I2C)**:
  - 4 Halaman Tampilan: Status Ringkas, Jadwal Harian, Timer Aktif, dan Status Remote AC.
- **Keamanan Over-The-Air (OTA) & secrets.ini**:
  - Flash firmware nirkabel terproteksi password dengan enkripsi autentikasi MD5.

---

## 🔌 Pinout & Skema Koneksi Hardware (ESP32 DevKit V1)

| Komponen | Pin ESP32 | Mode I/O | Keterangan |
| :--- | :--- | :--- | :--- |
| **Relay Channel 1** | `GPIO 4` (D4) | Output | Beban Listrik 1 |
| **Relay Channel 2** | `GPIO 16` (RX2) | Output | Beban Listrik 2 |
| **Relay Channel 3** | `GPIO 17` (TX2) | Output | Beban Listrik 3 |
| **Relay Channel 4** | `GPIO 5` (D5) | Output | Beban Listrik 4 |
| **Switch A - Servo ON** | `GPIO 14` | PWM Servo | Motor Servo 0 (Saklar A Tekan ON) |
| **Switch A - Servo OFF** | `GPIO 27` | PWM Servo | Motor Servo 1 (Saklar A Tekan OFF) |
| **Switch B - Servo ON** | `GPIO 26` | PWM Servo | Motor Servo 2 (Saklar B Tekan ON) |
| **Switch B - Servo OFF** | `GPIO 25` | PWM Servo | Motor Servo 3 (Saklar B Tekan OFF) |
| **Switch C - Servo ON** | `GPIO 33` | PWM Servo | Motor Servo 4 (Saklar C Tekan ON) |
| **Switch C - Servo OFF** | `GPIO 32` | PWM Servo | Motor Servo 5 (Saklar C Tekan OFF) |
| **IR Emitter (Remote AC)** | `GPIO 18` | Output IR | Transmiter IR (Gree/FLiFE) via Transistor |
| **OLED SSD1306 SDA** | `GPIO 21` (D21) | I2C Data | Layar OLED (Alamat: `0x3C`) |
| **OLED SSD1306 SCL** | `GPIO 22` (D22) | I2C Clock | Layar OLED Clock |
| **Tombol BOOT** | `GPIO 0` | Input Pull-Up | Navigasi OLED & Konfigurasi Fisik |
| **Servo Rail Monitor** | `SERVO_RAIL_ADC_PIN` | ADC Input | Pembagi tegangan 100k/100k (opsional, default: `-1`) |

### Skema Rangkaian IR Emitter (GPIO 18)
Untuk menghasilkan daya pancar infrared maksimal yang mampu memantul di dinding ruangan tanpa kehilangan sinyal, gunakan transistor NPN (misalnya 2N2222 atau S8050) dengan **3 buah resistor dirangkai paralel** pada jalur daya LED IR untuk menurunkan resistansi efektif menjadi ~97Ω (menghasilkan *peak current* ~40–50mA yang aman dan kuat bagi LED IR):

```text
              +5V (VIN ESP32)
                     |
        +------------+------------+
        |            |            |
      [220Ω]       [220Ω]       [330Ω]   <-- 3 Resistor Dipasang Paralel (~97Ω)
        |            |            |
        +------------+------------+
                     |
                     |
           Anoda (+) [LED IR] (Kaki Panjang)
           Katoda (-) [LED IR] (Kaki Pendek)
                     |
                     |
                 C (Kolektor)

GPIO 18 --- [330Ω] --- B (Basis)  [Transistor NPN (2N2222 / S8050)]
             (R_basis)

                     E (Emitter)
                     |
                 GND ESP32
```

> [!TIP]
> Rangkaian paralel 3 resistor di atas menghasilkan hambatan ekuivalen:  
> $\frac{1}{R_{total}} = \frac{1}{220} + \frac{1}{220} + \frac{1}{330} \approx 97.05\,\Omega$.  
> Nilai ini memberikan arus pancar pulsa IR yang optimal tanpa membebani regulator 5V ESP32.

---

## 🛡️ Mekanisme Pengamanan Hardware

1. **Anti-Brownout & Isolasi Beban Servo (Detached Mode):**
   - Menjalankan banyak motor servo secara bersamaan dapat memicu penurunan tegangan (*voltage sag*) di bawah ambang batas BOD (Brownout Detector) ESP32 (~2.8V – 3.0V) yang mereset sistem secara mendadak.
   - Firmware R-Sync menggunakan antrean **FIFO non-blocking**. Gerakan servo dieksekusi **satu per satu secara berurutan**.
   - Setiap servo hanya di-attach saat bergerak, menekan saklar selama `pressDurationMs` (default 400ms), kembali ke `restAngle` (default 90°), lalu **langsung di-detach (sudut diset `-1`)**.
   - Dalam kondisi detach, servo mengonsumsi arus statis 0 mA (tanpa torsi idle), membebaskan jalur suplai daya 5V sepenuhnya.

2. **Perlindungan Polaritas Relay (*Fail-Safe Reset*):**
   - Ketika logika polaritas relay diubah via `/api/relay/polarity` atau melalui tombol fisik BOOT, firmware secara otomatis memaksa **seluruh 4 relay ke kondisi OFF** sebelum menerapkan tingkat logika baru. Hal ini mencegah terjadinya *short-circuit* atau beban aktif tiba-tiba saat pembalikan logika.

3. **Penyimpanan Non-Volatile Memory (NVS Preferences):**
   - Seluruh status operasional kritis disimpan pada partisi NVS flash ESP32 menggunakan library `Preferences`:
     - `cfg`: Konfigurasi polaritas (`activeLow`), kalibrasi sudut servo (`restAngle`, `pressAngle`, `pressDur`, `pa0`..`pa5`).
     - `hwcfg`: Status channel aktif (`r0`..`r3`, `s0`..`s2`, `ac`).
     - `ac_state`: State remote AC lengkap (power, temp, mode, fan, swing, turbo, sleep, xfan, light, ifeel, display_temp).
     - `timers`: Slot timer hitung mundur dan status selesai.
     - `sched2`: Daftar 10 slot jadwal harian otomatis.
   - Jika terjadi pemadaman listrik (*power outage*), seluruh konfigurasi dan state AC akan otomatis dipulihkan kembali saat boot.

---

## 📡 Spesifikasi Lengkap REST API

Base URL: `http://<ESP32_IP>` (Port default 80).  
Header: `Content-Type: application/json`.  
Dukungan: CORS Universal (`Access-Control-Allow-Origin: *`).

### 1. `GET /api/capabilities`
Mendapatkan spesifikasi kapabilitas firmware dan ketersediaan hardware yang sedang aktif.
```json
{
  "device_name": "R-Sync ESP32 Server",
  "version": "3.0.0",
  "oled_connected": true,
  "ac_feature": true,
  "active_relays": [true, true, true, true],
  "active_switches": [true, true, true],
  "relays_count": 4,
  "switches_count": 3,
  "servos_count": 6,
  "timer_feature": true,
  "max_timers": 10,
  "scheduler_feature": true,
  "max_schedules": 10,
  "servo_config_feature": true
}
```

---

### 2. `GET /api/status`
Mendapatkan status operasional menyeluruh perangkat secara real-time.
```json
{
  "ip": "192.168.1.50",
  "wifi": "Connected",
  "time": "2026-10-04 21:00:00",
  "activeLow": true,
  "displayPage": 0,
  "oledConnected": true,
  "restAngle": 90,
  "pressAngle": 0,
  "pressDurationMs": 400,
  "pressAngles": [0, 0, 0, 0, 0, 0],
  "servoAngles": [-1, -1, -1, -1, -1, -1],
  "servoQueueLength": 0,
  "servoBusy": false,
  "servoRailMv": 5020,
  "servoRailMinMv": 4850,
  "relays": ["OFF", "ON", "OFF", "OFF"],
  "relayActive": [true, true, true, true],
  "switches": ["ON", "OFF", "OFF"],
  "switchActive": [true, true, true],
  "timers": [
    {
      "id": 1,
      "totalDurationSec": 600,
      "remainingSec": 320,
      "paused": false,
      "finished": false,
      "invertOnStartEnd": true,
      "targetAction": "OFF",
      "targetAc": 2,
      "targetRelays": [true, false, false, false],
      "targetSwitches": [false, false, false]
    }
  ],
  "schedules": [
    {
      "h": 18,
      "m": 30,
      "a": "ON",
      "e": true,
      "targetAc": 1,
      "r": [true, true, false, false],
      "s": [false, false, false]
    }
  ],
  "ac": {
    "power": true,
    "temp": 24,
    "mode": 1,
    "fan": 0,
    "swing_v": true,
    "sleep": false,
    "turbo": false,
    "xfan": false,
    "light": true,
    "ifeel": false,
    "display_temp": 1
  }
}
```

---

### 3. `POST /api/relay`
Mengontrol status hidup/mati Relay 1 sampai 4.
```json
// Request:
{
  "channel": 1,
  "state": "ON"
}

// Response 200 OK:
{ "status": "OK" }
```
- `channel` (int, wajib): `1`, `2`, `3`, atau `4` (1-indexed).
- `state` (string, wajib): `"ON"` atau `"OFF"`.

---

### 4. `POST /api/switch`
Memicu motor servo untuk menekan saklar tembok mekanis A, B, atau C.
```json
// Request:
{
  "switch": 0,
  "state": "ON"
}

// Response 200 OK:
{ "status": "OK" }
```
- `switch` (int, wajib): `0` (Saklar A), `1` (Saklar B), `2` (Saklar C) (0-indexed).
- `state` (string, wajib): `"ON"` atau `"OFF"`.

---

### 5. `POST /api/ac`
Mengirimkan perintah sinyal infrared ke remote AC FLiFE / Gree. Perintah bersifat idempoten dan mendukung pembaruan parsial.
```json
// Request:
{
  "power": true,
  "temp": 22,
  "mode": 1,
  "fan": 3,
  "swing_v": true,
  "sleep": false,
  "turbo": false,
  "xfan": false,
  "light": true,
  "ifeel": false,
  "display_temp": 1
}

// Response 200 OK:
{ "status": "OK" }

// Response 403 Forbidden (jika port AC dinonaktifkan di hardware config):
{ "status": "Error", "message": "AC Disabled" }
```

---

### 6. `GET` & `POST /api/relay/polarity` (Alias: `/api/polarity`)
Mengecek atau mengonfigurasi polaritas aktif relay.
```json
// POST /api/relay/polarity:
{
  "activeLow": true
}

// Response 200 OK:
{ "status": "OK", "activeLow": true }
```

---

### 7. `POST /api/timer` (Unified Timer Engine)
Mengelola operasi hitung mundur (Add, Update, Control, Delete).

#### a. Menambah Timer Baru (`action: "add"`):
```json
{
  "action": "add",
  "durationSec": 1800,
  "invertOnStartEnd": true,
  "targetAction": "OFF",
  "targetAc": 2,
  "targetRelays": [true, false, false, false],
  "targetSwitches": [false, true, false]
}
```
- `targetAc`: `0` = Abaikan AC, `1` = Targetkan AC Hidup, `2` = Targetkan AC Mati.

#### b. Mengontrol Timer (`action: "pause" | "resume" | "cancel" | "start" | "delete"`):
```json
{
  "action": "pause",
  "id": 1
}
```

---

### 8. `GET` & `POST /api/schedules`
Membaca atau menyimpan daftar 10 slot jadwal otomatis harian berbasis jam NTP.
```json
// POST /api/schedules:
[
  {
    "h": 21,
    "m": 0,
    "a": "ON",
    "e": true,
    "targetAc": 1,
    "r": [false, false, false, false],
    "s": [false, false, false]
  },
  {
    "h": 5,
    "m": 0,
    "a": "OFF",
    "e": true,
    "targetAc": 2,
    "r": [false, false, false, false],
    "s": [false, false, false]
  }
]
```

---

### 9. `POST /api/servo/config`
Mengalibrasi sudut motor servo dan durasi penekanan tombol.
```json
{
  "restAngle": 90,
  "pressAngle": 0,
  "pressAngles": [0, 5, 0, 10, 0, 5],
  "pressDurationMs": 400
}
```

---

### 10. `POST /api/servo/test`
Menjalankan pengujian mekanis motor servo (*Self-Test*) secara aman.
- `POST /api/servo/test` (tanpa payload): Menguji seluruh 6 servo secara berurutan.
- `POST /api/servo/test` dengan payload `{"servo": 2}`: Menguji hanya Servo index 2.

---

### 11. `POST /api/display`
Mengubah halaman tampilan layar OLED SSD1306 secara manual.
```json
{
  "page": 3
}
```
- `page`: `0` = Status Ringkas, `1` = Jadwal Harian, `2` = Timer Aktif, `3` = Remote AC.
- Jika dipanggil tanpa payload, display akan berpindah ke halaman berikutnya secara berputar (`0 -> 1 -> 2 -> 3 -> 0`).

---

### 12. `GET` & `POST /api/hardware/config`
Mengaktifkan atau menonaktifkan channel relay, saklar dinding, atau modul AC secara permanen di memori flash.
```json
// POST /api/hardware/config:
{
  "relays": [true, true, true, false],
  "switches": [true, true, true],
  "ac": true
}
```

---

### 13. `POST /api/wifi/reset`
Menghapus seluruh kredensial Wi-Fi tersimpan dan merestart ESP32 ke mode Access Point Captive Portal.
```json
// Response 200 OK:
{ "status": "OK", "message": "Resetting WiFi..." }
```

---

## ❄️ Remote AC Pintar (IR Gree / FLiFE)

Modul remote AC pada firmware R-Sync menggunakan protokol **Gree YAW1F** melalui pustaka `IRremoteESP8266`. Protokol ini mengemas seluruh parameter AC ke dalam satu paket data transmisi penuh (*full frame packet*) 64-bit setiap kali perintah dipancarkan.

### Parameter Kontrol AC (`POST /api/ac`):
| Parameter | Tipe Data | Pilihan Nilai / Jangkauan | Keterangan |
| :--- | :--- | :--- | :--- |
| `power` | `bool` | `true` (ON), `false` (OFF) | Daya unit AC (bersifat idempoten / non-toggle) |
| `temp` | `uint8` | `16` s/d `30` | Suhu target (°C) |
| `mode` | `uint8` | `0`=Auto, `1`=Cool, `2`=Dry, `3`=Fan, `4`=Heat | Mode pendinginan |
| `fan` | `uint8` | `0`=Auto, `1`=Min, `2`=Med, `3`=Max | Kecepatan hembusan kipas indoor |
| `swing_v` | `bool` | `true`, `false` | Ayunan sirip kisi udara vertikal otomatis |
| `sleep` | `bool` | `true`, `false` | Mode kenyamanan tidur malam hari |
| `turbo` | `bool` | `true`, `false` | Pendinginan kecepatan ekstra tinggi |
| `xfan` | `bool` | `true`, `false` | Pengeringan blower evaporator setelah AC mati (cegah jamur) |
| `light` | `bool` | `true`, `false` | Lampu LED indikator display pada unit AC indoor |
| `ifeel` | `bool` | `true`, `false` | Sensor suhu I-Feel |
| `display_temp` | `uint8` | `0`=Off, `1`=Set, `2`=Inside, `3`=Outside | Pilihan informasi suhu pada layar LED unit indoor |

---

## ⏱️ Timer Engine & Penjadwalan NTP

### 1. Unified Countdown Timer
- **Maksimal 10 Slot Aktif/Selesai:** Setiap timer memiliki ID unik, durasi total, durasi sisa, dan status aktif.
- **Logika `invertOnStartEnd`:**
  - Cocok untuk kebutuhan seperti *"Nyalakan pompa selama 15 menit lalu matikan"* atau *"Matikan lampu kamar selama 1 jam lalu nyalakan lagi"*.
  - Pada detik ke-0 (timer dimulai), perangkat target dipaksa ke status **kebalikan** dari `targetAction`.
  - Pada detik akhir (hitungan countdown 0), perangkat target dipaksa ke status `targetAction`.

### 2. Penjadwal Harian Cerdas (NTP Synchronization)
- **Timezone WIB (UTC+7):** Diatur melalui `configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com")`.
- **Rekonsiliasi Otomatis (*Auto-Reconciliation*):**
  - Saat ESP32 pertama kali boot atau saat koneksi internet kembali pulih setelah terputus, firmware secara otomatis memindai jadwal terdekat yang seharusnya telah aktif hari ini dan langsung mengeksekusinya ke relay dan servo saklar.

---

## 🖥️ Antarmuka Layar OLED SSD1306 & Tombol BOOT

### 1. Halaman Tampilan Layar OLED (128x64 I2C):
- **Halaman 0 (Status Ringkas):**
  - Baris atas: Jam NTP saat ini, status Wi-Fi, dan IP Lokal ESP32.
  - Baris tengah: Status Relay 1..4 (ON/OFF).
  - Baris bawah: Status Saklar Tembok A..C (ON/OFF).
- **Halaman 1 (Jadwal Harian):**
  - Menampilkan daftar jadwal aktif beserta jam:menit dan target channel.
- **Halaman 2 (Timer Countdown):**
  - Menampilkan daftar timer yang sedang berjalan beserta sisa detik countdown.
- **Halaman 3 (Remote AC FLiFE):**
  - Menampilkan status daya AC, suhu setelan saat ini, mode (COOL/AUTO/DRY/FAN), dan kecepatan kipas.

### 2. Multi-Fungsi Tombol Fisik BOOT (`GPIO 0`):
- **Saat Boot / Menyalakan Alat:**
  - Tahan tombol BOOT saat perangkat dihidupkan untuk memicu **Factory Reset Wi-Fi** dan membuka Access Point konfigurasi `R-Sync`.
- **Saat Operasi Normal (Runtime):**
  - **Tekan Singkat (< 1 detik):** Berganti halaman OLED (`Page 0 -> 1 -> 2 -> 3 -> 0`).
  - **Tahan 1 s/d 10 detik:** Menampilkan animasi countdown hitung mundur 10 detik di layar OLED.
  - **Tahan >= 11 detik:** Menjalankan **Toggle Polaritas Relay** (`Active LOW` $\leftrightarrow$ `Active HIGH`), memaksa seluruh 4 relay mati seketika, dan menyimpan konfigurasi ke NVS.

---

## 📶 Manajemen Wi-Fi & Captive Portal

1. **AutoConnect Tanpa Konfigurasi Ulang:**
   - ESP32 secara otomatis mencoba terhubung ke jaringan Wi-Fi yang tersimpan di flash.
2. **Fallback Captive Portal Mandiri:**
   - Jika jaringan Wi-Fi tidak ditemukan dalam 60 detik atau tombol BOOT ditekan saat startup, ESP32 otomatis mengaktifkan hotspot Access Point:
     - **SSID:** `R-Sync`
     - **IP Default:** `192.168.4.1`
   - Buka browser dan arahkan ke `192.168.4.1` untuk memilih SSID baru dan memasukkan password Wi-Fi melalui antarmuka web bertema R-Sync SVG.

---

## 🔒 Keamanan OTA Password & `secrets.ini`

Firmware ini menggunakan otentikasi password aman pada skema pembaruan nirkabel Over-The-Air (OTA) untuk mencegah pihak tidak berwenang memperbarui firmware ESP32 di jaringan lokal.

### Konfigurasi Password OTA:
1. Buat atau perbarui file `secrets.ini` pada root direktori proyek `relay-local-server`:
   ```ini
   [secrets]
   ota_password = ganti-dengan-password-kamu
   ```
2. File `secrets.ini` telah terdaftar di `.gitignore` sehingga password rahasia tidak akan terunggah ke repositori GitHub.

---

## 🛠️ Panduan Kompilasi & Flash PlatformIO

### 1. Flash Pertama Kali via Kabel USB Serial
Saat pertama kali memprogram modul ESP32 baru atau baru saja memperbarui `ota_password` di `secrets.ini`, gunakan lingkungan kompilasi `usb`:

```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe run -e usb -t upload
```

### 2. Flash Nirkabel via Wi-Fi (Over-The-Air)
Setelah partisi dual-bank (`min_spiffs.csv`) dan password OTA tersimpan di ESP32, pembaruan firmware dapat dilakukan secara nirkabel tanpa melepas alat:

```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe run -t upload
```

### 3. Membuka Serial Monitor (115200 Baud):
```powershell
C:\Users\user\.platformio\penv\Scripts\pio.exe device monitor
```

---

*Dikembangkan dengan standar rekayasa terbaik untuk ekosistem open-source **R-Sync** oleh **Zenalghi** ([Firmware ESP32](https://github.com/Zenalghi/relay-local-server) • [Aplikasi Flutter](https://github.com/Zenalghi/r_sync_app) • [Panduan AI Tool Calling](file:///c:/Nova/relay-local-server/R-Sync_API_TOOL_CALLING.md)).*
