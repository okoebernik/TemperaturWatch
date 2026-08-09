# TemperaturWatch – REST-API-Referenz

Vollständige Referenz aller `/api/...`-Endpunkte der TemperaturWatch-Firmware, inkl. Feldbeschreibungen,
Validierungsregeln, Beispiel-Requests (`curl`) und Beispiel-Responses. Für den schnellen Überblick siehe die
kompakte Tabelle im [README](../README.md#rest-api); dieses Dokument beschreibt jeden Endpunkt im Detail.

## Inhalt

- [Grundlagen](#grundlagen)
- [Authentifizierung](#authentifizierung)
- [Fehlerbehandlung](#fehlerbehandlung)
- [System](#system)
  - [`GET /api/system/info`](#get-apisysteminfo)
  - [`POST /api/system/reboot`](#post-apisystemreboot)
  - [`POST /api/system/factory-reset`](#post-apisystemfactory-reset)
  - [`POST /api/system/ota`](#post-apisystemota)
  - [`POST /api/system/ota-web`](#post-apisystemota-web)
  - [`GET /api/board/pins`](#get-apiboardpins)
- [Sensoren](#sensoren)
  - [`GET /api/sensors`](#get-apisensors)
  - [`GET /api/sensors/config`](#get-apisensorsconfig)
  - [`PUT /api/sensors/config`](#put-apisensorsconfig)
  - [`GET /api/sensors/dallas-scan`](#get-apisensorsdallas-scan)
- [Digitale IOs](#digitale-ios)
  - [`GET /api/io`](#get-apiio)
  - [`GET /api/io/config`](#get-apiioconfig)
  - [`PUT /api/io/config`](#put-apiioconfig)
  - [`POST /api/io/set`](#post-apiioset)
- [Netzwerk](#netzwerk)
  - [`GET /api/network/config`](#get-apinetworkconfig)
  - [`PUT /api/network/config`](#put-apinetworkconfig)
- [Uhrzeit (NTP)](#uhrzeit-ntp)
  - [`GET /api/time/config`](#get-apitimeconfig)
  - [`PUT /api/time/config`](#put-apitimeconfig)
- [MQTT](#mqtt)
  - [`GET /api/mqtt/config`](#get-apimqttconfig)
  - [`PUT /api/mqtt/config`](#put-apimqttconfig)
- [SNMP](#snmp)
  - [`GET /api/snmp/config`](#get-apisnmpconfig)
  - [`PUT /api/snmp/config`](#put-apisnmpconfig)
- [Login (HTTP-Basic-Auth)](#login-http-basic-auth)
  - [`GET /api/auth/config`](#get-apiauthconfig)
  - [`PUT /api/auth/config`](#put-apiauthconfig)
- [Vollständiger Workflow (Beispiel-Skript)](#vollständiger-workflow-beispiel-skript)

## Grundlagen

- **Base-URL:** `http://<ip-adresse>` oder `http://<hostname>.local` (mDNS, Default `temperaturwatch.local`).
  Kein separater API-Port – dieselbe Instanz von `esp_http_server` liefert auch das Web-UI aus.
- **Content-Type:** Alle `GET`-Antworten und JSON-`PUT`/`POST`-Bodies sind `application/json`. Ausnahmen sind
  `POST /api/system/ota` und `POST /api/system/ota-web`, die ein rohes Binary (`.bin`) als Body erwarten
  (`application/octet-stream`, kein JSON).
- **Zeichensatz:** UTF-8.
- **Statefulness:** Die API ist zustandslos (kein Session-Cookie); bei aktiviertem Login wird bei **jedem**
  Request erneut per HTTP-Basic-Auth authentifiziert.
- **Config- vs. Live-Endpunkte:** Bei Sensoren und IOs gibt es je zwei GET-Routen: `/config` liefert die
  *gespeicherte Konfiguration* (was im Web-UI bearbeitet wird), die Route ohne `/config`
  (`/api/sensors`, `/api/io`) liefert den *aktuellen Live-Zustand* (Messwerte bzw. Schaltzustände).
- **Vollständiges Ersetzen bei PUT:** `/api/sensors/config` und `/api/io/config` erwarten beim `PUT` **immer
  das komplette Array**, nicht nur den geänderten Eintrag. Ablauf zum Bearbeiten eines einzelnen Eintrags:
  aktuelle Liste per `GET` holen → gewünschten Eintrag in der Liste ändern/hinzufügen/entfernen → das
  **gesamte** Array per `PUT` zurücksenden. Bei einem Validierungsfehler wird **nichts** gespeichert (die
  bisherige Konfiguration bleibt unverändert) – es gibt kein teilweises Übernehmen.

## Authentifizierung

HTTP-Basic-Auth, global über „System" → „Web-UI-Login" (de-)aktivierbar (Default: **deaktiviert**). Ist sie
aktiviert, verlangt **jede** `/api/...`-Route (und auch die statischen Web-UI-Dateien) einen gültigen
`Authorization: Basic ...`-Header – es gibt keine öffentlichen Ausnahme-Routen.

```bash
# Ohne Login (Default)
curl http://192.168.1.50/api/system/info

# Mit aktiviertem Login
curl -u admin:geheim http://192.168.1.50/api/system/info
```

Fehlt der Header oder sind die Zugangsdaten falsch, antwortet die Firmware mit **`401 Unauthorized`** und dem
Header `WWW-Authenticate: Basic realm="TemperaturWatch"` (löst im Browser den nativen Login-Dialog aus).

## Fehlerbehandlung

Allgemeines Schema, das für praktisch alle Endpunkte gilt (Ausnahmen sind unter dem jeweiligen Endpunkt
vermerkt):

| Code | Bedeutung | Typischer Auslöser |
|---|---|---|
| `200 OK` | Erfolgreich. Body ist JSON (meist `true` bei reinen Schreib-Operationen, oder das angeforderte Objekt/Array). | – |
| `400 Bad Request` | Ungültiger Request-Body (kein/ungültiges JSON, fehlende Pflichtfelder, Validierungsfehler wie GPIO-Konflikt). Klartext-Fehlermeldung im Body (kein JSON). | Fehlerhafte Konfiguration |
| `401 Unauthorized` | Login aktiviert, aber kein/falscher `Authorization`-Header. | Fehlende Basic-Auth-Zugangsdaten |
| `404 Not Found` | Unbekannte Route, statische Datei nicht gefunden, oder unbekannte IO-`id` bei `/api/io/set`. | Tippfehler in der ID/Route |
| `409 Conflict` | Nur bei `/api/io/set`: der Ausgang hat eine aktive Schwellwert-Regel und kann nicht manuell geschaltet werden. | Versuch, einen automatisch geregelten Ausgang manuell zu schalten |
| `500 Internal Server Error` | Interner Fehler (z.B. Flash-/NVS-Schreibfehler, SPIFFS-Mount-Fehler). | Hardware-/Flash-Problem |

Fehlerantworten (`400`/`404`/`409`/`500`) sind **Klartext**, kein JSON – beim Parsen der Antwort im eigenen Code
also zuerst den HTTP-Status prüfen, bevor der Body als JSON geparst wird.

---

## System

### `GET /api/system/info`

Geräte-, Firmware- und Verbindungsstatus. Wird auch vom Web-UI-Footer/Dashboard alle 10s gepollt.

**Auth:** wie global konfiguriert · **Query-Parameter:** keine · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/system/info
```

**Beispiel-Response (200):**
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

| Feld | Typ | Beschreibung |
|---|---|---|
| `idf_version` | string | ESP-IDF-Version, mit der die Firmware gebaut wurde |
| `chip` | string | Ziel-Chip (`esp32p4`) |
| `cores` | number | Anzahl CPU-Kerne |
| `uptime_s` | number | Laufzeit seit letztem Boot in Sekunden |
| `free_heap` | number | Aktuell freier Heap in Bytes |
| `min_free_heap` | number | Minimal je gemessener freier Heap seit Boot (Fragmentierungs-Indikator) |
| `has_ip` | bool | `true`, sobald Ethernet **oder** WLAN eine IP-Adresse hat |
| `wifi_connected` | bool | WLAN-Station hat aktuell eine IP-Adresse |
| `eth_connected` | bool | Ethernet-Interface hat aktuell eine IP-Adresse |
| `mqtt_enabled` | bool | MQTT aktuell aktiviert (unabhängig vom Verbindungsstatus) |
| `mqtt_connected` | bool | MQTT-Client aktuell mit dem Broker verbunden |
| `snmp_enabled` | bool | SNMP aktuell aktiviert (Kurzform von `/api/snmp/config`) |
| `snmp_listening` | bool | SNMP-UDP-Listener läuft tatsächlich (kann trotz `snmp_enabled:true` `false` sein, falls die Aktivierung noch einen Neustart benötigt, siehe [README → SNMP](../README.md#snmp)) |
| `auth_enabled` | bool | Login aktuell aktiviert (Kurzform von `/api/auth/config`) |
| `header_gpio_count` | number | Anzahl der auf der 40-Pin-Leiste verfügbaren GPIOs (konstant 28) |
| `time_synced` | bool | `true`, sobald die Uhrzeit mindestens einmal per NTP synchronisiert wurde (siehe [Uhrzeit (NTP)](#uhrzeit-ntp)) |
| `unix_time_s` | number | Aktuelle Unix-Zeit (UTC, Sekunden) – vor der ersten NTP-Synchronisation ein kleiner, bedeutungsloser Wert nahe 0 |
| `utc_offset_s` | number | Aktueller UTC-Offset der konfigurierten Zeitzone in Sekunden, inkl. gerade aktiver Sommerzeit (z.B. `7200` = UTC+2). Praktisch für Clients, die die lokale Zeit selbst aus `unix_time_s` berechnen wollen, ohne POSIX-TZ-Syntax zu interpretieren – lokale Zeit = `unix_time_s + utc_offset_s` |

### `POST /api/system/reboot`

Startet das Gerät sofort neu. Konfiguration bleibt erhalten.

**Auth:** wie global konfiguriert · **Body:** keiner (leerer Body oder `{}`)

**Beispiel-Request:**
```bash
curl -X POST http://192.168.1.50/api/system/reboot
```

**Beispiel-Response (200):** `true` — wird gesendet, **bevor** der eigentliche Neustart nach ~300ms erfolgt;
die TCP-Verbindung kann dabei bereits abreißen, ein Verbindungsfehler ist nach dieser Route also normal und
kein Fehlschlag.

### `POST /api/system/factory-reset`

Löscht **alle** Konfigurationsbereiche (Netzwerk, MQTT, SNMP, Sensoren, IOs, Login) aus dem NVS-Speicher und
startet danach neu. **Nicht umkehrbar.**

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl -X POST http://192.168.1.50/api/system/factory-reset
```

**Beispiel-Response (200):** `true` (Neustart folgt nach ~300ms, siehe Hinweis oben).

> ⚠️ Nach einem Factory-Reset ist der Login deaktiviert und alle Sensoren/IOs sind gelöscht – das Gerät startet
> mit reinem Werkszustand (aber unverändertem WLAN-/Ethernet-Verhalten, da Netzwerk-Interfaces nicht Teil der
> gelöschten Konfiguration im engeren Sinn sind, sondern nur die gespeicherten WLAN-Zugangsdaten).

### `POST /api/system/ota`

Firmware-Update: Nimmt ein rohes `.bin`-Image (aus `idf.py build`, Datei `build/temperaturwatch.bin`) als
**Request-Body** entgegen (kein JSON, kein Multipart – der komplette Body ist das Binary) und schreibt es in
den jeweils inaktiven OTA-Slot (`ota_0`/`ota_1`).

**Auth:** wie global konfiguriert · **Content-Type:** `application/octet-stream` (oder beliebig, wird nicht
geprüft) · **Body:** rohe Binärdaten des `.bin`-Images

**Beispiel-Request:**
```bash
curl -X POST --data-binary @build/temperaturwatch.bin http://192.168.1.50/api/system/ota
```

**Beispiel-Response (200):** `true` — danach startet das Gerät automatisch mit der neuen Firmware neu (~500ms
Verzögerung, Verbindungsabbruch ist normal). Startet die neue Firmware nicht sauber, rollt der Bootloader dank
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` beim nächsten Boot automatisch auf die vorherige Version zurück.

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body leer, Übertragung während des Uploads abgebrochen, oder Image-Validierung fehlgeschlagen (`esp_ota_end`, z.B. defekte/unvollständige Datei) |
| `500 Internal Server Error` | Keine OTA-Partition verfügbar, `esp_ota_begin`/`esp_ota_write`/`esp_ota_set_boot_partition` fehlgeschlagen |

> Aktualisiert **ausschließlich die Firmware** (App-Partition). Für das Web-UI siehe `/api/system/ota-web`.

### `POST /api/system/ota-web`

Web-UI-Update: Nimmt ein SPIFFS-Image (aus `idf.py build`, Datei `build/www.bin`, gebaut aus dem `web/`-Ordner)
als **Request-Body** entgegen und schreibt es direkt in die `www`-Partition. **Kein Neustart nötig** – die
Partition wird nach dem Schreiben sofort wieder eingehängt.

**Auth:** wie global konfiguriert · **Body:** rohe Binärdaten des `www.bin`-Images

**Beispiel-Request:**
```bash
curl -X POST --data-binary @build/www.bin http://192.168.1.50/api/system/ota-web
```

**Beispiel-Response (200):** `true`. Die REST-API selbst bleibt während des Uploads durchgehend erreichbar (sie
ist Teil der Firmware, nicht von SPIFFS abhängig) – nur das Web-UI (statische Dateien) ist für die kurze Dauer
des Schreibvorgangs (Löschen + Beschreiben der Partition) nicht erreichbar.

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body leer, Image größer als die `www`-Partition (2 MB), oder Übertragung abgebrochen |
| `500 Internal Server Error` | `www`-Partition nicht gefunden, Löschen/Schreiben der Partition fehlgeschlagen, oder das neue Dateisystem lässt sich nach dem Schreiben nicht mounten (defektes Image – in diesem Fall bleibt nur ein Reflash per USB, `idf.py -p <PORT> flash`) |

### `GET /api/board/pins`

Liste aller 28 auf der 40-Pin-Leiste verfügbaren GPIOs. Wird vom Web-UI genutzt, um die GPIO-Auswahl in den
Sensor-/IO-Dialogen zu befüllen.

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/board/pins
```

**Beispiel-Response (200, gekürzt):**
```json
[
  { "gpio": 0, "header_pin": 24, "note": null },
  { "gpio": 7, "header_pin": 4, "note": "I2C SDA (Standardfunktion)" },
  { "gpio": 37, "header_pin": 7, "note": "Debug-UART TX (Standardfunktion)" },
  { "gpio": 54, "header_pin": 31, "note": null }
]
```

| Feld | Typ | Beschreibung |
|---|---|---|
| `gpio` | number | GPIO-Nummer |
| `header_pin` | number | Physische Pin-Nummer auf der 40-Pin-Leiste (1–40) |
| `note` | string \| `null` | Hinweis auf eine Standardfunktion (z.B. I2C, Debug-UART), die durch eine Zuweisung überschrieben würde; `null` wenn der Pin ohne Einschränkung frei ist |

---

## Sensoren

### `GET /api/sensors`

Live-Messwerte aller konfigurierten Sensoren.

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/sensors
```

**Beispiel-Response (200):**
```json
[
  {
    "id": "aussentemperatur",
    "type": "dallas",
    "gpio": 4,
    "label": "Außentemperatur",
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
    "label": "Keller",
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

| Feld | Typ | Beschreibung |
|---|---|---|
| `id` | string | Eindeutige Sensor-ID (aus der Konfiguration) |
| `type` | string | `dallas` \| `dht11` \| `am2301` |
| `gpio` | number | Zugewiesener GPIO |
| `label` | string | Anzeigename (kann leer sein) |
| `poll_interval_s` | number | Konfiguriertes Poll-Intervall |
| `rom_id` | string | **Nur bei `type:"dallas"`** und wenn eine feste ROM-ID konfiguriert ist (16 Hex-Zeichen) |
| `has_reading` | bool | `false`, solange noch kein einziger Messversuch stattgefunden hat (kurz nach Boot/Anlegen) |
| `last_read_ok` | bool | Ergebnis des letzten Messversuchs (Sensor kann z.B. durch Wackelkontakt zeitweise Fehler liefern) |
| `temperature_c` | number | **Nur vorhanden, wenn `has_reading && last_read_ok`** |
| `humidity_pct` | number | **Nur vorhanden bei DHT11/AM2301** (nicht bei Dallas) und gültiger Messung |
| `last_read_time_ms` | number | Zeitstempel der letzten Messung in ms seit Boot (`0`, falls noch nie gemessen) – vergleichbar mit `uptime_s * 1000` aus `/api/system/info`, um das Alter der Messung zu berechnen |

### `GET /api/sensors/config`

Gespeicherte Sensor-Konfiguration (nicht die Live-Werte).

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/sensors/config
```

**Beispiel-Response (200):**
```json
[
  {
    "id": "aussentemperatur",
    "type": "dallas",
    "gpio": 4,
    "label": "Außentemperatur",
    "poll_interval_s": 10,
    "rom_id": "28FF641234567890"
  },
  {
    "id": "keller",
    "type": "am2301",
    "gpio": 21,
    "label": "Keller",
    "poll_interval_s": 15
  }
]
```

### `PUT /api/sensors/config`

Ersetzt die **komplette** Sensor-Konfiguration. Siehe [Vollständiges Ersetzen bei PUT](#grundlagen) für den
Bearbeiten-Ablauf.

**Auth:** wie global konfiguriert · **Content-Type:** `application/json` · **Body:** JSON-Array (auch `[]`
erlaubt = alle Sensoren löschen)

| Feld | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `id` | string | ja | Eindeutige ID, max. 31 Zeichen. Frei wählbar (das Web-UI generiert sie automatisch aus dem Label) |
| `type` | string | ja | `dallas` \| `dht11` \| `am2301` |
| `gpio` | number | ja | Muss auf der 40-Pin-Leiste liegen (siehe `/api/board/pins`) und darf nicht bereits von einem IO oder einem inkompatiblen Sensor belegt sein |
| `label` | string | nein | Anzeigename, max. 47 Zeichen |
| `poll_interval_s` | number | nein | Poll-Intervall in Sekunden; Default `10`, falls fehlend oder `≤ 0` |
| `rom_id` | string | nein | Nur für `type:"dallas"` relevant: feste 1-Wire-ROM-ID als 16-stelliger Hex-String (Großbuchstaben, z.B. `"28FF641234567890"`). Fehlt sie, wird bei jeder Messung das erste am Bus gefundene Gerät verwendet – praktisch für Einzelsensoren, riskant bei mehreren Dallas-Sensoren am selben GPIO |

**Besonderheit GPIO-Sharing:** Mehrere `dallas`-Sensoren dürfen sich denselben `gpio` teilen (1-Wire-Multidrop-Bus,
unterschieden über `rom_id`). Alle anderen Kombinationen (zwei DHT-Sensoren, oder Dallas+DHT auf demselben
GPIO) werden abgelehnt.

**Beispiel-Request** (zwei Sensoren, einer davon Dallas mit fester ROM-ID, einer AM2301):
```bash
curl -X PUT http://192.168.1.50/api/sensors/config \
  -H "Content-Type: application/json" \
  -d '[
    {
      "id": "aussentemperatur",
      "type": "dallas",
      "gpio": 4,
      "label": "Außentemperatur",
      "poll_interval_s": 10,
      "rom_id": "28FF641234567890"
    },
    {
      "id": "keller",
      "type": "am2301",
      "gpio": 21,
      "label": "Keller",
      "poll_interval_s": 15
    }
  ]'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body ist kein JSON-Array; ein Eintrag hat fehlendes/falsch typisiertes `id`/`type`/`gpio`; `type` ist kein gültiger Wert; `gpio` liegt nicht auf der Stiftleiste; `gpio` ist bereits von einem anderen Sensor (unzulässige Kombination) oder einem IO belegt |

**Einzelnen Sensor hinzufügen (Bearbeiten-Muster):**
```bash
# 1. Aktuelle Liste holen
curl http://192.168.1.50/api/sensors/config > sensors.json

# 2. sensors.json manuell/skriptgesteuert um den neuen Eintrag ergänzen

# 3. Komplettes Array zurückschreiben
curl -X PUT http://192.168.1.50/api/sensors/config \
  -H "Content-Type: application/json" \
  --data-binary @sensors.json
```

### `GET /api/sensors/dallas-scan`

Scannt den 1-Wire-Bus an einem gegebenen GPIO und liefert die ROM-IDs aller gefundenen Dallas-Geräte. Wird vom
Web-UI im Sensor-Dialog über den Button „Bus scannen" verwendet, funktioniert aber unabhängig vom Speichern
einer Sensor-Konfiguration (reiner Diagnose-Scan).

**Auth:** wie global konfiguriert · **Query-Parameter:**

| Parameter | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `gpio` | number | ja | GPIO, an dem der 1-Wire-Bus angeschlossen ist (0–54, muss auf der Stiftleiste liegen) |

**Beispiel-Request:**
```bash
curl "http://192.168.1.50/api/sensors/dallas-scan?gpio=4"
```

**Beispiel-Response (200):**
```json
["28FF641234567890", "28AA112233445566"]
```
Leeres Array `[]`, wenn keine Geräte gefunden wurden (kein Fehler).

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | `gpio`-Parameter fehlt, ist keine Zahl, außerhalb 0–54, oder liegt nicht auf der Stiftleiste |
| `500 Internal Server Error` | Fehler beim Bus-Scan selbst (z.B. `onewire_bus`-Treiberfehler) |

---

## Digitale IOs

### `GET /api/io`

Live-Zustände aller konfigurierten digitalen Ein-/Ausgänge.

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/io
```

**Beispiel-Response (200):** ein Ausgang mit aktiver Schwellwert-Regel (inkl. Hysterese), ein Ausgang ohne
Regel, ein Eingang:
```json
[
  {
    "id": "lueftungsrelais",
    "type": "output",
    "gpio": 23,
    "label": "Lüfter-Relais",
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
    "label": "Warnleuchte",
    "invert": true,
    "state": false
  },
  {
    "id": "tuerkontakt",
    "type": "input",
    "gpio": 25,
    "label": "Türkontakt",
    "invert": false,
    "state": true
  }
]
```

| Feld | Typ | Beschreibung |
|---|---|---|
| `id` | string | Eindeutige IO-ID |
| `type` | string | `output` \| `input` |
| `gpio` | number | Zugewiesener GPIO |
| `label` | string | Anzeigename |
| `invert` | bool | `true` = active-low (Logikpegel wird invertiert) |
| `state` | bool | **Logischer** Zustand nach Anwendung von `invert` (nicht der rohe Pegel am Pin) |
| `rule` | object | **Nur vorhanden bei `type:"output"` mit aktiver Schwellwert-Regel** – fehlt bei manuell geschalteten Ausgängen und bei Eingängen komplett (kein `"rule": null`) |

`rule`-Unterobjekt:

| Feld | Typ | Beschreibung |
|---|---|---|
| `sensor_id` | string | ID des steuernden Sensors |
| `field` | string | `temperature_c` \| `humidity_pct` |
| `operator` | string | `gt` (>) \| `gte` (≥) \| `lt` (<) \| `lte` (≤) |
| `threshold` | number | Schwellwert |
| `hysteresis` | number | Totzone gegen schnelles Ein-/Ausschalten (0 = keine Hysterese). Bei `gt`/`gte` schaltet der Ausgang erst wieder aus, wenn der Messwert um `hysteresis` **unter** den Schwellwert fällt; bei `lt`/`lte` umgekehrt erst aus, wenn er darüber steigt |

### `GET /api/io/config`

Gespeicherte IO-Konfiguration (identisches Feldschema wie beim `PUT`, siehe unten).

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/io/config
```

### `PUT /api/io/config`

Ersetzt die **komplette** IO-Konfiguration. Siehe [Vollständiges Ersetzen bei PUT](#grundlagen).

**Auth:** wie global konfiguriert · **Content-Type:** `application/json` · **Body:** JSON-Array

| Feld | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `id` | string | ja | Eindeutige ID, max. 31 Zeichen |
| `type` | string | ja | `output` \| `input` |
| `gpio` | number | ja | Muss auf der Stiftleiste liegen; **im Gegensatz zu Sensoren gibt es bei IOs kein Sharing** – jeder GPIO darf nur genau einem IO-Eintrag zugewiesen sein |
| `label` | string | nein | Anzeigename, max. 47 Zeichen |
| `invert` | bool | nein | Default `false` |
| `initial_state` | bool | nein | Nur für `type:"output"` relevant: Zustand direkt nach dem Booten, bevor eine Regel/manuelle Schaltung greift. Default `false` |
| `rule` | object | nein | Nur für `type:"output"` sinnvoll (siehe unten); weglassen/`null` = kein automatisches Schalten |

`rule`-Unterobjekt (wenn vorhanden, sind alle Felder außer `hysteresis` Pflicht):

| Feld | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `sensor_id` | string | ja | Muss auf eine existierende Sensor-`id` verweisen (wird nicht serverseitig auf Existenz geprüft – bei unbekannter ID liefert die Regel einfach nie eine gültige Messung) |
| `field` | string | ja | **Nur** `temperature_c` oder `humidity_pct` – jeder andere Wert wird abgelehnt |
| `operator` | string | ja | **Nur** `gt`, `gte`, `lt`, `lte` |
| `threshold` | number | ja | Schwellwert |
| `hysteresis` | number | nein | Muss `≥ 0` sein, sonst Validierungsfehler. Default `0` |

**Beispiel-Request** (ein Ausgang mit Schwellwert-Regel + Hysterese, ein einfacher Eingang):
```bash
curl -X PUT http://192.168.1.50/api/io/config \
  -H "Content-Type: application/json" \
  -d '[
    {
      "id": "lueftungsrelais",
      "type": "output",
      "gpio": 23,
      "label": "Lüfter-Relais",
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
      "label": "Türkontakt",
      "invert": false
    }
  ]'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein Array; fehlende/falsch typisierte Pflichtfelder; ungültiger `type`; `gpio` nicht auf der Stiftleiste oder doppelt vergeben (auch nicht zwischen zwei IOs); `gpio` bereits von einem Sensor belegt; `rule` strukturell ungültig (falsches `field`/`operator`, fehlende Pflichtfelder, `hysteresis < 0`) |

### `POST /api/io/set`

Schaltet einen digitalen Ausgang manuell. Funktioniert **nicht**, solange der Ausgang eine aktive
Schwellwert-Regel hat (dann übernimmt die Regel die Steuerung).

**Auth:** wie global konfiguriert · **Query-Parameter:**

| Parameter | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `id` | string | ja | ID des Ausgangs (aus `/api/io/config`) |

**Body:** `{ "state": bool }`

**Beispiel-Request:**
```bash
curl -X POST "http://192.168.1.50/api/io/set?id=warnleuchte" \
  -H "Content-Type: application/json" \
  -d '{"state": true}'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Query-Parameter `id` fehlt; Feld `state` fehlt oder ist kein Boolean |
| `404 Not Found` | Keine IO mit dieser `id` bekannt (oder es handelt sich um einen Eingang statt eines Ausgangs) |
| `409 Conflict` | Der Ausgang hat eine aktive Schwellwert-Regel – Klartext-Antwort `"Ausgang hat eine aktive Schwellwert-Regel"`. Um manuell zu schalten, zuerst die Regel per `PUT /api/io/config` entfernen |

---

## Netzwerk

### `GET /api/network/config`

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/network/config
```

**Beispiel-Response (200):**
```json
{
  "wifi_ssid": "MeinNetzwerk",
  "wifi_password": "",
  "hostname": "temperaturwatch"
}
```

> `wifi_password` wird aus Sicherheitsgründen **immer** als leerer String zurückgegeben, unabhängig vom
> tatsächlich gespeicherten Passwort.

### `PUT /api/network/config`

**Auth:** wie global konfiguriert · **Content-Type:** `application/json`

| Feld | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `wifi_ssid` | string | ja (muss als String vorhanden sein, leer erlaubt) | Max. 32 Zeichen. Leerer String = WLAN-Station bleibt inaktiv (nur Ethernet) |
| `wifi_password` | string | nein | Max. 63 Zeichen. **Leer/weggelassen = bisheriges Passwort bleibt erhalten** (Write-if-nonempty-Muster, wie beim Login- und MQTT-Passwort) |
| `hostname` | string | nein | Max. 31 Zeichen (mDNS-Name, erreichbar als `<hostname>.local`). Leer/weggelassen = bisheriger Wert bleibt erhalten |

> **Wichtig:** Änderungen werden erst nach einem Neustart wirksam (`POST /api/system/reboot`) – es gibt keinen
> Live-Reconnect.

**Beispiel-Request** (WLAN-Zugangsdaten setzen, Hostname unverändert lassen):
```bash
curl -X PUT http://192.168.1.50/api/network/config \
  -H "Content-Type: application/json" \
  -d '{
    "wifi_ssid": "MeinNetzwerk",
    "wifi_password": "sicheres-passwort123"
  }'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein JSON-Objekt, oder `wifi_ssid` fehlt/ist kein String |

---

## Uhrzeit (NTP)

### `GET /api/time/config`

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/time/config
```

**Beispiel-Response (200):**
```json
{
  "enabled": true,
  "ntp_server": "pool.ntp.org",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
  "synced": true
}
```

| Feld | Zusatz |
|---|---|
| `synced` | **Nur in der GET-Antwort enthalten** (Laufzeit-Status) – wird beim `PUT` ignoriert, falls mitgesendet. Bleibt nach der ersten erfolgreichen Synchronisation dauerhaft `true`, auch wenn NTP danach deaktiviert wird (die zuletzt bekannte Zeit läuft per Systemuhr weiter) |

### `PUT /api/time/config`

**Auth:** wie global konfiguriert · **Content-Type:** `application/json`

| Feld | Typ | Pflicht | Default | Beschreibung |
|---|---|---|---|---|
| `enabled` | bool | nein | `false` (bei explizitem `PUT` ohne dieses Feld) | NTP-Client aktiv/inaktiv. **Wirkt sofort** – kein Neustart nötig, der SNTP-Client wird beim Speichern direkt neu gestartet bzw. gestoppt |
| `ntp_server` | string | nein | `pool.ntp.org` | Max. 63 Zeichen. Hostname oder IP-Adresse eines NTP-Servers. Leerer String fällt auf den Default zurück |
| `timezone` | string | nein | `CET-1CEST,M3.5.0,M10.5.0/3` (Europe/Berlin) | Max. 63 Zeichen, **POSIX-TZ-Syntax** (nicht IANA-Zonennamen wie `Europe/Berlin`!), z.B. `UTC0`, `EST5EDT,M3.2.0,M11.1.0`. Leerer String fällt auf den Default zurück. Wirkt sofort auf alle Zeitausgaben der Firmware (u.a. `utc_offset_s` in [`/api/system/info`](#get-apisysteminfo)) |

> **Wichtig zum allerersten Start:** Ist noch keine Konfiguration gespeichert (Werkszustand bzw. nach Factory-Reset), startet die Firmware **automatisch** mit `enabled:true`, `ntp_server:"pool.ntp.org"` und der Default-Zeitzone – kein manueller Eingriff nötig, damit die Uhr läuft. Ein `PUT` ohne `"enabled"`-Feld deaktiviert NTP hingegen explizit (Konsistenz mit den übrigen Config-Endpunkten) – wer nur `ntp_server`/`timezone` ändern will, muss `"enabled": true` im selben Request mitschicken.

**Beispiel-Request** (Zeitzone auf US-Ostküste umstellen, eigenen NTP-Server verwenden):
```bash
curl -X PUT http://192.168.1.50/api/time/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "ntp_server": "time.cloudflare.com",
    "timezone": "EST5EDT,M3.2.0,M11.1.0"
  }'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein JSON-Objekt (keine weiteren Feld-Validierungen – ein syntaktisch unsinniger `timezone`-String wird nicht abgelehnt, führt aber zu falscher/keiner Sommerzeit-Umrechnung statt eines Fehlers) |

---

## MQTT

### `GET /api/mqtt/config`

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/mqtt/config
```

**Beispiel-Response (200):**
```json
{
  "enabled": true,
  "broker_uri": "mqtt://192.168.1.20:1883",
  "username": "",
  "password": "",
  "client_id": "",
  "base_topic": "temperaturwatch",
  "publish_interval_s": 30,
  "ca_cert": ""
}
```

> `password` wird analog zu `wifi_password` immer maskiert (leerer String) zurückgegeben.

### `PUT /api/mqtt/config`

**Auth:** wie global konfiguriert · **Content-Type:** `application/json`

| Feld | Typ | Pflicht | Default | Beschreibung |
|---|---|---|---|---|
| `enabled` | bool | nein | `false` | MQTT-Client aktiv/inaktiv |
| `broker_uri` | string | **ja, wenn `enabled:true`** | `""` | Max. 127 Zeichen. Muss mit `mqtt://` oder `mqtts://` beginnen, sonst Validierungsfehler |
| `username` | string | nein | `""` | Max. 63 Zeichen |
| `password` | string | nein | – | Max. 63 Zeichen. **Leer/weggelassen = bisheriges Passwort bleibt erhalten** |
| `client_id` | string | nein | `""` (Firmware generiert intern eine ID) | Max. 31 Zeichen |
| `base_topic` | string | nein | `temperaturwatch` | Max. 31 Zeichen. Leerer String fällt auf den Default zurück |
| `publish_interval_s` | number | nein | `30` | Wird intern auf mindestens `5` angehoben, falls kleiner |
| `ca_cert` | string | nein | `""` | PEM-CA-Zertifikat (mehrzeilig) für `mqtts://`-Verbindungen; leer = Systemvalidierung/keine TLS-Prüfung je nach Broker-Setup |

**Beispiel-Request:**
```bash
curl -X PUT http://192.168.1.50/api/mqtt/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "broker_uri": "mqtt://192.168.1.20:1883",
    "base_topic": "temperaturwatch",
    "publish_interval_s": 30
  }'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein JSON-Objekt, oder `enabled:true` mit fehlendem/ungültigem `broker_uri` (muss `mqtt://`/`mqtts://`-Präfix haben) |

**Zur Einordnung – tatsächlich verwendete MQTT-Topics zur Laufzeit** (unterhalb von `base_topic`, Default
`temperaturwatch`), relevant um die per REST konfigurierten Werte auf dem Broker zu beobachten:

| Topic | Richtung | Payload | Beschreibung |
|---|---|---|---|
| `<base>/status` | Publish (retained, LWT) | `online` / `offline` | Verbindungsstatus |
| `<base>/sensor/<id>/temperature_c` | Publish (periodisch) | z.B. `21.50` | Nur bei gültiger Messung |
| `<base>/sensor/<id>/humidity_pct` | Publish (periodisch) | z.B. `62.30` | Nur bei DHT/AM2301 mit gültiger Messung |
| `<base>/io/<id>/state` | Publish (periodisch, retained) | `ON` / `OFF` | Für jeden konfigurierten IO |
| `<base>/io/<id>/set` | Subscribe | `ON`/`OFF`/`1`/`0`/`true`/`false` | Schaltet einen Ausgang – Pendant zu `POST /api/io/set`, unterliegt denselben Regel-Einschränkungen |

---

## SNMP

### `GET /api/snmp/config`

**Auth:** wie global konfiguriert · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/snmp/config
```

**Beispiel-Response (200):**
```json
{
  "enabled": true,
  "community": "public",
  "sys_name": "temperaturwatch",
  "sys_contact": "admin@example.com",
  "sys_location": "Serverraum",
  "listening": true
}
```

| Feld | Zusatz |
|---|---|
| `listening` | **Nur in der GET-Antwort enthalten** (Laufzeit-Status, ob der UDP-Listener aktuell läuft) – wird beim `PUT` ignoriert, falls mitgesendet, und nicht in die gespeicherte Konfiguration übernommen |

### `PUT /api/snmp/config`

**Auth:** wie global konfiguriert · **Content-Type:** `application/json`

| Feld | Typ | Pflicht | Default | Beschreibung |
|---|---|---|---|---|
| `enabled` | bool | nein | `false` | **Ändern erfordert einen Neustart** – lwIP bietet keine Funktion, den UDP-Listener nach dem Start wieder zu stoppen |
| `community` | string | nein | `public` | Max. 31 Zeichen. Leerer String fällt auf `public` zurück. Bei laufendem Listener wird eine Änderung **sofort live** übernommen (kein Neustart nötig) |
| `sys_name` | string | nein | Kconfig-Hostname | Max. 31 Zeichen |
| `sys_contact` | string | nein | `""` | Max. 31 Zeichen |
| `sys_location` | string | nein | `""` | Max. 31 Zeichen |

**Beispiel-Request:**
```bash
curl -X PUT http://192.168.1.50/api/snmp/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "community": "public",
    "sys_contact": "admin@example.com",
    "sys_location": "Serverraum"
  }'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein JSON-Objekt (keine weiteren Feld-Validierungen) |

> Details zur SNMP-MIB-Struktur (private MIB + Standard-MIB-II-`system`-Gruppe für Monitoring-Tools wie
> CheckMK/PRTG) siehe [README → SNMP](../README.md#snmp).

---

## Login (HTTP-Basic-Auth)

### `GET /api/auth/config`

**Auth:** wie global konfiguriert (auch dieser Endpunkt selbst ist bei aktiviertem Login geschützt) · **Body:** keiner

**Beispiel-Request:**
```bash
curl http://192.168.1.50/api/auth/config
```

**Beispiel-Response (200):**
```json
{
  "enabled": true,
  "username": "admin",
  "password_set": true
}
```

> Das Passwort selbst wird **nie** zurückgegeben (weder Klartext noch Hash) – `password_set` zeigt nur an, ob
> überhaupt eines hinterlegt ist.

### `PUT /api/auth/config`

**Auth:** wie global konfiguriert · **Content-Type:** `application/json`

| Feld | Typ | Pflicht | Beschreibung |
|---|---|---|---|
| `enabled` | bool | nein (Default `false`) | Login aktiv/inaktiv. **Kann nicht auf `true` gesetzt werden, solange kein Passwort gesetzt ist** (weder vorher noch im selben Request) |
| `username` | string | nein | Max. 31 Zeichen. Leer/weggelassen = bisheriger Benutzername bleibt, fällt beim allerersten Setzen auf `admin` zurück |
| `password` | string (Klartext) | nein | Wird serverseitig als SHA-256-Hash gespeichert, nie im Klartext persistiert. **Leer/weggelassen = bisheriges Passwort (bzw. bisheriger Hash) bleibt erhalten** |

**Beispiel-Request** (Login aktivieren und Passwort setzen):
```bash
curl -X PUT http://192.168.1.50/api/auth/config \
  -H "Content-Type: application/json" \
  -d '{
    "enabled": true,
    "username": "admin",
    "password": "sicheres-passwort123"
  }'
```

**Beispiel-Response (200):** `true`

**Fehlerfälle (zusätzlich zum allgemeinen Schema):**

| Code | Bedingung |
|---|---|
| `400 Bad Request` | Body kein JSON-Objekt, **oder** `enabled:true` ohne dass (vorher oder in diesem Request) ein Passwort gesetzt wurde – Klartext-Antwort `"Login kann nicht ohne Passwort aktiviert werden"` |

> ⚠️ Wird der Login aktiviert, während man selbst gerade unauthentifiziert verbunden ist, sind alle
> **nachfolgenden** Requests (inkl. Web-UI) sofort betroffen. Zugangsdaten vorher notieren.

---

## Vollständiger Workflow (Beispiel-Skript)

Beispiel: neuen Dallas-Sensor anlegen, dazu einen Ausgang mit Schwellwert-Regel + Hysterese, und den Zustand
überwachen.

```bash
DEVICE=192.168.1.50

# 1. Freie GPIOs anzeigen
curl -s "http://$DEVICE/api/board/pins" | jq

# 2. 1-Wire-Bus an GPIO 4 scannen
curl -s "http://$DEVICE/api/sensors/dallas-scan?gpio=4" | jq
# -> ["28FF641234567890"]

# 3. Aktuelle Sensor-Konfiguration holen, neuen Sensor anhängen, zurückschreiben
curl -s "http://$DEVICE/api/sensors/config" \
  | jq '. + [{"id":"aussen","type":"dallas","gpio":4,"label":"Außen","poll_interval_s":10,"rom_id":"28FF641234567890"}]' \
  | curl -s -X PUT "http://$DEVICE/api/sensors/config" -H "Content-Type: application/json" --data-binary @-

# 4. Ausgang mit Schwellwert-Regel (schaltet ab 25°C, Hysterese 1°C) anlegen
curl -s "http://$DEVICE/api/io/config" \
  | jq '. + [{
      "id":"lueftung","type":"output","gpio":23,"label":"Lüftung",
      "invert":false,"initial_state":false,
      "rule":{"sensor_id":"aussen","field":"temperature_c","operator":"gt","threshold":25,"hysteresis":1}
    }]' \
  | curl -s -X PUT "http://$DEVICE/api/io/config" -H "Content-Type: application/json" --data-binary @-

# 5. Live-Zustand beobachten
watch -n5 "curl -s http://$DEVICE/api/sensors | jq; curl -s http://$DEVICE/api/io | jq"
```

Alle Beispiele setzen [`jq`](https://jqlang.org/) für die JSON-Verarbeitung in der Shell voraus; die REST-API
selbst hat keine Abhängigkeit davon.
