# Connecting TemperaturWatch via MQTT

Guide to setting up the MQTT integration: broker configuration in the Web UI, a complete topic reference with
example payloads, IO control via MQTT, securing the connection with TLS, and examples for Home Assistant,
Node-RED, and the command line (`mosquitto_pub`/`mosquitto_sub`). For the REST API and SNMP, see
[docs/REST_API.en.md](REST_API.en.md) and [docs/PRTG_SETUP.en.md](PRTG_SETUP.en.md).

## Contents

- [Prerequisites](#prerequisites)
- [1. Set up MQTT in the Web UI](#1-set-up-mqtt-in-the-web-ui)
- [2. Test the Connection](#2-test-the-connection)
- [3. Topic Reference](#3-topic-reference)
- [4. Receiving Sensor Values](#4-receiving-sensor-values)
- [5. Switching Digital Outputs via MQTT](#5-switching-digital-outputs-via-mqtt)
- [6. Monitoring Connection Status (Last Will)](#6-monitoring-connection-status-last-will)
- [7. Securing with TLS (mqtts://)](#7-securing-with-tls-mqtts)
- [8. Integrating with Home Assistant](#8-integrating-with-home-assistant)
- [9. Integrating with Node-RED](#9-integrating-with-node-red)
- [Troubleshooting](#troubleshooting)
- [Security Notes](#security-notes)

## Prerequisites

- A reachable MQTT broker (e.g. [Mosquitto](https://mosquitto.org/), EMQX, HiveMQ, or a cloud broker).
  For local testing, a plain Mosquitto instance with no extra configuration is enough.
- Network connectivity from the TemperaturWatch device to the broker (default port **1883** unencrypted, **8883**
  for TLS – depending on the broker configuration).
- Optional: [`mosquitto_pub`/`mosquitto_sub`](https://mosquitto.org/man/mosquitto_pub-1.html) on a test machine,
  to watch/publish topics without additional software.

## 1. Set up MQTT in the Web UI

1. Open the Web UI → **"MQTT"** menu item.
2. Enable **"Enabled"**.
3. Enter the **Broker URI** – **required once enabled**, must start with `mqtt://` (unencrypted) or
   `mqtts://` (TLS, see [Section 7](#7-securing-with-tls-mqtts)), e.g.:
   ```
   mqtt://192.168.1.20:1883
   ```
4. **Username/Password** – only fill in if the broker requires authentication (leave blank for an anonymous
   connection). Leaving the password blank when editing later keeps the previously set value – for security
   reasons it is never returned in plain text.
5. **Base topic** – prefix for all of this device's topics (default `temperaturwatch`). With **multiple
   TemperaturWatch devices on the same broker**, assign a unique value per device here (e.g.
   `temperaturwatch/keller`, `temperaturwatch/dachboden`), otherwise the devices' retained messages will
   overwrite each other.
6. **Publish interval (s)** – how often sensor/IO values are published automatically (default 30s,
   minimum 5s – smaller values are raised to 5s server-side).
7. **Client ID** – optional; leave blank and the MQTT library automatically generates a unique ID.
   Set it explicitly if the broker expects fixed client IDs (e.g. for ACLs), or with multiple devices, to tell
   them apart uniquely in broker logs.
8. **Save.**

> **No restart required:** Unlike Wi-Fi or SNMP settings, an MQTT configuration change takes effect
> **immediately** – the existing client is automatically disconnected on save and reconnected with the new
> configuration.

## 2. Test the Connection

Capture all topics live at once (test machine with `mosquitto_sub`, on the same network as the broker):

```bash
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/#" -v
```

On success, this appears right after the device establishes a connection:
```
temperaturwatch/status online
temperaturwatch/sensor/aussentemperatur/temperature_c 21.50
temperaturwatch/sensor/keller/temperature_c 17.80
temperaturwatch/sensor/keller/humidity_pct 62.30
temperaturwatch/io/lueftungsrelais/state ON
```

If nothing appears, see [Troubleshooting](#troubleshooting). Alternatively, the Web UI shows a status badge
under "MQTT" (**connected** / **disconnected** / **disabled**), and the live connection status is also shown
under "System" → "Device Information".

## 3. Topic Reference

All topics below the configured **base topic** (`<base>`, default `temperaturwatch`):

| Topic | Direction | QoS | Retained | Payload | Description |
|---|---|---|---|---|---|
| `<base>/status` | Publish | 1 | yes | `online` / `offline` | Connection status; `offline` is sent by the broker as the **Last Will** if the connection is closed uncleanly (see [Section 6](#6-monitoring-connection-status-last-will)) |
| `<base>/sensor/<id>/temperature_c` | Publish | 0 | no | e.g. `21.50` | Only when a valid reading is available; `<id>` = the sensor's `id` from the configuration |
| `<base>/sensor/<id>/humidity_pct` | Publish | 0 | no | e.g. `62.30` | Only for DHT11/AM2301 with a valid reading (Dallas sensors do not provide a humidity topic) |
| `<base>/io/<id>/state` | Publish | 0 | **yes** | `ON` / `OFF` | For every configured IO (inputs **and** outputs), on every publish cycle |
| `<base>/io/<id>/set` | **Subscribe** | 1 | – | `ON`/`OFF`/`1`/`0`/`true`/`false` | Switches an output – see [Section 5](#5-switching-digital-outputs-via-mqtt) |

**Timing:** The first publish cycle (all sensors/IOs) runs **immediately after the connection is established**,
not only after the first interval elapses. After that, it follows the configured `publish_interval_s` cadence.

**Retained vs. non-retained:** `status` and `io/<id>/state` are *retained* – a newly connected client
(e.g. `mosquitto_sub`, Node-RED, Home Assistant) sees the last known value immediately, even without the device
having just published again. Temperature/humidity values are **not** retained, so that stale readings don't
remain stuck showing as "current" if a sensor is removed or the device is offline for a long time.

## 4. Receiving Sensor Values

The payload is always a plain numeric string (`%.2f`, dot as the decimal separator), **not JSON** – deliberately
minimalistic, to stay compatible with practically any MQTT-capable system (Home Assistant, Node-RED, Grafana via
an MQTT data source, ioBroker, …) without payload parsing.

```bash
# Watch a single sensor
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/sensor/aussentemperatur/temperature_c"
# -> 21.50
# -> 21.60
# -> 21.50
```

A sensor with no valid reading (probe not connected, read error) simply publishes **no** update for that cycle
– there is no explicit error topic. To detect sensor failures, use either the REST API (`has_reading`/
`last_read_ok` in [`GET /api/sensors`](REST_API.en.md#get-apisensors)) or an application-side timeout (e.g.
Home Assistant's `expire_after`, see [Section 8](#8-integrating-with-home-assistant)).

## 5. Switching Digital Outputs via MQTT

Publish to `<base>/io/<id>/set` with one of the following payloads: `ON`, `on`, `true`, `1` → switches on;
**anything else** (including `OFF`, `false`, `0`, an empty string) → switches off. Case doesn't matter for
`ON`/`TRUE`; for `1` it must be exactly the digit `1`.

```bash
mosquitto_pub -h 192.168.1.20 -t "temperaturwatch/io/lueftungsrelais/set" -m "ON"
mosquitto_pub -h 192.168.1.20 -t "temperaturwatch/io/lueftungsrelais/set" -m "OFF"
```

The new state is then confirmed automatically (within the next publish cycle) on
`<base>/io/lueftungsrelais/state`.

> ⚠️ **Rule conflict:** Just like manual switching via the REST API/Web UI, `.../set` has **no effect** on
> outputs with an active threshold rule (see [`POST /api/io/set`](REST_API.en.md#post-apiioset)) – the
> rule keeps control in that case. The MQTT handler does not acknowledge this with an error topic; only a
> warning appears in the device log. From the outside, the only visible sign is that `.../state` doesn't
> change despite `.../set` having been sent.

## 6. Monitoring Connection Status (Last Will)

`<base>/status` is published as `online` (retained) right when the connection is established. If the
connection drops **uncleanly** (power outage, network failure, crash – not a planned restart via the
Web UI), the **broker** detects this via the MQTT Last Will mechanism and automatically publishes
`offline` (retained) to the same topic – even if the device itself has long since stopped responding. This
makes for a simple, broker-guaranteed online/offline indicator, independent of sensor timeouts:

```bash
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/status" -v
# -> temperaturwatch/status online
# (device powered off)
# -> temperaturwatch/status offline
```

## 7. Securing with TLS (mqtts://)

For connections over untrusted networks (the internet, shared VLANs), TLS should be used:

1. Switch the broker URI to `mqtts://`, e.g. `mqtts://mein-broker.example.com:8883`.
2. In the **"CA certificate (for mqtts://, optional)"** field, paste the broker's PEM-encoded CA certificate
   (multi-line, including `-----BEGIN CERTIFICATE-----`/`-----END CERTIFICATE-----`). For publicly signed
   certificates (e.g. Let's Encrypt via a cloud broker), the respective certificate authority's root CA
   certificate is often enough; for self-signed/internal brokers, use your own CA or server certificate.
3. Save – the connection is immediately re-established with TLS (no restart needed, see
   [Section 1](#1-set-up-mqtt-in-the-web-ui)).

Client certificates (mTLS) are **not** currently supported – only server certificate verification via the
stored CA certificate, plus optional username/password authentication over TLS.

## 8. Integrating with Home Assistant

The firmware does **not** implement MQTT discovery (`homeassistant/...` topics) – entities must be created
manually in `configuration.yaml`. Example for a temperature/humidity sensor and a switchable relay:

```yaml
mqtt:
  sensor:
    - name: "Outdoor temperature"
      state_topic: "temperaturwatch/sensor/aussentemperatur/temperature_c"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      expire_after: 120   # mark as "unavailable" if 2 cycles (default interval 30s×4) are missed
    - name: "Basement humidity"
      state_topic: "temperaturwatch/sensor/keller/humidity_pct"
      unit_of_measurement: "%"
      device_class: humidity
      state_class: measurement
      expire_after: 120

  switch:
    - name: "Fan relay"
      state_topic: "temperaturwatch/io/lueftungsrelais/state"
      command_topic: "temperaturwatch/io/lueftungsrelais/set"
      payload_on: "ON"
      payload_off: "OFF"
      state_on: "ON"
      state_off: "OFF"
      # command_topic has no effect on outputs with an active threshold rule (see Section 5) -
      # the switch will still display the state correctly, but cannot be toggled manually.
```

`expire_after` is recommended because, as described in [Section 4](#4-receiving-sensor-values), the firmware
simply stops sending updates on sensor failure instead of publishing an error value; without `expire_after`,
Home Assistant would otherwise keep showing the last (stale) reading as current indefinitely.

## 9. Integrating with Node-RED

Subscribing an `mqtt in` node to the topic `temperaturwatch/#` (wildcard) delivers `msg.topic` and
`msg.payload` (string) for every message. Example function node for splitting it apart:

```javascript
const parts = msg.topic.split("/");
// ["temperaturwatch", "sensor", "<id>", "temperature_c"] or ["temperaturwatch", "io", "<id>", "state"]
if (parts[1] === "sensor") {
    msg.payload = { id: parts[2], field: parts[3], value: parseFloat(msg.payload) };
    return msg;
}
if (parts[1] === "io" && parts[3] === "state") {
    msg.payload = { id: parts[2], state: msg.payload === "ON" };
    return msg;
}
return null;
```

To switch: an `mqtt out` node on `temperaturwatch/io/<id>/set` with payload `ON`/`OFF` (e.g. fed from a
dashboard switch node).

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---|---|---|
| Web UI badge permanently shows "disconnected" | Broker unreachable, wrong port, firewall | Test with `mosquitto_sub`/`telnet <broker-ip> 1883` from another machine on the same network; check the broker log for incoming connection attempts |
| Save fails with 400 | `broker_uri` is missing or doesn't start with `mqtt://`/`mqtts://` | Check the prefix, see [Section 1](#1-set-up-mqtt-in-the-web-ui) |
| Connection is established but drops again immediately (`status` flaps online/offline) | The broker requires authentication but the username/password are missing or incorrect; or a client ID collision with another client on the same broker | Check the credentials; if multiple devices/clients use the same fixed client ID, assign unique client IDs or leave the field blank (automatic generation) |
| `mqtts://` connection fails, `mqtt://` works | CA certificate missing/incorrect, or the broker certificate isn't covered by the stored CA certificate | Check the certificate chain (`openssl s_client -connect <broker>:8883 -showcerts`); paste exactly the root/intermediate CA certificate that signed the broker |
| `.../set` doesn't change `.../state` | The output has an active threshold rule | Remove the rule in the Web UI under "IOs", or adjust the rule condition so the desired state is reached – manual switching is fundamentally blocked while a rule is active |
| Humidity topic never appears for a sensor | The sensor is of type `dallas` (does not provide humidity by design) | Expected – only DHT11/AM2301 publish `humidity_pct` |
| Values only become visible after a long delay | `publish_interval_s` is set high | Reduce the interval in the Web UI under "MQTT" (minimum 5s); however, the first publish after connecting always happens immediately |

## Security Notes

- If the Web UI login (HTTP Basic Auth, see [`/api/auth/config`](REST_API.en.md#login-http-basic-auth))
  is enabled, it **only** protects access to the Web UI/REST API – **not** MQTT access. Protecting the
  MQTT channel is entirely up to the broker (username/password, ACLs, TLS).
- Like all other configuration data, MQTT credentials and the CA certificate are stored unencrypted in
  internal NVS storage, but are never returned unmasked via the REST API/Web UI (see
  [REST API basics](REST_API.en.md#basics)).
- The default QoS for sensor values is deliberately **0** (no delivery confirmation, no duplicate handling
  needed) – on unstable connections, individual readings can therefore be lost without this being noticeable
  during normal operation. For critical use cases (e.g. alarm outputs), it's advisable to add monitoring via
  REST API polling or SNMP as a cross-check, rather than relying on MQTT exclusively.
- For broker access from the internet: always use TLS (`mqtts://`) **and** authentication, along with
  broker-side ACLs, so that not every connected client can switch arbitrary outputs (the firmware itself
  does not check permissions in the `.../set` handler – anyone who can publish to the topic can switch the
  corresponding output).
