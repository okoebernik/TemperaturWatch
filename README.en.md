# TemperaturWatch

Firmware for the [Waveshare ESP32-P4-WIFI6-POE-ETH](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH), which
reads temperature/humidity from Dallas 1-Wire (DS18B20), DHT11, and AM2301 sensors, manages digital
inputs/outputs (e.g. relays, alarms), and exposes everything via REST API, MQTT, and SNMP. Configuration
runs entirely through a web UI – reachable via Ethernet (always active) and WiFi (via the onboard
ESP32-C6 co-processor).

Framework: **ESP-IDF v5.5.4** (no Arduino). Web UI: hand-written vanilla HTML/CSS/JS, no build step.

## Hardware

- Board: Waveshare ESP32-P4-WIFI6-POE-ETH (ESP32-P4, Rev. v1.3, 32 MB flash, 32 MB PSRAM)
- WiFi/Bluetooth via an onboard ESP32-C6 co-processor (SDIO, via `esp_wifi_remote`/`esp-hosted`)
- Ethernet via internal EMAC + IP101 PHY (always active, no setup required)

### 40-pin header (2×20) – assignable GPIOs

Only these 28 GPIOs are permitted for sensors/IOs (hard-coded in `components/board_pins/board_pins.c`;
all others are internally wired to the Ethernet PHY, C6 SDIO link, SD card, USB, MIPI-CSI/DSI, or audio
codec and are rejected by the firmware):

| Pin | Signal | Pin | Signal | Pin | Signal | Pin | Signal |
|----|--------|----|--------|----|--------|----|--------|
| 1 | 5V | 11 | GPIO22 | 21 | GPIO1 | 31 | GPIO54 |
| 2 | 3V3 | 12 | GPIO21 | 22 | GPIO2 | 32 | GPIO26 |
| 3 | 5V | 13 | GND | 23 | GPIO36 | 33 | GND |
| 4 | GPIO7 (I2C SDA) | 14 | GPIO20 | 24 | GPIO0 | 34 | GPIO48 |
| 5 | GND | 15 | GPIO5 | 25 | GPIO32 | 35 | GPIO46 |
| 6 | GPIO8 (I2C SCL) | 16 | GPIO6 | 26 | GND | 36 | GPIO53 |
| 7 | GPIO37 (Debug UART TX) | 17 | GPIO4 | 27 | GPIO25 | 37 | GPIO27 |
| 8 | GPIO23 | 18 | 3V3 | 28 | GPIO24 | 38 | GPIO47 |
| 9 | GPIO38 (Debug UART RX) | 19 | GND | 29 | GND | 39 | GPIO45 |
| 10 | GND | 20 | GPIO3 | 30 | GPIO33 | 40 | GND |

GPIO7/8 default to I2C, GPIO37/38 share the onboard USB debug UART – both remain freely assignable in
the web UI (with a note about the default function). The current assignment is also available at runtime
via `GET /api/board/pins`.

