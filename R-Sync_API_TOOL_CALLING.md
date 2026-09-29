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

> ⚠️ **PERINGATAN KRITIS INDEXING UNTUK LLM:**
> - Endpoint `/api/relay` menggunakan `channel` bernilai **1 sampai 4** (1-based).
> - Endpoint `/api/switch` menggunakan `switch` bernilai **0 sampai 2** (0-based: 0 = Saklar A, 1 = Saklar B, 2 = Saklar C).
> - Pada payload array `targetRelays`, selalu berupa array 4 boolean: `[Relay1, Relay2, Relay3, Relay4]`.
> - Pada payload array `targetSwitches`, selalu berupa array 3 boolean: `[SwitchA, SwitchB, SwitchC]`.

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
| `GET`/`POST` | `/api/relay/polarity` | Cek atau ubah polaritas relay | Query/Body: `{"activeLow": boolean}` |
| `POST` | `/api/timer` | Operasi lengkap timer (Add/Update/Control) | `action`, `durationSec`, `id`, dll |
| `POST` | `/api/timer/add` | (Alias) Tambah timer hitung mundur | `durationSec`, `targetAction`, `targetRelays`, `targetSwitches` |
| `POST` | `/api/timer/update` | (Alias) Edit timer yang sudah ada | `id`, `durationSec`, dll |
| `POST` | `/api/timer/control` | (Alias) Jeda/Lanjut/Hentikan timer | `{"id": int, "command": "pause"\|"resume"\|"cancel"\|"start"}` |
| `GET` | `/api/schedules` | Ambil seluruh 10 slot jadwal | Tidak ada |
| `POST` | `/api/schedules` | Setel / timpa daftar 10 jadwal | Array JSON jadwal `[{h, m, a, e, r, s}, ...]` |
| `POST` | `/api/servo/config` | Kalibrasi sudut rest & tekan servo | `restAngle`, `pressAngle`, `pressAngles`, `pressDurationMs` |
| `POST` | `/api/servo/test` | Uji coba fisik servo (*Self-Test*) | Query/Body: `{"servo": 0..5}` (opsional) |
| `POST` | `/api/display` | Ganti halaman tampilan layar OLED | `{"page": 0..2}` (opsional) |
| `GET` | `/api/hardware/config` | Cek channel relay/saklar yang aktif | Tidak ada |
| `POST` | `/api/hardware/config` | Aktifkan/nonaktifkan channel fisik | `{"relays": [4 bool], "switches": [3 bool]}` |
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
        "r": [true, true, false, false],
        "s": [false, false, false]
      }
    ]
  }
  ```
  *Keterangan Field Kritis:*
  - `time`: Jika bernilai `"Not Synced"`, NTP belum mendapatkan jam akurat.
  - `servoAngles`: Nilai `-1` menandakan servo dalam kondisi **detached** (mati / tanpa torsi idle).
  - `servoBusy`: `true` jika servo sedang bergerak, menjalankan self-test, atau menyelaraskan sudut.

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
  "targetRelays": [true, false, false, false],
  "targetSwitches": [false, true, false]
}
```
- `durationSec` (integer, detik): Durasi hitung mundur. Contoh 300 = 5 menit.
- `invertOnStartEnd` (boolean):
  - Jika `true`: Pada detik ke-0 (saat timer dibuat/dimulai), perangkat target langsung di-trigger ke status **kebalikan** dari `targetAction` (misal di-ON-kan). Setelah durasi habis, perangkat target di-trigger ke `targetAction` (misal di-OFF-kan).
  - Jika `false`: Perangkat target hanya disentuh saat durasi berakhir.
- `targetAction` (string): `"ON"` atau `"OFF"`. Aksi yang dijalankan ketika countdown mencapai `0`.
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
      "r": [true, false, false, false],
      "s": [false, false, false]
    },
    {
      "h": 18,
      "m": 30,
      "a": "ON",
      "e": true,
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
      "r": [true, true, false, false],
      "s": [true, false, false]
    }
  ]
  ```
  *Struktur Object Jadwal:*
  - `h`: Jam (0 - 23).
  - `m`: Menit (0 - 59).
  - `a`: Target aksi `"ON"` atau `"OFF"`.
  - `e`: Status aktif `true` / `false`.
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
  - *Catatan:* Jika `page` tidak disertakan, display otomatis berpindah ke halaman berikutnya (siklus `0 -> 1 -> 2 -> 0`).

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
    "switches": [true, true, true]
  }
  ```
- **Mengubah Konfigurasi Hardware:**
  ```json
  POST /api/hardware/config
  Content-Type: application/json

  {
    "relays": [true, true, true, false],
    "switches": [true, true, true]
  }
  ```
  *Catatan:* Jika suatu channel relay dinonaktifkan (`false`), output GPIO relay tersebut langsung dipaksa mati (`OFF`) dan tidak akan merespons perintah kontrol relay apapun.

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
      "name": "create_countdown_timer",
      "description": "Buat pewaktu hitung mundur (countdown timer) untuk mematikan atau menyalakan relay/saklar setelah durasi tertentu.",
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
            "enum": [0, 1, 2],
            "description": "Nomor halaman: 0 = Status Ringkas, 1 = Jadwal Harian, 2 = Timer Aktif."
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

