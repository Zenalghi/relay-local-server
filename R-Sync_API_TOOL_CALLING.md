# R-Sync ESP32 Local Server — API & Tool Calling Specification for Local LLM 🤖⚡

Dokumentasi ini dirancang khusus sebagai panduan referensi, instruksi sistem, dan definisi **Function Calling / Tool Calling** untuk Local LLM (seperti Llama 3, Qwen 2.5, Mistral, DeepSeek, Phi-3, Gemma) agar dapat memahami, mengontrol, dan memvalidasi seluruh kapabilitas hardware ESP32 pada firmware [main.cpp](file:///c:/Nova/relay-local-server/src/main.cpp).

---

## 1. Arsitektur & Aturan Kritis Hardware (WAJIB DIPAHAMI LLM)

Sebelum memanggil fungsi/tool apapun, LLM harus memahami batasan fisik dan aturan indexing berikut:

### 1.1 Skema Indexing & Mapping Hardware
| Komponen | Penamaan Fisik | Index API Tunggal | Index dalam Array Boolean | GPIO ESP32 | Keterangan |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Relay 1** | Relay 1 | `channel: 1` (1-indexed) | `targetRelays[0]` | `GPIO 4` | Beban AC/DC 1 |
| **Relay 2** | Relay 2 | `channel: 2` (1-indexed) | `targetRelays[1]` | `GPIO 16` | Beban AC/DC 2 |
| **Relay 3** | Relay 3 | `channel: 3` (1-indexed) | `targetRelays[2]` | `GPIO 17` | Beban AC/DC 3 |
| **Relay 4** | Relay 4 | `channel: 4` (1-indexed) | `targetRelays[3]` | `GPIO 5` | Beban AC/DC 4 |
| **Switch A** | Saklar Tembok A | `switch: 0` (0-indexed) | `targetSwitches[0]` | `GPIO 14` (ON) / `GPIO 27` (OFF) | Servo 0 (ON) & Servo 1 (OFF) |
| **Switch B** | Saklar Tembok B | `switch: 1` (0-indexed) | `targetSwitches[1]` | `GPIO 26` (ON) / `GPIO 25` (OFF) | Servo 2 (ON) & Servo 3 (OFF) |
| **Switch C** | Saklar Tembok C | `switch: 2` (0-indexed) | `targetSwitches[2]` | `GPIO 33` (ON) / `GPIO 32` (OFF) | Servo 4 (ON) & Servo 5 (OFF) |
| **AC Remote** | IR Emitter | `targetAc: 1` (ON) / `2` (OFF) | `targetAc` (0,1,2) | `GPIO 18` | Transmiter IR (Gree/FLiFE) |

> ⚠️ **PERINGATAN KRITIS INDEXING UNTUK LLM:**
> - Endpoint `/api/relay` menggunakan `channel` bernilai **1 sampai 4** (1-based).
> - Endpoint `/api/switch` menggunakan `switch` bernilai **0 sampai 2** (0-based: 0 = Saklar A, 1 = Saklar B, 2 = Saklar C).
> - Pada payload array `targetRelays`, selalu berupa array 4 boolean: `[Relay1, Relay2, Relay3, Relay4]`.
> - Pada payload array `targetSwitches`, selalu berupa array 3 boolean: `[SwitchA, SwitchB, SwitchC]`.
> - Parameter `targetAc` bernilai **0** (Abaikan/Ignore), **1** (Power ON AC), atau **2** (Power OFF AC).

### 1.1b Karakteristik Transmisi IR AC (FLiFE / Gree) & Idempotensi
- **Paket State Penuh (Full Frame Transmission):** Protokol IR Gree/FLiFE tidak mengirim tombol mentah, melainkan **seluruh status AC** (Power, Suhu, Mode, Fan, Swing, Turbo, Sleep, X-Fan, Light, I-Feel, Display Temp) dalam satu paket sinyal 64-bit lengkap setiap kali dikirim.
- **Non-Toggle & Idempoten (Sangat Aman untuk AI):**
  - Perintah daya bersifat mutlak: `power: true` selalu menyalakan, `power: false` selalu mematikan.
  - Jika AC saat ini sudah mati dan AI mengirim `power: false`, AC **tetap mati** (TIDAK akan tertukar/toggle menyala).
  - Begitu pula suhu: jika AC sudah di 24°C dan AI mengirim `temp: 24`, AC tetap di 24°C.
- **Dukungan Partial Update (Delta State):**
  - Endpoint `/api/ac` mendukung payload parsial (misal hanya mengirim `{"temp": 22}`). Parameter lain yang tidak disertakan akan mempertahankan nilai terakhir yang tersimpan di flash ESP32 NVS (*Preferences*).
- **Integrasi Timer & Scheduler (`targetAc`):**
  - `0`: Abaikan AC (jangan ubah status AC).
  - `1`: Targetkan AC Menyala (`Power ON`).
  - `2`: Targetkan AC Mati (`Power OFF`).

### 1.2 Servo Non-Blocking & Anti-Brownout
- Firmware menggunakan **FIFO Queue** (maksimal 12 antrean).
- Gerakan servo dijalankan **satu per satu**.
- Servo di-attach saat akan bergerak, menekan selama `pressDurationMs` (default 400ms), kembali ke `restAngle` (default 90°), lalu **langsung di-detach (0 mA holding torque)** guna mencegah penurunan tegangan 5V (*Brownout Detector / BOD reset*).
- Jika ada *Self-Test* atau *Alignment* yang sedang berjalan (`servoBusy: true`), perintah servo baru akan ditolak atau diantrekan.

### 1.3 Polaritas Relay (`activeLow`)
- Default firmware: `activeLow = true` (LOW = Relay Aktif/ON, HIGH = Relay Mati/OFF).
- Mengubah polaritas via `/api/relay/polarity` akan **mereset dan mematikan (OFF) seluruh 4 relay seketika** demi keselamatan instalasi beban listrik.

### 1.4 Format Waktu & Penjadwalan
- Waktu lokal menggunakan NTP Timezone WIB (UTC+7: `configTime(7 * 3600, 0, ...)`).
- Penjadwalan dievaluasi setiap menit.
- Saat boot atau reconnect NTP, firmware otomatis melakukan **reconciliation** untuk mencocokkan status relay dan saklar dengan jadwal terdekat yang telah berlalu.

---

## 2. Ringkasan Endpoint REST API

Base URL: `http://<ESP32_IP>` (Port default 80).  
Header: `Content-Type: application/json` (CORS diaktifkan secara universal `*`).

| Method | Path Endpoint | Kegunaan | Parameter / Body Utama |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/capabilities` | Cek kapabilitas fitur & channel aktif | Tidak ada |
| `GET` | `/api/status` | Baca kondisi real-time lengkap perangkat | Tidak ada |
| `POST` | `/api/relay` | Kontrol Relay 1..4 | `{"channel": 1..4, "state": "ON"\|"OFF"}` |
| `POST` | `/api/switch` | Kontrol Saklar Tembok A..C via Servo | `{"switch": 0..2, "state": "ON"\|"OFF"}` |
| `POST` | `/api/ac` | Kontrol Remote AC FLiFE / Gree (IR) | `{"power", "temp", "mode", "fan", "swing_v", ...}` |
| `GET`/`POST` | `/api/relay/polarity` | Cek atau ubah polaritas relay | Query/Body: `{"activeLow": boolean}` |
| `POST` | `/api/timer` | Operasi lengkap timer (Add/Update/Control) | `action`, `durationSec`, `targetAc`, dll |
| `POST` | `/api/timer/add` | (Alias) Tambah timer hitung mundur | `durationSec`, `targetAction`, `targetAc`, `targetRelays`, `targetSwitches` |
| `POST` | `/api/timer/update` | (Alias) Edit timer yang sudah ada | `id`, `durationSec`, `targetAc`, dll |
| `POST` | `/api/timer/control` | (Alias) Jeda/Lanjut/Hentikan timer | `{"id": int, "command": "pause"\|"resume"\|"cancel"\|"start"}` |
| `GET` | `/api/schedules` | Ambil seluruh 10 slot jadwal | Tidak ada |
| `POST` | `/api/schedules` | Setel / timpa daftar 10 jadwal | Array JSON jadwal `[{h, m, a, e, targetAc, r, s}, ...]` |
| `POST` | `/api/servo/config` | Kalibrasi sudut rest & tekan servo | `restAngle`, `pressAngle`, `pressAngles`, `pressDurationMs` |
| `POST` | `/api/servo/test` | Uji coba fisik servo (*Self-Test*) | Query/Body: `{"servo": 0..5}` (opsional) |
| `POST` | `/api/display` | Ganti halaman tampilan layar OLED | `{"page": 0..3}` (opsional) |
| `GET` | `/api/hardware/config` | Cek channel relay/saklar/AC yang aktif | Tidak ada |
| `POST` | `/api/hardware/config` | Aktifkan/nonaktifkan channel fisik/AC | `{"relays": [4 bool], "switches": [3 bool], "ac": bool}` |
| `POST` | `/api/wifi/reset` | Hapus konfigurasi WiFi & restart AP | Tidak ada |

---

## 3. Spesifikasi Lengkap Endpoint & Contoh Payload

### 3.1 `GET /api/capabilities`
Mendapatkan spesifikasi kapabilitas firmware dan hardware yang sedang aktif.

- **Request:**
  ```http
  GET /api/capabilities HTTP/1.1
  Host: 192.168.1.50
  ```
- **Response 200 OK:**
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

### 3.2 `GET /api/status`
Mendapatkan status operasional menyeluruh perangkat secara real-time.

- **Request:**
  ```http
  GET /api/status HTTP/1.1
  Host: 192.168.1.50
  ```
- **Response 200 OK:**
  ```json
  {
    "ip": "192.168.1.50",
    "wifi": "Connected",
    "time": "2026-09-29 22:30:15",
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
        "remainingSec": 420,
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
      "power": false,
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
  *Keterangan Field Kritis:*
  - `time`: Jika bernilai `"Not Synced"`, NTP belum mendapatkan jam akurat.
  - `servoAngles`: Nilai `-1` menandakan servo dalam kondisi **detached** (mati / tanpa torsi idle).
  - `servoBusy`: `true` jika servo sedang bergerak, menjalankan self-test, atau menyelaraskan sudut.
  - `ac`: Objek status remote AC FLiFE / Gree yang sedang aktif tersimpan di ESP32 flash Preferences.
    - `power` (bool): Status hidup/mati AC.
    - `temp` (int): Suhu setelan (16..30 °C).
    - `mode` (int): `0` = AUTO, `1` = COOL, `2` = DRY, `3` = FAN, `4` = HEAT.
    - `fan` (int): `0` = AUTO, `1` = MIN, `2` = MED, `3` = MAX.
    - `swing_v` (bool): Swing vertikal auto (true) atau diam (false).
    - `sleep`, `turbo`, `xfan`, `light`, `ifeel` (bool): Fitur kenyamanan.
    - `display_temp` (int): `0` = OFF, `1` = SET TEMP, `2` = INSIDE TEMP, `3` = OUTSIDE TEMP.

---

### 3.3 `POST /api/relay`
Menghidupkan atau mematikan Relay 1..4.

- **Request:**
  ```json
  POST /api/relay
  Content-Type: application/json

  {
    "channel": 1,
    "state": "ON"
  }
  ```
  - `channel` (integer, wajib): `1`, `2`, `3`, atau `4`.
  - `state` (string, wajib): `"ON"` atau `"OFF"`.
- **Response 200 OK:**
  ```json
  { "status": "OK" }
  ```
- **Response 400 Bad Request:**
  ```json
  { "status": "Error", "message": "Bad Payload Relay" }
  ```

---

### 3.4 `POST /api/switch`
Memicu servo saklar mekanis tembok A..C.

- **Request:**
  ```json
  POST /api/switch
  Content-Type: application/json

  {
    "switch": 0,
    "state": "ON"
  }
  ```
  - `switch` (integer, wajib): `0` (Saklar A), `1` (Saklar B), `2` (Saklar C).
  - `state` (string, wajib): `"ON"` atau `"OFF"`.
- **Mekanisme Gerakan Servo:**
  - Jika `switch = 0` dan `state = "ON"`, Servo 0 (GPIO 14) bergerak menekan saklar lalu kembali ke posisi rest.
  - Jika `switch = 0` dan `state = "OFF"`, Servo 1 (GPIO 27) bergerak menekan saklar lalu kembali ke posisi rest.
- **Response 200 OK:**
  ```json
  { "status": "OK" }
  ```

---

### 3.5 `GET` & `POST /api/relay/polarity` (Alias: `/api/polarity`)
Mengecek atau mengonfigurasi polaritas aktif relay (`activeLow`).

- **Mengecek Polaritas:**
  ```http
  GET /api/relay/polarity
  ```
  Response:
  ```json
  { "status": "OK", "activeLow": true }
  ```
- **Mengubah Polaritas:**
  ```json
  POST /api/relay/polarity
  Content-Type: application/json

  {
    "activeLow": true
  }
  ```
  *(Juga mendukung format URL query: `POST /api/relay/polarity?activeLow=true`)*.
- **Efek Samping:** Seluruh 4 relay langsung dipaksa mati (`OFF`) dan disimpan ke memori flash ESP32 NVS.

---

### 3.5b `POST /api/ac` (Kontrol Remote AC FLiFE / Gree IR)
Mengirimkan perintah infrared ke unit Air Conditioner Gree / FLiFE (YAW1F protocol) melalui GPIO 18. Perintah bersifat idempoten (non-toggle) dan mendukung update parsial.

- **Request (Semua field bersifat opsional, kirimkan yang ingin diubah saja):**
  ```json
  POST /api/ac
  Content-Type: application/json

  {
    "power": true,
    "temp": 24,
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
  ```
  - `power` (boolean): `true` = Hidupkan AC, `false` = Matikan AC.
  - `temp` (integer): Suhu target dalam °C, jangkauan: `16` sampai `30`.
  - `mode` (integer):
    - `0`: AUTO (Otomatis)
    - `1`: COOL (Pendingin / Dingin)
    - `2`: DRY (Dehumidifier / Kering)
    - `3`: FAN (Hanya kipas angin)
    - `4`: HEAT (Pemanas)
  - `fan` (integer):
    - `0`: AUTO (Kecepatan kipas otomatis)
    - `1`: MIN (Kecepatan 1 / Pelan)
    - `2`: MED (Kecepatan 2 / Sedang)
    - `3`: MAX (Kecepatan 3 / Kencang)
  - `swing_v` (boolean): `true` = Swing vertikal bergerak otomatis naik-turun, `false` = Swing berhenti di sudut saat ini.
  - `sleep` (boolean): `true` = Mode tidur aktif (suhu dinaikkan bertahap untuk kenyamanan), `false` = Nonaktif.
  - `turbo` (boolean): `true` = Mode turbo pendinginan maksimal aktif, `false` = Normal.
  - `xfan` (boolean): `true` = Fitur pengering blower evaporator setelah AC mati agar tidak bau/berjamur, `false` = Nonaktif.
  - `light` (boolean): `true` = Layar LED suhu pada unit indoor AC menyala, `false` = Gelap/mati.
  - `ifeel` (boolean): `true` = Mode sensor suhu I-Feel aktif, `false` = Nonaktif.
  - `display_temp` (integer):
    - `0`: OFF (Tidak ada tampilan angka suhu)
    - `1`: SET TEMP (Tampilkan suhu setelan target)
    - `2`: INSIDE TEMP (Tampilkan suhu ruangan terdeteksi)
    - `3`: OUTSIDE TEMP (Tampilkan suhu luar ruangan)
- **Response 200 OK:**
  ```json
  { "status": "OK" }
  ```
- **Response 403 Forbidden (Jika hardware AC dinonaktifkan di config):**
  ```json
  { "status": "Error", "message": "AC Disabled" }
  ```
- **Response 400 Bad Request:**
  ```json
  { "status": "Error", "message": "Bad Payload AC" }
  ```

---

### 3.6 `POST /api/timer` (Unified Timer Engine)
Mengelola seluruh fungsionalitas timer hitung mundur (Add, Update, Control, Delete).

#### 3.6.1 Tambah Timer Baru (`action: "add"` atau `"create"`)
```json
POST /api/timer
Content-Type: application/json

{
  "action": "add",
  "durationSec": 300,
  "invertOnStartEnd": true,
  "targetAction": "OFF",
  "targetAc": 2,
  "targetRelays": [true, false, false, false],
  "targetSwitches": [false, true, false]
}
```
- `durationSec` (integer, detik): Durasi hitung mundur. Contoh 300 = 5 menit.
- `invertOnStartEnd` (boolean):
  - Jika `true`: Pada detik ke-0 (saat timer dibuat/dimulai), perangkat target langsung di-trigger ke status **kebalikan** dari `targetAction` (misal di-ON-kan). Setelah durasi habis, perangkat target di-trigger ke `targetAction` (misal di-OFF-kan).
  - Jika `false`: Perangkat target hanya disentuh saat durasi berakhir.
- `targetAction` (string): `"ON"` atau `"OFF"`. Aksi yang dijalankan ketika countdown mencapai `0`.
- `targetAc` (integer):
  - `0`: Abaikan AC (tidak mengubah status AC).
  - `1`: Targetkan AC Menyala (`Power ON`).
  - `2`: Targetkan AC Mati (`Power OFF`).
- `targetRelays`: Array 4 boolean `[R1, R2, R3, R4]`.
- `targetSwitches`: Array 3 boolean `[SwA, SwB, SwC]`.

Response 200 OK:
```json
{ "status": "OK", "id": 1 }
```

#### 3.6.2 Kontrol Timer: Pause / Resume / Cancel / Start
```json
POST /api/timer
Content-Type: application/json

{
  "action": "pause",
  "id": 1
}
```
Pilihan nilai `action`:
- `"pause"`: Menjeda hitung mundur.
- `"resume"`: Melanjutkan hitung mundur yang dijeda.
- `"cancel"` / `"stop"`: Menghentikan timer (set `active=false`, `finished=true`, tanpa mengeksekusi aksi target).
- `"start"` / `"restart"`: Mengulang timer dari awal sesuai total durasi aslinya.
- `"delete"` / `"remove"`: Menghapus timer dari daftar slot penyimpanan.

Response 200 OK:
```json
{ "status": "OK" }
```

#### 3.6.3 Update Timer yang Ada (`action: "update"` atau `"edit"`)
```json
POST /api/timer
Content-Type: application/json

{
  "action": "update",
  "id": 1,
  "durationSec": 600,
  "invertOnStartEnd": false,
  "targetAction": "ON",
  "targetAc": 1,
  "targetRelays": [true, true, false, false],
  "targetSwitches": [false, false, false]
}
```

---

### 3.7 `GET` & `POST /api/schedules`
Manajemen jadwal otomatis harian berbasis jam NTP (maksimal 10 slot).

- **Membaca Jadwal:**
  ```http
  GET /api/schedules
  ```
  Response:
  ```json
  [
    {
      "h": 6,
      "m": 0,
      "a": "OFF",
      "e": true,
      "targetAc": 2,
      "r": [true, false, false, false],
      "s": [false, false, false]
    },
    {
      "h": 18,
      "m": 30,
      "a": "ON",
      "e": true,
      "targetAc": 1,
      "r": [true, false, false, false],
      "s": [false, false, false]
    }
  ]
  ```

- **Menyimpan / Menimpa Seluruh Jadwal:**
  ```json
  POST /api/schedules
  Content-Type: application/json

  [
    {
      "h": 17,
      "m": 45,
      "a": "ON",
      "e": true,
      "targetAc": 1,
      "r": [true, true, false, false],
      "s": [true, false, false]
    }
  ]
  ```
  *Struktur Object Jadwal:*
  - `h`: Jam (0 - 23 WIB).
  - `m`: Menit (0 - 59).
  - `a`: Target aksi `"ON"` atau `"OFF"`.
  - `e`: Status aktif `true` / `false`.
  - `targetAc`: `0` = Abaikan AC, `1` = Paksa ON, `2` = Paksa OFF.
  - `r`: Array 4 boolean untuk Relay 1..4.
  - `s`: Array 3 boolean untuk Saklar A..C.

---

### 3.8 `POST /api/servo/config`
Kalibrasi sudut derajat servo (0° - 180°) dan durasi tekanan mekanis.

- **Request:**
  ```json
  POST /api/servo/config
  Content-Type: application/json

  {
    "restAngle": 90,
    "pressAngle": 0,
    "pressAngles": [0, 5, 0, 10, 0, 5],
    "pressDurationMs": 400
  }
  ```
  - `restAngle` (0..180): Posisi netral mengambang (saat diubah, seluruh 6 servo otomatis diselaraskan ke posisi ini lalu di-detach).
  - `pressAngle` (0..180): Posisi tekan umum.
  - `pressAngles`: Array 6 sudut tekan spesifik untuk Servo 0 sampai Servo 5.
  - `pressDurationMs`: Lama waktu menahan saklar (milidetik).

---

### 3.9 `POST /api/servo/test`
Menjalankan pengujian mekanis servo (Self-Test non-blocking).

- **Menguji Seluruh 6 Servo Secara Berurutan:**
  ```json
  POST /api/servo/test
  ```
- **Menguji Satu Servo Spesifik (0..5):**
  ```json
  POST /api/servo/test
  Content-Type: application/json

  {
    "servo": 2
  }
  ```
  *(Juga dapat dipanggil via query string: `POST /api/servo/test?servo=2`)*.

---

### 3.10 `POST /api/display`
Mengubah halaman yang ditampilkan pada layar OLED 128x64 I2C.

- **Request:**
  ```json
  POST /api/display
  Content-Type: application/json

  {
    "page": 0
  }
  ```
  - `page`:
    - `0`: Tampilan status IP, WiFi, Relay 1..4, dan Saklar A..C.
    - `1`: Tampilan jadwal harian yang sedang aktif.
    - `2`: Tampilan daftar timer aktif / hitung mundur.
    - `3`: Tampilan status Remote AC FLiFE (Power, Suhu, Mode, Fan).
  - *Catatan:* Jika `page` tidak disertakan, display otomatis berpindah ke halaman berikutnya (siklus `0 -> 1 -> 2 -> 3 -> 0`). Jika AC dinonaktifkan, siklus hanya 3 halaman (`0..2`).

---

### 3.11 `GET` & `POST /api/hardware/config`
Menonaktifkan / mengaktifkan channel hardware tertentu secara permanen (disimpan ke NVS).

- **Membaca Konfigurasi Hardware:**
  ```http
  GET /api/hardware/config
  ```
  Response:
  ```json
  {
    "relays": [true, true, true, true],
    "switches": [true, true, true],
    "ac": true
  }
  ```
- **Mengubah Konfigurasi Hardware:**
  ```json
  POST /api/hardware/config
  Content-Type: application/json

  {
    "relays": [true, true, true, false],
    "switches": [true, true, true],
    "ac": true
  }
  ```
  *Catatan:*
  - Jika suatu channel relay dinonaktifkan (`false`), output GPIO relay tersebut langsung dipaksa mati (`OFF`) dan tidak akan merespons perintah kontrol relay apapun.
  - Jika `ac: false`, pemanggilan `/api/ac` akan mengembalikan kode HTTP `403 Forbidden` (`AC Disabled`).

---

### 3.12 `POST /api/wifi/reset`
Mereset seluruh SSID & password Wi-Fi yang tersimpan di WiFiManager dan me-reboot ESP32 ke mode Access Point captive portal (`SSID: R-Sync`).

- **Request:**
  ```http
  POST /api/wifi/reset
  ```
- **Response 200 OK:**
  ```json
  { "status": "OK", "message": "Resetting WiFi..." }
  ```

---

## 4. Definisi Function Calling Schema (Format OpenAI / Ollama JSON Tools)

Salin definisi JSON Schema di bawah ini ke dalam konfigurasi `tools` pada local LLM framework kamu (misal: LangChain, LlamaIndex, Ollama API, Ollama Modelfile, vLLM, LM Studio, LiteLLM):

```json
[
  {
    "type": "function",
    "function": {
      "name": "get_device_status",
      "description": "Ambil status operasional lengkap ESP32 R-Sync secara real-time, meliputi status Relay 1..4 (ON/OFF), Saklar Tembok A..C (ON/OFF), waktu NTP saat ini, timer aktif, jadwal aktif, tegangan rail servo, dan koneksi WiFi.",
      "parameters": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "get_device_capabilities",
      "description": "Ambil informasi fitur dan spesifikasi hardware yang didukung oleh ESP32 (jumlah relay, jumlah switch, ketersediaan OLED, kapasitas timer, dll).",
      "parameters": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "control_relay",
      "description": "Kontrol status on/off channel relay listrik AC/DC (Channel 1 sampai 4).",
      "parameters": {
        "type": "object",
        "properties": {
          "channel": {
            "type": "integer",
            "enum": [1, 2, 3, 4],
            "description": "Nomor relay fisik yang ingin dikontrol. Nilai: 1, 2, 3, atau 4 (1-indexed)."
          },
          "state": {
            "type": "string",
            "enum": ["ON", "OFF"],
            "description": "Status target yang diinginkan: 'ON' untuk menyalakan, 'OFF' untuk mematikan."
          }
        },
        "required": ["channel", "state"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "control_wall_switch",
      "description": "Kontrol saklar mekanis dinding A, B, atau C dengan menggerakkan motor servo penekan fisik.",
      "parameters": {
        "type": "object",
        "properties": {
          "switch_index": {
            "type": "integer",
            "enum": [0, 1, 2],
            "description": "Index saklar dinding yang ingin dikontrol. 0 untuk Saklar A, 1 untuk Saklar B, 2 untuk Saklar C (0-indexed)."
          },
          "state": {
            "type": "string",
            "enum": ["ON", "OFF"],
            "description": "Status target: 'ON' untuk menekan saklar hidup, 'OFF' untuk menekan saklar mati."
          }
        },
        "required": ["switch_index", "state"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "control_ac",
      "description": "Kontrol unit Air Conditioner (AC) FLiFE / Gree melalui transmisi sinyal Infrared (IR). Perintah bersifat non-toggle (aman dan idempoten): mengirim power=false saat AC mati tidak akan menyalakan AC. Menerima update parsial (parameter yang tidak disertakan akan mempertahankan nilai sebelumnya yang tersimpan di flash ESP32).",
      "parameters": {
        "type": "object",
        "properties": {
          "power": {
            "type": "boolean",
            "description": "Nyalakan (true) atau matikan (false) AC. Bersifat idempoten (bukan toggle)."
          },
          "temp": {
            "type": "integer",
            "minimum": 16,
            "maximum": 30,
            "description": "Target suhu AC dalam derajat Celsius (16 - 30 °C)."
          },
          "mode": {
            "type": "integer",
            "enum": [0, 1, 2, 3, 4],
            "description": "Mode operasi AC: 0 = AUTO (Otomatis), 1 = COOL (Pendingin/Dingin), 2 = DRY (Pengering Kelembaban), 3 = FAN (Hanya Kipas), 4 = HEAT (Pemanas)."
          },
          "fan": {
            "type": "integer",
            "enum": [0, 1, 2, 3],
            "description": "Kecepatan hembusan kipas angin: 0 = AUTO (Otomatis), 1 = MIN (Kecepatan 1/Lembut), 2 = MED (Kecepatan 2/Sedang), 3 = MAX (Kecepatan 3/Kencang)."
          },
          "swing_v": {
            "type": "boolean",
            "description": "Swing sirip vertikal: true = Sirip bergerak naik-turun otomatis, false = Sirip diam pada posisi saat ini."
          },
          "turbo": {
            "type": "boolean",
            "description": "Mode Turbo pendinginan instan berkecepatan ekstra tinggi: true = Aktif, false = Nonaktif."
          },
          "sleep": {
            "type": "boolean",
            "description": "Mode Sleep / Kenyamanan tidur (menyesuaikan suhu bertahap di malam hari): true = Aktif, false = Nonaktif."
          },
          "xfan": {
            "type": "boolean",
            "description": "Mode X-Fan (Blow/Pengering internal): meniupkan udara setelah AC dimatikan untuk mencegah jamur pada evaporator: true = Aktif, false = Nonaktif."
          },
          "light": {
            "type": "boolean",
            "description": "Lampu LED layar display indikator suhu pada unit indoor AC: true = Menyala, false = Mati (gelap)."
          },
          "ifeel": {
            "type": "boolean",
            "description": "Mode I-Feel (sensor suhu remote control): true = Aktif, false = Nonaktif."
          },
          "display_temp": {
            "type": "integer",
            "enum": [0, 1, 2, 3],
            "description": "Pilihan informasi suhu yang ditampilkan di layar LED AC: 0 = OFF (Mati), 1 = SET (Suhu Target/Setelan), 2 = INSIDE (Suhu Ruangan Saat Ini), 3 = OUTSIDE (Suhu Luar Ruangan)."
          }
        },
        "required": []
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "create_countdown_timer",
      "description": "Buat pewaktu hitung mundur (countdown timer) untuk mematikan atau menyalakan relay/saklar/AC setelah durasi tertentu.",
      "parameters": {
        "type": "object",
        "properties": {
          "duration_sec": {
            "type": "integer",
            "minimum": 1,
            "description": "Durasi hitung mundur dalam satuan detik (contoh: 60 untuk 1 menit, 300 untuk 5 menit, 3600 untuk 1 jam)."
          },
          "target_action": {
            "type": "string",
            "enum": ["ON", "OFF"],
            "description": "Aksi yang dieksekusi saat hitungan mundur selesai."
          },
          "target_ac": {
            "type": "integer",
            "enum": [0, 1, 2],
            "description": "Kontrol AC saat timer berakhir: 0 = Abaikan AC, 1 = Nyalakan AC (Power ON), 2 = Matikan AC (Power OFF). Bersifat non-toggle."
          },
          "invert_on_start_end": {
            "type": "boolean",
            "description": "Jika true, perangkat target akan langsung dinyalakan/dimatikan dengan status kebalikan saat timer dimulai, lalu diubah ke target_action saat timer selesai. Contoh: menyalakan lampu sekarang dan otomatis mematikannya 10 menit lagi."
          },
          "target_relays": {
            "type": "array",
            "items": { "type": "boolean" },
            "minItems": 4,
            "maxItems": 4,
            "description": "Array 4 boolean [Relay1, Relay2, Relay3, Relay4]. True jika relay tersebut menjadi target timer."
          },
          "target_switches": {
            "type": "array",
            "items": { "type": "boolean" },
            "minItems": 3,
            "maxItems": 3,
            "description": "Array 3 boolean [SwitchA, SwitchB, SwitchC]. True jika saklar dinding tersebut menjadi target timer."
          }
        },
        "required": ["duration_sec", "target_action", "target_relays", "target_switches"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "manage_countdown_timer",
      "description": "Mengelola status timer yang sedang berjalan atau mengedit/menghapus timer berdasarkan ID timer.",
      "parameters": {
        "type": "object",
        "properties": {
          "timer_id": {
            "type": "integer",
            "description": "ID unik timer yang diperoleh dari status atau saat pembuatan."
          },
          "action": {
            "type": "string",
            "enum": ["pause", "resume", "cancel", "start", "delete"],
            "description": "Aksi manajemen timer: 'pause' (jeda), 'resume' (lanjut), 'cancel' (hentikan), 'start' (ulang dari awal), 'delete' (hapus slot)."
          }
        },
        "required": ["timer_id", "action"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "get_schedules",
      "description": "Membaca seluruh 10 slot jadwal harian otomatis yang tersimpan di memori ESP32.",
      "parameters": {
        "type": "object",
        "properties": {},
        "required": []
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "set_schedules",
      "description": "Menyimpan atau menimpa daftar jadwal harian otomatis pada ESP32 (maksimal 10 jadwal).",
      "parameters": {
        "type": "object",
        "properties": {
          "schedules": {
            "type": "array",
            "description": "Daftar objek jadwal harian.",
            "items": {
              "type": "object",
              "properties": {
                "h": { "type": "integer", "minimum": 0, "maximum": 23, "description": "Jam (0-23 WIB)" },
                "m": { "type": "integer", "minimum": 0, "maximum": 59, "description": "Menit (0-59)" },
                "a": { "type": "string", "enum": ["ON", "OFF"], "description": "Aksi target saat jadwal tercapai" },
                "e": { "type": "boolean", "description": "Status aktif jadwal (true = aktif, false = dinonaktifkan)" },
                "target_ac": {
                  "type": "integer",
                  "enum": [0, 1, 2],
                  "description": "Aksi target AC: 0 = Abaikan, 1 = Nyalakan AC (Power ON), 2 = Matikan AC (Power OFF)"
                },
                "r": {
                  "type": "array",
                  "items": { "type": "boolean" },
                  "minItems": 4,
                  "maxItems": 4,
                  "description": "Target Relay [R1, R2, R3, R4]"
                },
                "s": {
                  "type": "array",
                  "items": { "type": "boolean" },
                  "minItems": 3,
                  "maxItems": 3,
                  "description": "Target Saklar [SwA, SwB, SwC]"
                }
              },
              "required": ["h", "m", "a", "e", "r", "s"]
            }
          }
        },
        "required": ["schedules"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "set_display_page",
      "description": "Mengatur halaman tampilan layar OLED I2C 128x64 pada ESP32.",
      "parameters": {
        "type": "object",
        "properties": {
          "page": {
            "type": "integer",
            "enum": [0, 1, 2, 3],
            "description": "Nomor halaman: 0 = Status Ringkas, 1 = Jadwal Harian, 2 = Timer Aktif, 3 = Remote AC."
          }
        },
        "required": ["page"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "test_servo_hardware",
      "description": "Menjalankan uji coba mandiri pergerakan servo fisik (self-test) untuk memastikan tidak ada servo yang macet.",
      "parameters": {
        "type": "object",
        "properties": {
          "servo_index": {
            "type": "integer",
            "enum": [-1, 0, 1, 2, 3, 4, 5],
            "description": "Index servo yang ingin diuji (0..5). Berikan nilai -1 untuk menguji seluruh 6 servo secara berurutan."
          }
        },
        "required": ["servo_index"]
      }
    }
  },
  {
    "type": "function",
    "function": {
      "name": "set_relay_polarity",
      "description": "Mengubah mode logika polaritas relay (Active LOW vs Active HIGH). PERHATIAN: Perintah ini akan mematikan seluruh relay seketika demi keamanan.",
      "parameters": {
        "type": "object",
        "properties": {
          "active_low": {
            "type": "boolean",
            "description": "True untuk Active LOW (standar modul relay optocoupler), False untuk Active HIGH."
          }
        },
        "required": ["active_low"]
      }
    }
  }
]
```

---

## 5. Implementasi Tool Handler (Python Contoh untuk Local LLM)

Berikut adalah implementasi fungsi eksekutor Python yang menjembatani Tool Call dari LLM (misalnya dari library `openai`, `ollama`, atau `langchain`) ke REST API ESP32:

```python
import requests
from typing import List, Optional, Dict, Any

ESP32_BASE_URL = "http://192.168.1.50"  # Sesuaikan dengan IP lokal ESP32 kamu
TIMEOUT_SEC = 5.0

def get_device_status() -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/status"
    resp = requests.get(url, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def get_device_capabilities() -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/capabilities"
    resp = requests.get(url, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def control_relay(channel: int, state: str) -> Dict[str, Any]:
    assert channel in [1, 2, 3, 4], "Channel relay harus antara 1 sampai 4"
    assert state in ["ON", "OFF"], "State harus ON atau OFF"
    url = f"{ESP32_BASE_URL}/api/relay"
    payload = {"channel": channel, "state": state}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def control_wall_switch(switch_index: int, state: str) -> Dict[str, Any]:
    assert switch_index in [0, 1, 2], "Switch index harus 0 (A), 1 (B), atau 2 (C)"
    assert state in ["ON", "OFF"], "State harus ON atau OFF"
    url = f"{ESP32_BASE_URL}/api/switch"
    payload = {"switch": switch_index, "state": state}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def control_ac(
    power: Optional[bool] = None,
    temp: Optional[int] = None,
    mode: Optional[int] = None,
    fan: Optional[int] = None,
    swing_v: Optional[bool] = None,
    sleep: Optional[bool] = None,
    turbo: Optional[bool] = None,
    xfan: Optional[bool] = None,
    light: Optional[bool] = None,
    ifeel: Optional[bool] = None,
    display_temp: Optional[int] = None
) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/ac"
    payload: Dict[str, Any] = {}
    if power is not None: payload["power"] = power
    if temp is not None:
        assert 16 <= temp <= 30, "Suhu harus antara 16 sampai 30 °C"
        payload["temp"] = temp
    if mode is not None:
        assert mode in [0, 1, 2, 3, 4], "Mode: 0=AUTO, 1=COOL, 2=DRY, 3=FAN, 4=HEAT"
        payload["mode"] = mode
    if fan is not None:
        assert fan in [0, 1, 2, 3], "Fan: 0=AUTO, 1=MIN, 2=MED, 3=MAX"
        payload["fan"] = fan
    if swing_v is not None: payload["swing_v"] = swing_v
    if sleep is not None: payload["sleep"] = sleep
    if turbo is not None: payload["turbo"] = turbo
    if xfan is not None: payload["xfan"] = xfan
    if light is not None: payload["light"] = light
    if ifeel is not None: payload["ifeel"] = ifeel
    if display_temp is not None:
        assert display_temp in [0, 1, 2, 3], "Display temp: 0=OFF, 1=SET, 2=INSIDE, 3=OUTSIDE"
        payload["display_temp"] = display_temp
    
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def create_countdown_timer(
    duration_sec: int,
    target_action: str,
    target_relays: List[bool],
    target_switches: List[bool],
    target_ac: int = 0,
    invert_on_start_end: bool = False
) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/timer"
    payload = {
        "action": "add",
        "durationSec": duration_sec,
        "invertOnStartEnd": invert_on_start_end,
        "targetAction": target_action,
        "targetAc": target_ac,
        "targetRelays": target_relays,
        "targetSwitches": target_switches
    }
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def manage_countdown_timer(timer_id: int, action: str) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/timer"
    payload = {"id": timer_id, "action": action}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def get_schedules() -> List[Dict[str, Any]]:
    url = f"{ESP32_BASE_URL}/api/schedules"
    resp = requests.get(url, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def set_schedules(schedules: List[Dict[str, Any]]) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/schedules"
    resp = requests.post(url, json=schedules, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def set_display_page(page: int) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/display"
    payload = {"page": page}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def test_servo_hardware(servo_index: int = -1) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/servo/test"
    payload = {} if servo_index < 0 else {"servo": servo_index}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()

def set_relay_polarity(active_low: bool) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/relay/polarity"
    payload = {"activeLow": active_low}
    resp = requests.post(url, json=payload, timeout=TIMEOUT_SEC)
    resp.raise_for_status()
    return resp.json()
```

---

## 6. Format Definisi Skill Agen (Hermes Agent / OpenClaw `SKILL.md`)

Bagi pengguna framework **Hermes Agent** (Nous Research) atau **OpenClaw Agent**, simpan blok markdown di bawah ini sebagai file `skills/r_sync_controller/SKILL.md` agar agen langsung memahami seluruh cara mengoperasikan perangkat R-Sync secara otomatis:

```markdown
---
name: r_sync_smart_controller
description: "Kontrol cerdas perangkat IoT ESP32 R-Sync: 4 Relay Listrik, 3 Saklar Tembok Mekanis Servo, Remote AC IR FLiFE/Gree, Countdown Timer, dan Penjadwalan NTP."
---

# R-Sync Controller Skill

Gunakan skill ini ketika pengguna meminta untuk:
1. Menyalakan atau mematikan lampu, stopkontak, atau pompa (Relay 1..4).
2. Menekan saklar dinding fisik A, B, atau C menggunakan motor servo.
3. Mengontrol AC FLiFE / Gree (mengatur suhu 16-30°C, mode COOL/AUTO/DRY/FAN/HEAT, fan speed, swing, turbo, sleep, atau mematikan AC).
4. Menyetel timer hitung mundur untuk mematikan perangkat setelah durasi tertentu.
5. Menyetel jadwal otomatis berbasis jam WIB harian.
6. Memeriksa status real-time atau mengubah halaman display OLED.

## Aturan Perilaku AI:
- **Idempotensi AC:** Kontrol AC menggunakan paket IR non-toggle. Mengirim perintah `power: false` ketika AC sudah mati adalah 100% aman dan tidak akan menyalakan AC secara tidak sengaja.
- **Dukungan Partial State:** Anda tidak perlu menyertakan semua parameter AC saat memanggil `control_ac`. Cukup sertakan parameter yang diminta pengguna (misal hanya `{"temp": 24}`).
- **Keselamatan Indeks:**
  - Relay: 1..4 (1-indexed)
  - Saklar dinding: 0..2 (0 = Saklar A, 1 = Saklar B, 2 = Saklar C)
  - target_ac pada timer/jadwal: 0 = Jangan sentuh AC, 1 = ON, 2 = OFF
- **Cek Status Dulu jika Ragu:** Jika pengguna bertanya apakah suatu lampu atau AC sedang hidup, panggil `get_device_status` terlebih dahulu sebelum menjawab.
```

---

## 7. Skenario Prompting & Pola Pikir LLM (Few-Shot Examples)

Berikan contoh-contoh berikut pada System Prompt atau konteks percakapan agar LLM tidak salah memilih tool:

### Contoh Skenario 1: Menyalakan Lampu Utama (Relay)
- **User:** "Tolong nyalakan lampu utama di Relay 1 dong."
- **LLM Reasoning:** User meminta menyalakan Relay 1. Sesuai aturan, nomor relay adalah 1 (1-indexed) dan target state adalah "ON".
- **Tool Call:**
  ```json
  {
    "name": "control_relay",
    "arguments": {
      "channel": 1,
      "state": "ON"
    }
  }
  ```

---

### Contoh Skenario 2: Mengatur Suhu dan Mode AC
- **User:** "Dinginkan kamar tidur, tolong setel AC ke 22 derajat mode Cool dan fan kencang ya."
- **LLM Reasoning:**
  1. Pengguna ingin menyalakan / mengontrol AC kamar.
  2. Suhu: 22°C (`temp: 22`).
  3. Mode: Cool (`mode: 1`).
  4. Fan: Kencang / Max (`fan: 3`).
  5. Pastikan power aktif (`power: true`).
- **Tool Call:**
  ```json
  {
    "name": "control_ac",
    "arguments": {
      "power": true,
      "temp": 22,
      "mode": 1,
      "fan": 3
    }
  }
  ```

---

### Contoh Skenario 3: Mematikan AC Kamar secara Idempoten
- **User:** "Matikan AC kamar sekarang."
- **LLM Reasoning:** User meminta mematikan AC. Cukup kirim `power: false`. Ini bersifat non-toggle dan tidak akan mengaktifkan AC kembali jika sudah mati.
- **Tool Call:**
  ```json
  {
    "name": "control_ac",
    "arguments": {
      "power": false
    }
  }
  ```

---

### Contoh Skenario 4: Persiapan Tidur (Multi-Target: Saklar + AC + Countdown Timer)
- **User:** "Saya mau tidur. Matikan saklar lampu kamar (Saklar A), setel AC 25 derajat mode sleep, dan tolong matikan AC otomatis dalam 6 jam."
- **LLM Reasoning:**
  1. Matikan Saklar A -> `control_wall_switch(switch_index=0, state="OFF")`.
  2. Setel AC 25°C mode Sleep -> `control_ac(power=True, temp=25, sleep=True)`.
  3. Countdown timer 6 jam (6 × 3600 = 21600 detik) untuk mematikan AC (`target_ac: 2`, `target_action: "OFF"`, `target_relays: [false, false, false, false]`, `target_switches: [false, false, false]`).
- **Tool Calls Berurutan:**
  ```json
  {
    "name": "control_wall_switch",
    "arguments": { "switch_index": 0, "state": "OFF" }
  }
  ```
  ```json
  {
    "name": "control_ac",
    "arguments": { "power": true, "temp": 25, "sleep": true }
  }
  ```
  ```json
  {
    "name": "create_countdown_timer",
    "arguments": {
      "duration_sec": 21600,
      "target_action": "OFF",
      "target_ac": 2,
      "target_relays": [false, false, false, false],
      "target_switches": [false, false, false]
    }
  }
  ```

---

### Contoh Skenario 5: Menyalakan Pompa Selama 15 Menit Lalu Mati Otomatis
- **User:** "Nyalakan pompa air (Relay 3) selama 15 menit dari sekarang, habis itu matikan."
- **LLM Reasoning:**
  1. Durasi 15 menit = 15 × 60 = 900 detik.
  2. Beban berada di Relay 3.
  3. Menginginkan pompa hidup sekarang dan mati saat selesai -> gunakan `invert_on_start_end = true` dengan `target_action = "OFF"`.
  4. Array relay: `[false, false, true, false]`. Array saklar: `[false, false, false]`. `target_ac: 0` (abaikan AC).
- **Tool Call:**
  ```json
  {
    "name": "create_countdown_timer",
    "arguments": {
      "duration_sec": 900,
      "invert_on_start_end": true,
      "target_action": "OFF",
      "target_ac": 0,
      "target_relays": [false, false, true, false],
      "target_switches": [false, false, false]
    }
  }
  ```

---

### Contoh Skenario 6: Cek Kondisi Sebelum Bertindak
- **User:** "Kira-kira lampu teras (Relay 2) lagi nyala atau mati sekarang?"
- **LLM Reasoning:** Saya perlu mengetahui status relay saat ini terlebih dahulu sebelum memberikan jawaban akurat ke pengguna.
- **Tool Call:**
  ```json
  {
    "name": "get_device_status",
    "arguments": {}
  }
  ```
- **Setelah menerima response status:**
  ```json
  { "relays": ["OFF", "ON", "OFF", "OFF"] }
  ```
- **Jawaban Akhir LLM ke User:** "Lampu teras pada Relay 2 saat ini dalam keadaan menyala (ON)."

---

### Contoh Skenario 7: Menyetel Jadwal AC Harian Otomatis
- **User:** "Atur AC menyala otomatis setiap jam 21:00 malam dan mati jam 05:00 pagi setiap hari."
- **LLM Reasoning:**
  1. Jam 21:00 WIB -> `h: 21, m: 0, a: "ON", e: true, target_ac: 1, r: [false, false, false, false], s: [false, false, false]`.
  2. Jam 05:00 WIB -> `h: 5, m: 0, a: "OFF", e: true, target_ac: 2, r: [false, false, false, false], s: [false, false, false]`.
- **Tool Call:**
  ```json
  {
    "name": "set_schedules",
    "arguments": {
      "schedules": [
        {
          "h": 21,
          "m": 0,
          "a": "ON",
          "e": true,
          "target_ac": 1,
          "r": [false, false, false, false],
          "s": [false, false, false]
        },
        {
          "h": 5,
          "m": 0,
          "a": "OFF",
          "e": true,
          "target_ac": 2,
          "r": [false, false, false, false],
          "s": [false, false, false]
        }
      ]
    }
  }
  ```

---

## 8. Penanganan Masalah & Safety Guardrails untuk AI

1. **AC Disabled (Error 403 Forbidden):**
   - Jika pemanggilan `POST /api/ac` mengembalikan respons `403 Forbidden` (`{"status":"Error","message":"AC Disabled"}`), artinya hardware AC IR dinonaktifkan di konfigurasi hardware perangkat. Informasikan ke pengguna bahwa port hardware AC sedang dimatikan.
2. **Karakteristik Open-Loop Transmisi IR:**
   - Sinyal infrared tidak memiliki kanal komunikasi balik dari unit AC fisik ke ESP32. Status AC di `/api/status` mencerminkan *last commanded state* yang berhasil dikirimkan oleh ESP32 dan disimpan ke memori flash.
3. **Safety Indexing `target_ac`:**
   - Saat membuat timer atau jadwal yang hanya menargetkan relay atau saklar tembok, pastikan selalu mengisi `target_ac: 0` agar status AC pengguna tidak ikut berubah secara tidak disengaja.
4. **Servo Sedang Sibuk (`servoBusy: true`):**
   - Jika saat pemanggilan status didapati `servoBusy == true` atau `servoQueueLength > 5`, LLM sebaiknya menunda pengiriman perintah servo beruntun agar tidak membebani antrean FIFO.
5. **Channel Hardware Nonaktif:**
   - Cek `relayActive` dan `switchActive` dari status. Jika channel diminta user sedang dinonaktifkan (`false`), beri tahu user bahwa channel tersebut dinonaktifkan secara konfigurasi hardware sebelum mencoba mengeksekusinya.
6. **Waktu NTP Belum Sinkron (`time: "Not Synced"`):**
   - Jika waktu NTP belum sinkron, fungsi timer countdown tetap bekerja normal dengan millis internal, namun penjadwalan jam harian (`schedules`) tidak akan tertrigger sampai ESP32 berhasil memperoleh waktu dari internet.
7. **Validasi Range Ketat:**
   - Selalu validasi agar `channel` relay berada di range 1..4 (bukan 0..3).
   - Selalu validasi agar `switch` index berada di range 0..2 (bukan 1..3).
   - Selalu pastikan durasi timer bernilai positif integer dalam detik.
