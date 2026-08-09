# TemperaturWatch – REST API Reference

Complete reference for all `/api/...` endpoints of the TemperaturWatch firmware, including field descriptions,
validation rules, example requests (`curl`), and example responses. For a quick overview, see the
compact table in the [README](../README.en.md#rest-api); this document describes each endpoint in detail.

## Contents

- [Basics](#basics)
- [Authentication](#authentication)
- [Error handling](#error-handling)
- [System](#system)
  - [`GET /api/system/info`](#get-apisysteminfo)
  - [`POST /api/system/reboot`](#post-apisystemreboot)
  - [`POST /api/system/factory-reset`](#post-apisystemfactory-reset)
  - [`POST /api/system/ota`](#post-apisystemota)
  - [`POST /api/system/ota-web`](#post-apisystemota-web)
  - [`GET /api/board/pins`](#get-apiboardpins)
- [Sensors](#sensors)
  - [`GET /api/sensors`](#get-apisensors)
  - [`GET /api/sensors/config`](#get-apisensorsconfig)
  - [`PUT /api/sensors/config`](#put-apisensorsconfig)
  - [`GET /api/sensors/dallas-scan`](#get-apisensorsdallas-scan)
- [Digital IOs](#digital-ios)
  - [`GET /api/io`](#get-apiio)
  - [`GET /api/io/config`](#get-apiioconfig)
  - [`PUT /api/io/config`](#put-apiioconfig)
  - [`POST /api/io/set`](#post-apiioset)
- [Network](#network)
  - [`GET /api/network/config`](#get-apinetworkconfig)
  - [`PUT /api/network/config`](#put-apinetworkconfig)
- [Time (NTP)](#time-ntp)
  - [`GET /api/time/config`](#get-apitimeconfig)
  - [`PUT /api/time/config`](#put-apitimeconfig)
- [MQTT](#mqtt)
  - [`GET /api/mqtt/config`](#get-apimqttconfig)
  - [`PUT /api/mqtt/config`](#put-apimqttconfig)
- [SNMP](#snmp)
  - [`GET /api/snmp/config`](#get-apisnmpconfig)
  - [`PUT /api/snmp/config`](#put-apisnmpconfig)
- [Login (HTTP Basic Auth)](#login-http-basic-auth)
  - [`GET /api/auth/config`](#get-apiauthconfig)
  - [`PUT /api/auth/config`](#put-apiauthconfig)
- [Complete workflow (example script)](#complete-workflow-example-script)

## Basics

- **Base URL:** `http://<ip-address>` or `http://<hostname>.local` (mDNS, default `temperaturwatch.local`).
  No separate API port – the same instance of `esp_http_server` also serves the web UI.
- **Content type:** All `GET` responses and JSON `PUT`/`POST` bodies are `application/json`. The exceptions are
  `POST /api/system/ota` and `POST /api/system/ota-web`, which expect a raw binary (`.bin`) as the body
  (`application/octet-stream`, not JSON).
- **Character set:** UTF-8.
- **Statefulness:** The API is stateless (no session cookie); when login is enabled, **every**
  request is re-authenticated via HTTP Basic Auth.
- **Config vs. live endpoints:** For sensors and IOs there are two `GET` routes each: `/config` returns the
  *stored configuration* (what's edited in the web UI), while the route without `/config`
  (`/api/sensors`, `/api/io`) returns the *current live state* (readings or switch states, respectively).
- **Full replacement on PUT:** `/api/sensors/config` and `/api/io/config` always expect the **complete array**
  on `PUT`, not just the changed entry. Workflow for editing a single entry:
  fetch the current list via `GET` → modify/add/remove the desired entry in the list → send the
  **entire** array back via `PUT`. On a validation error **nothing** is saved (the
  existing configuration remains unchanged) – there is no partial update.

## Authentication

HTTP Basic Auth, globally enabled/disabled via "System" → "Web UI login" (default: **disabled**). When
enabled, **every** `/api/...` route (and even the static web UI files) requires a valid
`Authorization: Basic ...` header – there are no public exception routes.

```bash
# Without login (default)
curl http://192.168.1.50/api/system/info

# With login enabled
curl -u admin:secret http://192.168.1.50/api/system/info
```

If the header is missing or the credentials are wrong, the firmware responds with **`401 Unauthorized`** and
the header `WWW-Authenticate: Basic realm="TemperaturWatch"` (triggers the browser's native login dialog).

## Error handling

General scheme that applies to practically all endpoints (exceptions are noted under the respective
endpoint):

| Code | Meaning | Typical cause |
|---|---|---|
| `200 OK` | Success. Body is JSON (usually `true` for pure write operations, or the requested object/array). | – |
| `400 Bad Request` | Invalid request body (missing/invalid JSON, missing required fields, validation error such as a GPIO conflict). Plain-text error message in the body (not JSON). | Malformed configuration |
| `401 Unauthorized` | Login enabled, but missing/incorrect `Authorization` header. | Missing Basic Auth credentials |
| `404 Not Found` | Unknown route, static file not found, or unknown IO `id` in `/api/io/set`. | Typo in the ID/route |
| `409 Conflict` | Only for `/api/io/set`: the output has an active threshold rule and cannot be switched manually. | Attempting to manually switch an automatically controlled output |
| `500 Internal Server Error` | Internal error (e.g. flash/NVS write error, SPIFFS mount error). | Hardware/flash problem |

Error responses (`400`/`404`/`409`/`500`) are **plain text**, not JSON – when parsing the response in your own
code, check the HTTP status first before parsing the body as JSON.

---

## System

### `GET /api/system/info`

Device, firmware, and connection status. Also polled by the web UI footer/dashboard every 10s.

**Auth:** as globally configured · **Query parameters:** none · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/system/info
```

**Example response (200):**
```json
{
  "idf_version": "v5.5.4",
  "chip": "esp32p4",
  "cores": 2,
  "uptime_s": 7762,
  "free_heap": 33935260,
  "min_free_heap": 33915212,
  "has_ip": true,
  "wifi_connected": true,
  "eth_connected": false,
  "mqtt_enabled": true,
  "mqtt_connected": true,
  "snmp_enabled": true,
  "snmp_listening": true,
  "auth_enabled": false,
  "header_gpio_count": 28,
  "time_synced": true,
  "unix_time_s": 1786357200,
  "utc_offset_s": 7200
}
```

| Field | Type | Description |
|---|---|---|
| `idf_version` | string | ESP-IDF version the firmware was built with |
| `chip` | string | Target chip (`esp32p4`) |
| `cores` | number | Number of CPU cores |
| `uptime_s` | number | Time since last boot, in seconds |
| `free_heap` | number | Currently free heap in bytes |
| `min_free_heap` | number | Minimum free heap ever measured since boot (fragmentation indicator) |
| `has_ip` | bool | `true` as soon as Ethernet **or** Wi-Fi has an IP address |
| `wifi_connected` | bool | Wi-Fi station currently has an IP address |
| `eth_connected` | bool | Ethernet interface currently has an IP address |
| `mqtt_enabled` | bool | MQTT currently enabled (independent of connection status) |
| `mqtt_connected` | bool | MQTT client currently connected to the broker |
| `snmp_enabled` | bool | SNMP currently enabled (short form of `/api/snmp/config`) |
| `snmp_listening` | bool | SNMP UDP listener is actually running (can be `false` despite `snmp_enabled:true` if enabling it still requires a reboot, see [README → SNMP](../README.en.md#snmp)) |
| `auth_enabled` | bool | Login currently enabled (short form of `/api/auth/config`) |
| `header_gpio_count` | number | Number of GPIOs available on the 40-pin header (constant 28) |
| `time_synced` | bool | `true` once the clock has been synchronized via NTP at least once (see [Time (NTP)](#time-ntp)) |
| `unix_time_s` | number | Current Unix time (UTC, seconds) – a small, meaningless value near 0 before the first NTP sync |
| `utc_offset_s` | number | Current UTC offset of the configured timezone in seconds, including any currently active daylight saving time (e.g. `7200` = UTC+2). Useful for clients that want to compute local time themselves from `unix_time_s` without parsing POSIX TZ syntax – local time = `unix_time_s + utc_offset_s` |

### `POST /api/system/reboot`

Reboots the device immediately. Configuration is preserved.

**Auth:** as globally configured · **Body:** none (empty body or `{}`)

**Example request:**
```bash
curl -X POST http://192.168.1.50/api/system/reboot
```

**Example response (200):** `true` — sent **before** the actual reboot happens after ~300ms;
the TCP connection may already have dropped by then, so a connection error after this route is normal and
not a failure.

### `POST /api/system/factory-reset`

Erases **all** configuration areas (network, MQTT, SNMP, sensors, IOs, login) from NVS storage and then
reboots. **Not reversible.**

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl -X POST http://192.168.1.50/api/system/factory-reset
```

**Example response (200):** `true` (reboot follows after ~300ms, see note above).

> ⚠️ After a factory reset, login is disabled and all sensors/IOs are deleted – the device boots
> into a clean factory state (but Wi-Fi/Ethernet behavior is unchanged, since network interfaces are not part of
> the deleted configuration in the strict sense, only the stored Wi-Fi credentials are).

### `POST /api/system/ota`

Firmware update: accepts a raw `.bin` image (from `idf.py build`, file `build/temperaturwatch.bin`) as the
**request body** (not JSON, not multipart – the entire body is the binary) and writes it to
whichever OTA slot (`ota_0`/`ota_1`) is currently inactive.

**Auth:** as globally configured · **Content type:** `application/octet-stream` (or anything, it is not
checked) · **Body:** raw binary data of the `.bin` image

**Example request:**
```bash
curl -X POST --data-binary @build/temperaturwatch.bin http://192.168.1.50/api/system/ota
```

**Example response (200):** `true` — after that, the device automatically reboots into the new firmware (~500ms
delay, connection drop is normal). If the new firmware fails to start cleanly, the bootloader automatically
rolls back to the previous version on the next boot thanks to `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body empty, transfer aborted during upload, or image validation failed (`esp_ota_end`, e.g. corrupt/incomplete file) |
| `500 Internal Server Error` | No OTA partition available, `esp_ota_begin`/`esp_ota_write`/`esp_ota_set_boot_partition` failed |

> Updates **only the firmware** (app partition). For the web UI, see `/api/system/ota-web`.

### `POST /api/system/ota-web`

Web UI update: accepts a SPIFFS image (from `idf.py build`, file `build/www.bin`, built from the
`web/` folder) as the **request body** and writes it directly to the `www` partition. **No reboot needed** –
the partition is remounted immediately after writing.

**Auth:** as globally configured · **Body:** raw binary data of the `www.bin` image

**Example request:**
```bash
curl -X POST --data-binary @build/www.bin http://192.168.1.50/api/system/ota-web
```

**Example response (200):** `true`. The REST API itself remains reachable throughout the upload (it
is part of the firmware, not dependent on SPIFFS) – only the web UI (static files) is unreachable for the
short duration of the write (erasing + writing the partition).

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body empty, image larger than the `www` partition (2 MB), or transfer aborted |
| `500 Internal Server Error` | `www` partition not found, erasing/writing the partition failed, or the new filesystem cannot be mounted after writing (corrupt image – in this case only a reflash via USB, `idf.py -p <PORT> flash`, remains) |

### `GET /api/board/pins`

List of all 28 GPIOs available on the 40-pin header. Used by the web UI to populate the GPIO selector in the
sensor/IO dialogs.

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/board/pins
```

**Example response (200, abridged):**
```json
[
  { "gpio": 0, "header_pin": 24, "note": null },
  { "gpio": 7, "header_pin": 4, "note": "I2C SDA (default function)" },
  { "gpio": 37, "header_pin": 7, "note": "Debug UART TX (default function)" },
  { "gpio": 54, "header_pin": 31, "note": null }
]
```

| Field | Type | Description |
|---|---|---|
| `gpio` | number | GPIO number |
| `header_pin` | number | Physical pin number on the 40-pin header (1–40) |
| `note` | string \| `null` | Note about a default function (e.g. I2C, debug UART) that would be overridden by an assignment; `null` if the pin is free without restriction |

---

## Sensors

### `GET /api/sensors`

Live readings of all configured sensors.

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/sensors
```

**Example response (200):**
```json
[
  {
    "id": "aussentemperatur",
    "type": "dallas",
    "gpio": 4,
    "label": "Outdoor temperature",
    "poll_interval_s": 10,
    "rom_id": "28FF641234567890",
    "has_reading": true,
    "last_read_ok": true,
    "temperature_c": 21.5,
    "last_read_time_ms": 123456
  },
  {
    "id": "keller",
    "type": "am2301",
    "gpio": 21,
    "label": "Basement",
    "poll_interval_s": 15,
    "has_reading": true,
    "last_read_ok": true,
    "temperature_c": 17.8,
    "humidity_pct": 62.3,
    "last_read_time_ms": 118900
  },
  {
    "id": "defekt",
    "type": "dht11",
    "gpio": 20,
    "label": "",
    "poll_interval_s": 10,
    "has_reading": true,
    "last_read_ok": false,
    "last_read_time_ms": 98000
  }
]
```

| Field | Type | Description |
|---|---|---|
| `id` | string | Unique sensor ID (from the configuration) |
| `type` | string | `dallas` \| `dht11` \| `am2301` |
| `gpio` | number | Assigned GPIO |
| `label` | string | Display name (can be empty) |
| `poll_interval_s` | number | Configured poll interval |
| `rom_id` | string | **Only for `type:"dallas"`** and when a fixed ROM ID is configured (16 hex characters) |
| `has_reading` | bool | `false` as long as no measurement attempt has occurred yet (shortly after boot/creation) |
| `last_read_ok` | bool | Result of the last measurement attempt (a sensor can, e.g. due to a loose contact, occasionally return errors) |
| `temperature_c` | number | **Only present if `has_reading && last_read_ok`** |
| `humidity_pct` | number | **Only present for DHT11/AM2301** (not for Dallas) and a valid reading |
| `last_read_time_ms` | number | Timestamp of the last measurement in ms since boot (`0` if never measured) – comparable to `uptime_s * 1000` from `/api/system/info` to compute the age of the reading |

### `GET /api/sensors/config`

Stored sensor configuration (not the live values).

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/sensors/config
```

**Example response (200):**
```json
[
  {
    "id": "aussentemperatur",
    "type": "dallas",
    "gpio": 4,
    "label": "Outdoor temperature",
    "poll_interval_s": 10,
    "rom_id": "28FF641234567890"
  },
  {
    "id": "keller",
    "type": "am2301",
    "gpio": 21,
    "label": "Basement",
    "poll_interval_s": 15
  }
]
```

### `PUT /api/sensors/config`

Replaces the **entire** sensor configuration. See [Full replacement on PUT](#basics) for the
editing workflow.

**Auth:** as globally configured · **Content type:** `application/json` · **Body:** JSON array (`[]`
is allowed too = delete all sensors)

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Unique ID, max. 31 characters. Freely chosen (the web UI generates it automatically from the label) |
| `type` | string | yes | `dallas` \| `dht11` \| `am2301` |
| `gpio` | number | yes | Must be on the 40-pin header (see `/api/board/pins`) and must not already be used by an IO or an incompatible sensor |
| `label` | string | no | Display name, max. 47 characters |
| `poll_interval_s` | number | no | Poll interval in seconds; default `10` if missing or `≤ 0` |
| `rom_id` | string | no | Only relevant for `type:"dallas"`: fixed 1-Wire ROM ID as a 16-digit hex string (uppercase, e.g. `"28FF641234567890"`). If omitted, the first device found on the bus is used for each measurement – fine for single sensors, risky with multiple Dallas sensors on the same GPIO |

**GPIO sharing note:** Multiple `dallas` sensors may share the same `gpio` (1-Wire multidrop bus,
distinguished via `rom_id`). All other combinations (two DHT sensors, or Dallas+DHT on the same
GPIO) are rejected.

**Example request** (two sensors, one Dallas with a fixed ROM ID, one AM2301):
```bash
curl -X PUT http://192.168.1.50/api/sensors/config \
  -H "Content-Type: application/json" \
  -d '[
    {
      "id": "aussentemperatur",
      "type": "dallas",
      "gpio": 4,
      "label": "Outdoor temperature",
      "poll_interval_s": 10,
      "rom_id": "28FF641234567890"
    },
    {
      "id": "keller",
      "type": "am2301",
      "gpio": 21,
      "label": "Basement",
      "poll_interval_s": 15
    }
  ]'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body is not a JSON array; an entry has a missing/incorrectly typed `id`/`type`/`gpio`; `type` is not a valid value; `gpio` is not on the header; `gpio` is already used by another sensor (invalid combination) or by an IO |

**Adding a single sensor (edit pattern):**
```bash
# 1. Fetch the current list
curl http://192.168.1.50/api/sensors/config > sensors.json

# 2. Add the new entry to sensors.json manually or via script

# 3. Write the complete array back
curl -X PUT http://192.168.1.50/api/sensors/config \
  -H "Content-Type: application/json" \
  --data-binary @sensors.json
```

### `GET /api/sensors/dallas-scan`

Scans the 1-Wire bus on a given GPIO and returns the ROM IDs of all Dallas devices found. Used by the
web UI in the sensor dialog via the "Scan bus" button, but works independently of saving
a sensor configuration (pure diagnostic scan).

**Auth:** as globally configured · **Query parameters:**

| Parameter | Type | Required | Description |
|---|---|---|---|
| `gpio` | number | yes | GPIO the 1-Wire bus is connected to (0–54, must be on the header) |

**Example request:**
```bash
curl "http://192.168.1.50/api/sensors/dallas-scan?gpio=4"
```

**Example response (200):**
```json
["28FF641234567890", "28AA112233445566"]
```
Empty array `[]` if no devices were found (not an error).

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | `gpio` parameter missing, not a number, outside 0–54, or not on the header |
| `500 Internal Server Error` | Error during the bus scan itself (e.g. `onewire_bus` driver error) |

---

## Digital IOs

### `GET /api/io`

Live states of all configured digital inputs/outputs.

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/io
```

**Example response (200):** one output with an active threshold rule (incl. hysteresis), one output
without a rule, one input:
```json
[
  {
    "id": "lueftungsrelais",
    "type": "output",
    "gpio": 23,
    "label": "Fan relay",
    "invert": false,
    "state": true,
    "rule": {
      "sensor_id": "keller",
      "field": "temperature_c",
      "operator": "gt",
      "threshold": 25.0,
      "hysteresis": 1.0
    }
  },
  {
    "id": "warnleuchte",
    "type": "output",
    "gpio": 24,
    "label": "Warning light",
    "invert": true,
    "state": false
  },
  {
    "id": "tuerkontakt",
    "type": "input",
    "gpio": 25,
    "label": "Door contact",
    "invert": false,
    "state": true
  }
]
```

| Field | Type | Description |
|---|---|---|
| `id` | string | Unique IO ID |
| `type` | string | `output` \| `input` |
| `gpio` | number | Assigned GPIO |
| `label` | string | Display name |
| `invert` | bool | `true` = active-low (logic level is inverted) |
| `state` | bool | **Logical** state after applying `invert` (not the raw pin level) |
| `rule` | object | **Only present for `type:"output"` with an active threshold rule** – absent entirely for manually switched outputs and for inputs (no `"rule": null`) |

`rule` sub-object:

| Field | Type | Description |
|---|---|---|
| `sensor_id` | string | ID of the controlling sensor |
| `field` | string | `temperature_c` \| `humidity_pct` |
| `operator` | string | `gt` (>) \| `gte` (≥) \| `lt` (<) \| `lte` (≤) |
| `threshold` | number | Threshold value |
| `hysteresis` | number | Dead band against rapid on/off switching (0 = no hysteresis). For `gt`/`gte`, the output only switches off again once the reading falls `hysteresis` **below** the threshold; for `lt`/`lte`, conversely, it only switches off once it rises above it |

### `GET /api/io/config`

Stored IO configuration (identical field schema to `PUT`, see below).

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/io/config
```

### `PUT /api/io/config`

Replaces the **entire** IO configuration. See [Full replacement on PUT](#basics).

**Auth:** as globally configured · **Content type:** `application/json` · **Body:** JSON array

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Unique ID, max. 31 characters |
| `type` | string | yes | `output` \| `input` |
| `gpio` | number | yes | Must be on the header; **unlike sensors, IOs support no sharing** – each GPIO may be assigned to exactly one IO entry |
| `label` | string | no | Display name, max. 47 characters |
| `invert` | bool | no | Default `false` |
| `initial_state` | bool | no | Only relevant for `type:"output"`: state immediately after boot, before a rule/manual switch takes effect. Default `false` |
| `rule` | object | no | Only meaningful for `type:"output"` (see below); omit/`null` = no automatic switching |

`rule` sub-object (when present, all fields except `hysteresis` are required):

| Field | Type | Required | Description |
|---|---|---|---|
| `sensor_id` | string | yes | Must reference an existing sensor `id` (not checked server-side for existence – with an unknown ID the rule simply never gets a valid reading) |
| `field` | string | yes | **Only** `temperature_c` or `humidity_pct` – any other value is rejected |
| `operator` | string | yes | **Only** `gt`, `gte`, `lt`, `lte` |
| `threshold` | number | yes | Threshold value |
| `hysteresis` | number | no | Must be `≥ 0`, otherwise a validation error. Default `0` |

**Example request** (one output with a threshold rule + hysteresis, one plain input):
```bash
curl -X PUT http://192.168.1.50/api/io/config \
  -H "Content-Type: application/json" \
  -d '[
    {
      "id": "lueftungsrelais",
      "type": "output",
      "gpio": 23,
      "label": "Fan relay",
      "invert": false,
      "initial_state": false,
      "rule": {
        "sensor_id": "keller",
        "field": "temperature_c",
        "operator": "gt",
        "threshold": 25.0,
        "hysteresis": 1.0
      }
    },
    {
      "id": "tuerkontakt",
      "type": "input",
      "gpio": 25,
      "label": "Door contact",
      "invert": false
    }
  ]'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not an array; missing/incorrectly typed required fields; invalid `type`; `gpio` not on the header or assigned twice (including between two IOs); `gpio` already used by a sensor; `rule` structurally invalid (wrong `field`/`operator`, missing required fields, `hysteresis < 0`) |

### `POST /api/io/set`

Manually switches a digital output. Does **not** work while the output has an active
threshold rule (in that case the rule controls it).

**Auth:** as globally configured · **Query parameters:**

| Parameter | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | ID of the output (from `/api/io/config`) |

**Body:** `{ "state": bool }`

**Example request:**
```bash
curl -X POST "http://192.168.1.50/api/io/set?id=warnleuchte" \
  -H "Content-Type: application/json" \
  -d '{"state": true}'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Query parameter `id` missing; field `state` missing or not a boolean |
| `404 Not Found` | No IO with this `id` known (or it is an input instead of an output) |
| `409 Conflict` | The output has an active threshold rule – plain-text response `"Ausgang hat eine aktive Schwellwert-Regel"` ("Output has an active threshold rule"). To switch manually, first remove the rule via `PUT /api/io/config` |

---

## Network

### `GET /api/network/config`

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/network/config
```

**Example response (200):**
```json
{
  "wifi_ssid": "MyNetwork",
  "wifi_password": "",
  "hostname": "temperaturwatch"
}
```

> `wifi_password` is **always** returned as an empty string for security reasons, regardless of the
> password actually stored.

### `PUT /api/network/config`

**Auth:** as globally configured · **Content type:** `application/json`

| Field | Type | Required | Description |
|---|---|---|---|
| `wifi_ssid` | string | yes (must be present as a string, empty allowed) | Max. 32 characters. Empty string = Wi-Fi station stays inactive (Ethernet only) |
| `wifi_password` | string | no | Max. 63 characters. **Empty/omitted = existing password is kept** (write-if-nonempty pattern, same as the login and MQTT password) |
| `hostname` | string | no | Max. 31 characters (mDNS name, reachable as `<hostname>.local`). Empty/omitted = existing value is kept |

> **Important:** Changes only take effect after a reboot (`POST /api/system/reboot`) – there is no
> live reconnect.

**Example request** (set Wi-Fi credentials, leave hostname unchanged):
```bash
curl -X PUT http://192.168.1.50/api/network/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid": "MyNetwork",
    "wifi_password": "secure-password123"
  }'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not a JSON object, or `wifi_ssid` missing/not a string |

---

## Time (NTP)

### `GET /api/time/config`

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/time/config
```

**Example response (200):**
```json
{
  "enabled": true,
  "ntp_server": "pool.ntp.org",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
  "synced": true
}
```

| Field | Note |
|---|---|
| `synced` | **Only present in the GET response** (runtime status) – ignored on `PUT` if sent. Stays permanently `true` after the first successful sync, even if NTP is disabled afterward (the last known time keeps running on the system clock) |

### `PUT /api/time/config`

**Auth:** as globally configured · **Content type:** `application/json`

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `enabled` | bool | no | `false` (for an explicit `PUT` without this field) | NTP client active/inactive. **Takes effect immediately** – no reboot needed, the SNTP client is restarted or stopped directly on save |
| `ntp_server` | string | no | `pool.ntp.org` | Max. 63 characters. Hostname or IP address of an NTP server. An empty string falls back to the default |
| `timezone` | string | no | `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Berlin) | Max. 63 characters, **POSIX TZ syntax** (not IANA zone names like `Europe/Berlin`!), e.g. `UTC0`, `EST5EDT,M3.2.0,M11.1.0`. An empty string falls back to the default. Takes effect immediately on all time output from the firmware (including `utc_offset_s` in [`/api/system/info`](#get-apisysteminfo)) |

> **Important regarding the very first boot:** If no configuration has been saved yet (factory state or after a factory reset), the firmware **automatically** starts with `enabled:true`, `ntp_server:"pool.ntp.org"`, and the default timezone – no manual intervention needed to get the clock running. A `PUT` without the `"enabled"` field, however, explicitly disables NTP (for consistency with the other config endpoints) – anyone who only wants to change `ntp_server`/`timezone` must include `"enabled": true` in the same request.

**Example request** (switch timezone to US East Coast, use a custom NTP server):
```bash
curl -X PUT http://192.168.1.50/api/time/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "ntp_server": "time.cloudflare.com",
    "timezone": "EST5EDT,M3.2.0,M11.1.0"
  }'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not a JSON object (no further field validation – a syntactically nonsensical `timezone` string is not rejected, but leads to incorrect/no daylight saving conversion instead of an error) |

---

## MQTT

### `GET /api/mqtt/config`

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/mqtt/config
```

**Example response (200):**
```json
{
  "enabled": true,
  "broker_uri": "mqtt://192.168.150.156:1883",
  "username": "",
  "password": "",
  "client_id": "",
  "base_topic": "temperaturwatch",
  "publish_interval_s": 30,
  "ca_cert": ""
}
```

> `password`, like `wifi_password`, is always masked (returned as an empty string).

### `PUT /api/mqtt/config`

**Auth:** as globally configured · **Content type:** `application/json`

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `enabled` | bool | no | `false` | MQTT client active/inactive |
| `broker_uri` | string | **yes, if `enabled:true`** | `""` | Max. 127 characters. Must start with `mqtt://` or `mqtts://`, otherwise a validation error |
| `username` | string | no | `""` | Max. 63 characters |
| `password` | string | no | – | Max. 63 characters. **Empty/omitted = existing password is kept** |
| `client_id` | string | no | `""` (firmware generates an ID internally) | Max. 31 characters |
| `base_topic` | string | no | `temperaturwatch` | Max. 31 characters. Empty string falls back to the default |
| `publish_interval_s` | number | no | `30` | Internally raised to at least `5` if smaller |
| `ca_cert` | string | no | `""` | PEM CA certificate (multi-line) for `mqtts://` connections; empty = system validation/no TLS check, depending on broker setup |

**Example request:**
```bash
curl -X PUT http://192.168.1.50/api/mqtt/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "broker_uri": "mqtt://192.168.150.156:1883",
    "base_topic": "temperaturwatch",
    "publish_interval_s": 30
  }'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not a JSON object, or `enabled:true` with a missing/invalid `broker_uri` (must have an `mqtt://`/`mqtts://` prefix) |

**For reference – MQTT topics actually used at runtime** (below `base_topic`, default
`temperaturwatch`), relevant for observing the values configured via REST on the broker:

| Topic | Direction | Payload | Description |
|---|---|---|---|
| `<base>/status` | Publish (retained, LWT) | `online` / `offline` | Connection status |
| `<base>/sensor/<id>/temperature_c` | Publish (periodic) | e.g. `21.50` | Only for a valid reading |
| `<base>/sensor/<id>/humidity_pct` | Publish (periodic) | e.g. `62.30` | Only for DHT/AM2301 with a valid reading |
| `<base>/io/<id>/state` | Publish (periodic, retained) | `ON` / `OFF` | For every configured IO |
| `<base>/io/<id>/set` | Subscribe | `ON`/`OFF`/`1`/`0`/`true`/`false` | Switches an output – counterpart to `POST /api/io/set`, subject to the same rule restrictions |

---

## SNMP

### `GET /api/snmp/config`

**Auth:** as globally configured · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/snmp/config
```

**Example response (200):**
```json
{
  "enabled": true,
  "community": "public",
  "sys_name": "temperaturwatch",
  "sys_contact": "admin@example.com",
  "sys_location": "Server room",
  "listening": true
}
```

| Field | Note |
|---|---|
| `listening` | **Only present in the GET response** (runtime status of whether the UDP listener is currently running) – ignored on `PUT` if sent, and not carried over into the stored configuration |

### `PUT /api/snmp/config`

**Auth:** as globally configured · **Content type:** `application/json`

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `enabled` | bool | no | `false` | **Changing this requires a reboot** – lwIP provides no way to stop the UDP listener once started |
| `community` | string | no | `public` | Max. 31 characters. Empty string falls back to `public`. If the listener is already running, a change is applied **immediately, live** (no reboot needed) |
| `sys_name` | string | no | Kconfig hostname | Max. 31 characters |
| `sys_contact` | string | no | `""` | Max. 31 characters |
| `sys_location` | string | no | `""` | Max. 31 characters |

**Example request:**
```bash
curl -X PUT http://192.168.1.50/api/snmp/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "community": "public",
    "sys_contact": "admin@example.com",
    "sys_location": "Server room"
  }'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not a JSON object (no further field validation) |

> For details on the SNMP MIB structure (private MIB + standard MIB-II `system` group for monitoring tools such
> as CheckMK/PRTG), see [README → SNMP](../README.en.md#snmp).

---

## Login (HTTP Basic Auth)

### `GET /api/auth/config`

**Auth:** as globally configured (this endpoint itself is also protected when login is enabled) · **Body:** none

**Example request:**
```bash
curl http://192.168.1.50/api/auth/config
```

**Example response (200):**
```json
{
  "enabled": true,
  "username": "admin",
  "password_set": true
}
```

> The password itself is **never** returned (neither plain text nor hash) – `password_set` only indicates
> whether one is stored at all.

### `PUT /api/auth/config`

**Auth:** as globally configured · **Content type:** `application/json`

| Field | Type | Required | Description |
|---|---|---|---|
| `enabled` | bool | no (default `false`) | Login active/inactive. **Cannot be set to `true` while no password is set** (neither previously nor in the same request) |
| `username` | string | no | Max. 31 characters. Empty/omitted = existing username is kept; falls back to `admin` on the very first setup |
| `password` | string (plain text) | no | Stored server-side as a SHA-256 hash, never persisted in plain text. **Empty/omitted = existing password (or existing hash) is kept** |

**Example request** (enable login and set a password):
```bash
curl -X PUT http://192.168.1.50/api/auth/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "username": "admin",
    "password": "secure-password123"
  }'
```

**Example response (200):** `true`

**Error cases (in addition to the general scheme):**

| Code | Condition |
|---|---|
| `400 Bad Request` | Body not a JSON object, **or** `enabled:true` without a password having been set (previously or in this request) – plain-text response `"Login kann nicht ohne Passwort aktiviert werden"` ("Login cannot be enabled without a password") |

> ⚠️ If login is enabled while you yourself are currently connected unauthenticated, all
> **subsequent** requests (including the web UI) are immediately affected. Note down credentials beforehand.

---

## Complete workflow (example script)

Example: create a new Dallas sensor, add an output with a threshold rule + hysteresis, and monitor the
state.

```bash
DEVICE=192.168.1.50

# 1. Show free GPIOs
curl -s "http://$DEVICE/api/board/pins" | jq

# 2. Scan the 1-Wire bus on GPIO 4
curl -s "http://$DEVICE/api/sensors/dallas-scan?gpio=4" | jq
# -> ["28FF641234567890"]

# 3. Fetch current sensor configuration, append the new sensor, write it back
curl -s "http://$DEVICE/api/sensors/config" \
  | jq '. + [{"id":"aussen","type":"dallas","gpio":4,"label":"Outside","poll_interval_s":10,"rom_id":"28FF641234567890"}]' \
  | curl -s -X PUT "http://$DEVICE/api/sensors/config" -H "Content-Type: application/json" --data-binary @-

# 4. Create an output with a threshold rule (switches on above 25°C, hysteresis 1°C)
curl -s "http://$DEVICE/api/io/config" \
  | jq '. + [{
      "id":"lueftung","type":"output","gpio":23,"label":"Ventilation",
      "invert":false,"initial_state":false,
      "rule":{"sensor_id":"aussen","field":"temperature_c","operator":"gt","threshold":25,"hysteresis":1}
    }]' \
  | curl -s -X PUT "http://$DEVICE/api/io/config" -H "Content-Type: application/json" --data-binary @-

# 5. Watch the live state
watch -n5 "curl -s http://$DEVICE/api/sensors | jq; curl -s http://$DEVICE/api/io | jq"
```

All examples assume [`jq`](https://jqlang.org/) is available for JSON processing in the shell; the REST API
itself has no dependency on it.