**Ethernet pins** (internal EMAC/RMII, from `ETH_ESP32_EMAC_DEFAULT_CONFIG()` for `esp32p4`, identical to
the official Waveshare reference [waveshareteam/ESP32-P4-Platform](https://github.com/waveshareteam/ESP32-P4-Platform)):
MDC=GPIO31, MDIO=GPIO52, PHY reset=GPIO51, PHY address=1 (IP101, configurable via
`components/net_manager/Kconfig`).

## Build & flash

Prerequisite: ESP-IDF v5.5.4 (or compatible) installed.

```bash
# Load the ESP-IDF environment (adjust the path if needed)
. $IDF_PATH/export.sh   # or export.ps1 on Windows PowerShell

idf.py set-target esp32p4
idf.py build
idf.py -p <COM-port> flash monitor
```

> **Windows special case:** If `export.ps1`/`export.sh` fails with
> `ESP-IDF Python virtual environment ... not found`, it's caused by a system-wide `python` that doesn't
> match the venv created by the installer. Fix: set `IDF_PYTHON_ENV_PATH` explicitly before running export,
> e.g. `$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"`.

The first build automatically downloads several managed components via the ESP Component Registry
(internet access required): `esp_wifi_remote`, `esp_hosted`, `mdns`, `onewire_bus`, `ds18b20`.

**Important for this board:** `sdkconfig.defaults` sets `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, because
the board carries an ESP32-P4 revision v1.3 (ESP-IDF otherwise assumes rev. ≥ v3.1 and the bootloader
refuses to start with *"requires chip revision in range"*).

## First-time setup

1. Connect the board via Ethernet cable (works immediately, no configuration needed) **or** connect via
   serial and read the IP address from the log.
2. Open the web UI in a browser: `http://<ip-address>/` or `http://temperaturwatch.local/` (mDNS
   hostname, changeable under "Network").
3. Optionally set WiFi credentials under "Network" (takes effect only after a restart) and then unplug
   Ethernet.
4. Add sensors under "Sensors" (for Dallas: select a GPIO, use "Scan bus" to find ROM IDs).
5. Add digital IOs under "IOs", optionally with a threshold rule (sensor → output) including optional
   hysteresis against rapid on/off switching near the threshold (Schmitt-trigger behavior: for "gt"/"gte"
   the output only switches off again once the value drops below the threshold; for "lt"/"lte" the
   reverse).
6. Enable MQTT/SNMP as needed under the respective menu items.
7. Recommended: set up a login (HTTP Basic Auth) under "System" once the device is reachable on the
   network – the web UI/REST API is reachable **without** a login by default.

## Architecture

Every component under `components/` is self-contained (header + implementation + its own
`CMakeLists.txt`) and wired up via a `*_start()` call in `main/app_main.c`:

| Component | Purpose |
|---|---|
| `board_pins` | Static whitelist of the 28 header GPIOs |
| `gpio_registry` | Cross-module GPIO conflict checking (sensors ↔ IOs) |
| `config_store` | NVS persistence (cJSON blobs per area) |
| `net_manager` | Ethernet (IP101) + WiFi (ESP32-C6/esp-hosted) + mDNS |
| `dallas_sensor` / `dht_sensor` | Drivers (1-Wire bus scan / bit-banging, respectively) |
| `sensor_manager` | Sensor configuration, poll scheduler, live values |
| `io_manager` | Digital inputs/outputs, threshold rules |
| `mqtt_manager` | esp-mqtt client, publish/subscribe |
| `time_manager` | SNTP client (default server `pool.ntp.org`) + configurable timezone (POSIX TZ) |
| `snmp_agent` | Custom SNMPv1/v2c agent built on the lwIP protocol engine |
| `auth_manager` | HTTP Basic Auth (password stored as a SHA-256 hash) |
| `rest_api` | esp_http_server: REST routes + web UI served from SPIFFS |
| `web/` | Source of the web UI (flashed as a SPIFFS image into the `www` partition) |

## REST API

All routes under `/api/...`, JSON bodies. If login is enabled: HTTP Basic Auth is required (also applies
to the web UI itself).

> 📖 **Detailed documentation with field descriptions, validation rules, example requests (`curl`), and
> example responses for every single endpoint: [docs/REST_API.en.md](docs/REST_API.en.md).**

| Route | Methods | Purpose |
|---|---|---|
| `/api/system/info` | GET | Chip/firmware info, network/MQTT status, free heap |
| `/api/system/reboot` | POST | Restart |
| `/api/system/factory-reset` | POST | Delete all configuration areas + restart |
| `/api/system/ota` | POST (raw `.bin`) | Firmware update (app partition), automatic restart |
| `/api/system/ota-web` | POST (raw `.bin`) | Web UI update (`www` partition), no restart needed |
| `/api/board/pins` | GET | List of the 28 header GPIOs incl. hint text |
| `/api/sensors` | GET | Live readings |
| `/api/sensors/config` | GET/PUT | Sensor configuration (array, replaced in full) |
| `/api/sensors/dallas-scan?gpio=N` | GET | 1-Wire bus scan, returns discovered ROM IDs |
| `/api/io` | GET | Live IO states |
| `/api/io/config` | GET/PUT | IO configuration (array, replaced in full) |
| `/api/io/set?id=X` | POST `{state}` | Switch an output manually (not while a rule is active) |
| `/api/network/config` | GET/PUT | WiFi SSID/password, hostname |
| `/api/time/config` | GET/PUT | NTP server, timezone (POSIX TZ), sync status |
| `/api/mqtt/config` | GET/PUT | Broker, credentials, topics, TLS CA certificate |
| `/api/snmp/config` | GET/PUT | Community, sysName/Contact/Location |
| `/api/auth/config` | GET/PUT | Enable login, username/password |

Sensor/IO configuration is transferred as a complete array (PUT replaces the entire list) – the web UI
loads the current list, changes one entry locally, and sends the complete array back.

## Help in the web UI

The REST API, MQTT, and PRTG guides (`docs/REST_API.md`, `docs/MQTT_SETUP.md`, `docs/PRTG_SETUP.md`) are
also available directly on the device under "Help" – as pre-rendered HTML fragments in
`web/help/*.{de,en}.html` (generated from the Markdown sources via `scripts/md_to_help_html.py`, no
Markdown parser needed at runtime on the device), including an English translation (`docs/*.en.md`) and
switching via the same DE/EN language toggle as the rest of the web UI. When editing a `docs/*.md` file,
the corresponding HTML fragments must be regenerated manually:

```bash
python scripts/md_to_help_html.py docs/REST_API.md web/help/rest-api.de.html
python scripts/md_to_help_html.py docs/REST_API.en.md web/help/rest-api.en.html
# same pattern for MQTT_SETUP.md/.en.md -> mqtt.de/en.html and PRTG_SETUP.md/.en.md -> prtg.de/en.html
```

> Cross-references like "see README" or between the three guides link relatively in the source
> (`../README.md`, `PRTG_SETUP.md`, etc.) – which works when reading on GitHub. `scripts/md_to_help_html.py`
> automatically rewrites these links during conversion: README references point to the real GitHub URL of
> the repo, and cross-doc references between the guides become SPA-internal jumps (no page change, stays
> within the in-app help).

## Time (NTP)

The status bar at the top of the web UI continuously shows MQTT, SNMP, and network status, plus a live
ticking clock. The time is synchronized via SNTP (default server `pool.ntp.org`, configurable under
"Network" in the web UI), including a freely selectable timezone (POSIX TZ syntax, a curated selection of
common regions plus a free-text option for anything else). Enabled out of the box (default timezone
Europe/Berlin) – no manual setup needed for the clock to run once a network connection exists. Changes
take effect immediately, no restart needed. Details:
[docs/REST_API.en.md → Time (NTP)](docs/REST_API.en.md#time-ntp).

## MQTT

> 📖 **Detailed guide** (broker setup in the web UI, TLS, IO control, Home Assistant/Node-RED examples,
> troubleshooting): [docs/MQTT_SETUP.en.md](docs/MQTT_SETUP.en.md).

Topics beneath the configured base topic (default `temperaturwatch`):

- `<base>/status` – `online`/`offline` (retained, last will)
- `<base>/sensor/<id>/temperature_c`, `<base>/sensor/<id>/humidity_pct` – published periodically
- `<base>/io/<id>/state` – `ON`/`OFF`, published periodically
- `<base>/io/<id>/set` – subscribed, payload `ON`/`OFF`/`1`/`0`/`true`/`false` switches an output

## SNMP

Custom SNMPv1/v2c agent (UDP/161) with a private MIB under the placeholder OID `1.3.6.1.4.1.99999.1`
(PEN 99999 is **not** registered with IANA – replace with your own if needed).

| OID suffix (base `1.3.6.1.4.1.99999.1`) | Content |
|---|---|
| `.1.1.0` … `.1.5.0` | sysDescr, sysUpTime, sysName, sysContact, sysLocation |
| `.2.1.0` | sensorCount |
| `.2.2.1.{1..8}.<n>` | sensorTable: index, ID, label, type, temperature×10, humidity×10, valid, age(s) |
| `.3.1.0` | ioCount |
| `.3.2.1.{1..5}.<n>` | ioTable: index, ID, label, type, state |

Additionally, the same system info is fully available under the **standard MIB-II `system` group**
(`1.3.6.1.2.1.1.{1..7}.0` = sysDescr, sysObjectID, sysUpTime, sysContact, sysName, sysLocation,
sysServices, standard numbering). This matters for monitoring tools like CheckMK/Zabbix/PRTG: during
device discovery they first query sysDescr **and sysObjectID** to determine whether a host responds via
SNMP at all – an agent that only serves the private MIB gets classified as "unreachable" by such tools
despite functioning SNMP responses (CheckMK error message e.g. "Cannot fetch system object OID
.1.3.6.1.2.1.1.2.0"). Due to the lack of a registered PEN, `sysObjectID` points to the device's own
private base OID (`1.3.6.1.4.1.99999.1`), and `sysServices` reports `72` (layer 4 + 7, endpoint device
per RFC 1213).

**Known limitation:** automatic MIB-II packet counters (ifInOctets etc.) are deliberately not provided –
lwIP's ready-made `snmp_mib2` module would have caused an incompatible memory-layout conflict with the
rest of the (unmodified, precompiled) lwIP framework. Details in the comment in
`components/snmp_agent/snmp_agent.c`. Enabling/disabling SNMP requires a restart (lwIP has no function to
stop the UDP listener again after it has started).

**Monitoring integration:** step-by-step guide for PRTG (native `SNMP Custom Table` sensors, no plugin
needed) at [docs/PRTG_SETUP.en.md](docs/PRTG_SETUP.en.md). A ready-made CheckMK plugin lives at
[contrib/checkmk/](contrib/checkmk/).

## Security

- HTTP Basic Auth is **disabled** by default (web UI/API freely reachable) to avoid accidentally locking
  yourself out at factory delivery. Recommended to enable under "System" once the device is on the
  network.
- Passwords (login, MQTT) are never returned in plain text to the frontend; leaving the password field
  empty when saving keeps the previously set password. The login password is persisted only as a SHA-256
  hash.

## OTA updates

Firmware updates can be applied via `POST /api/system/ota` (raw `.bin` as the request body, no JSON) –
e.g. via curl:

```bash
curl -X POST --data-binary @build/temperaturwatch.bin http://<ip>/api/system/ota
```

In the web UI under "System" → "Firmware update" there's a file upload with a progress indicator for
this. The image is written to whichever `ota_0`/`ota_1` slot is currently inactive; on success the device
automatically restarts into it. If the new firmware doesn't start up cleanly (crash/hang before reaching
the main loop), the bootloader automatically rolls back to the previous version thanks to
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

> **Note:** `/api/system/ota` updates **only the firmware** (app partition). The web UI (`web/*`) lives in
> a separate SPIFFS partition (`www`) and has its own OTA endpoint: `POST /api/system/ota-web` accepts the
> `build/www.bin` produced by `idf.py build` and writes it directly to the `www` partition – no restart
> needed, no USB required. The web UI provides a second upload area, "Web UI update", for this. Details on
> both endpoints: [docs/REST_API.en.md](docs/REST_API.en.md#post-apisystemota-web).

```bash
curl -X POST --data-binary @build/www.bin http://<ip>/api/system/ota-web
```

## Network: static IP (optional)

Ethernet and WiFi obtain their address via DHCP by default. Either interface can independently be given a
fixed IP address (IP, subnet mask, gateway, optional DNS server) – under "Network" in the web UI or via
`PUT /api/network/config` (fields `eth_static`/`wifi_static`, see
[docs/REST_API.en.md](docs/REST_API.en.md#put-apinetworkconfig)). When static IP is enabled, the
respective interface's DHCP client is stopped and the fixed address applied as soon as the link comes up;
without it enabled, the interface behaves as before (DHCP). As with WiFi credentials/hostname, a change
takes effect only after a restart.

> ⚠️ An incorrectly entered address/gateway may make the device unreachable over the network – recovery
> is then only possible via a serial connection (`idf.py -p <PORT> monitor` + factory reset via the web UI,
> if still reachable, or reflashing).

## Known limitations / possible extensions

- WiFi and SNMP configuration changes take effect only after a restart (no live reconnect).
- Digital inputs use an internal pull-up without configurable debouncing.