def create_countdown_timer(
    duration_sec: int,
    target_action: str,
    target_relays: List[bool],
    target_switches: List[bool],
    invert_on_start_end: bool = False
) -> Dict[str, Any]:
    url = f"{ESP32_BASE_URL}/api/timer"
    payload = {
        "action": "add",
        "durationSec": duration_sec,
        "invertOnStartEnd": invert_on_start_end,
        "targetAction": target_action,
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

## 6. Skenario Prompting & Pola Pikir LLM (Few-Shot Examples)

Berikan contoh-contoh berikut pada System Prompt atau konteks percakapan agar LLM tidak salah memilih tool:

### Contoh Skenario 1: Menyalakan Lampu Utama
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

### Contoh Skenario 2: Menyalakan Pompa Selama 15 Menit Lalu Mati Otomatis
- **User:** "Nyalakan pompa air (Relay 3) selama 15 menit dari sekarang, habis itu matikan."
- **LLM Reasoning:**
  1. Durasi 15 menit = 15 × 60 = 900 detik.
  2. Beban berada di Relay 3.
  3. Menginginkan pompa hidup sekarang dan mati saat selesai -> gunakan `invert_on_start_end = true` dengan `target_action = "OFF"`.
  4. Array relay: `[false, false, true, false]`. Array saklar: `[false, false, false]`.
- **Tool Call:**
  ```json
  {
    "name": "create_countdown_timer",
    "arguments": {
      "duration_sec": 900,
      "invert_on_start_end": true,
      "target_action": "OFF",
      "target_relays": [false, false, true, false],
      "target_switches": [false, false, false]
    }
  }
  ```

---

### Contoh Skenario 3: Mematikan Saklar Dinding Kamar Tidur
- **User:** "Matikan saklar dinding kamar (Saklar B)."
- **LLM Reasoning:** Saklar B memiliki index `1` (karena Saklar A = 0, B = 1, C = 2). State yang diinginkan adalah "OFF".
- **Tool Call:**
  ```json
  {
    "name": "control_wall_switch",
    "arguments": {
      "switch_index": 1,
      "state": "OFF"
    }
  }
  ```

---

### Contoh Skenario 4: Cek Kondisi Sebelum Bertindak
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

### Contoh Skenario 5: Menyetel Jadwal Lampu Taman Setiap Malam
- **User:** "Atur lampu taman di Relay 4 agar menyala otomatis jam 18:00 dan mati jam 06:00 pagi setiap hari."
- **LLM Reasoning:**
  1. Jam 18:00 WIB -> `h: 18, m: 0, a: "ON", e: true, r: [false, false, false, true], s: [false, false, false]`.
  2. Jam 06:00 WIB -> `h: 6, m: 0, a: "OFF", e: true, r: [false, false, false, true], s: [false, false, false]`.
  3. Simpan kedua entri ini ke daftar jadwal.
- **Tool Call:**
  ```json
  {
    "name": "set_schedules",
    "arguments": {
      "schedules": [
        {
          "h": 18,
          "m": 0,
          "a": "ON",
          "e": true,
          "r": [false, false, false, true],
          "s": [false, false, false]
        },
        {
          "h": 6,
          "m": 0,
          "a": "OFF",
          "e": true,
          "r": [false, false, false, true],
          "s": [false, false, false]
        }
      ]
    }
  }
  ```

---

## 7. Penanganan Masalah & Safety Guardrails untuk AI

1. **Servo Sedang Sibuk (`servoBusy: true`):**
   - Jika saat pemanggilan status didapati `servoBusy == true` atau `servoQueueLength > 5`, LLM sebaiknya menunda pengiriman perintah servo beruntun agar tidak membebani antrean FIFO.
2. **Channel Hardware Nonaktif:**
   - Cek `relayActive` dan `switchActive` dari status. Jika channel diminta user sedang dinonaktifkan (`false`), beri tahu user bahwa channel tersebut dinonaktifkan secara konfigurasi hardware sebelum mencoba mengeksekusinya.
3. **Waktu NTP Belum Sinkron (`time: "Not Synced"`):**
   - Jika waktu NTP belum sinkron, fungsi timer countdown tetap bekerja normal dengan millis internal, namun penjadwalan jam harian (`schedules`) tidak akan tertrigger sampai ESP32 berhasil memperoleh waktu dari internet.
4. **Validasi Range Ketat:**
   - Selalu validasi agar `channel` relay berada di range 1..4 (bukan 0..3).
   - Selalu validasi agar `switch` index berada di range 0..2 (bukan 1..3).
   - Selalu pastikan durasi timer bernilai positif integer dalam detik.
