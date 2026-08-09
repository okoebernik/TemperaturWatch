# TemperaturWatch

Firmware für das [Waveshare ESP32-P4-WIFI6-POE-ETH](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH), die
Temperatur/Luftfeuchtigkeit von Dallas-1-Wire- (DS18B20), DHT11- und AM2301-Sensoren ausliest, digitale
Ein-/Ausgänge (z.B. Relais, Alarme) verwaltet und alles über REST-API, MQTT und SNMP bereitstellt. Konfiguration
läuft komplett über ein Web-UI – Erreichbarkeit per Ethernet (immer aktiv) und WLAN (über den onboard
ESP32-C6-Co-Prozessor).

Framework: **ESP-IDF v5.5.4** (kein Arduino). Web-UI: handgeschriebenes Vanilla HTML/CSS/JS, kein Build-Schritt.

## Hardware

- Board: Waveshare ESP32-P4-WIFI6-POE-ETH (ESP32-P4, Rev. v1.3, 32 MB Flash, 32 MB PSRAM)
- WLAN/Bluetooth über einen onboard ESP32-C6-Co-Prozessor (SDIO, via `esp_wifi_remote`/`esp-hosted`)
- Ethernet über internen EMAC + IP101-PHY (immer aktiv, kein Setup nötig)

### 40-Pin-Stiftleiste (2×20) – belegbare GPIOs

Nur diese 28 GPIOs sind für Sensoren/IOs zulässig (in `components/board_pins/board_pins.c` hart hinterlegt,
alle anderen liegen intern an Ethernet-PHY, C6-SDIO-Link, SD-Karte, USB, MIPI-CSI/DSI oder Audio-Codec und werden
von der Firmware verweigert):

| Pin | Signal | Pin | Signal | Pin | Signal | Pin | Signal |
|----|--------|----|--------|----|--------|----|--------|
| 1 | 5V | 11 | GPIO22 | 21 | GPIO1 | 31 | GPIO54 |
| 2 | 3V3 | 12 | GPIO21 | 22 | GPIO2 | 32 | GPIO26 |
| 3 | 5V | 13 | GND | 23 | GPIO36 | 33 | GND |
| 4 | GPIO7 (I2C SDA) | 14 | GPIO20 | 24 | GPIO0 | 34 | GPIO48 |
| 5 | GND | 15 | GPIO5 | 25 | GPIO32 | 35 | GPIO46 |
| 6 | GPIO8 (I2C SCL) | 16 | GPIO6 | 26 | GND | 36 | GPIO53 |
| 7 | GPIO37 (Debug-UART TX) | 17 | GPIO4 | 27 | GPIO25 | 37 | GPIO27 |
| 8 | GPIO23 | 18 | 3V3 | 28 | GPIO24 | 38 | GPIO47 |
| 9 | GPIO38 (Debug-UART RX) | 19 | GND | 29 | GND | 39 | GPIO45 |
| 10 | GND | 20 | GPIO3 | 30 | GPIO33 | 40 | GND |

GPIO7/8 sind standardmäßig I2C, GPIO37/38 teilen sich die Onboard-USB-Debug-UART – beide bleiben im Web-UI frei
zuweisbar (mit Hinweis auf die Standardfunktion). Die aktuelle Belegung liefert zur Laufzeit auch
`GET /api/board/pins`.

**Ethernet-Pins** (interne EMAC/RMII, aus `ETH_ESP32_EMAC_DEFAULT_CONFIG()` für `esp32p4`, identisch mit der
offiziellen Waveshare-Referenz [waveshareteam/ESP32-P4-Platform](https://github.com/waveshareteam/ESP32-P4-Platform)):
MDC=GPIO31, MDIO=GPIO52, PHY-Reset=GPIO51, PHY-Adresse=1 (IP101, konfigurierbar über
`components/net_manager/Kconfig`).

## Build & Flash

Voraussetzung: ESP-IDF v5.5.4 (oder kompatibel) installiert.

```bash
# ESP-IDF-Umgebung laden (Pfad ggf. anpassen)
. $IDF_PATH/export.sh   # oder export.ps1 unter Windows PowerShell

idf.py set-target esp32p4
idf.py build
idf.py -p <COM-Port> flash monitor
```

> **Windows-Sonderfall:** Falls `export.ps1`/`export.sh` mit
> `ESP-IDF Python virtual environment ... not found` abbricht, liegt das an einem system-globalen `python`, das
> nicht zur vom Installer angelegten venv passt. Abhilfe: `IDF_PYTHON_ENV_PATH` vor dem Export explizit setzen,
> z.B. `$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.11_env"`.

Der erste Build lädt automatisch mehrere Managed Components über die ESP Component Registry (Internetzugang
nötig): `esp_wifi_remote`, `esp_hosted`, `mdns`, `onewire_bus`, `ds18b20`.

**Wichtig für dieses Board:** `sdkconfig.defaults` setzt `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, weil das Board
einen ESP32-P4 der Revision v1.3 bestückt (ESP-IDF geht sonst von Rev. ≥ v3.1 aus und der Bootloader verweigert
den Start mit *"requires chip revision in range"*).

## Erstinbetriebnahme

1. Board per Ethernet-Kabel verbinden (funktioniert sofort, keine Konfiguration nötig) **oder** seriell
   verbinden und die IP-Adresse aus dem Log ablesen.
2. Web-UI im Browser öffnen: `http://<ip-adresse>/` oder `http://temperaturwatch.local/` (mDNS-Hostname,
   unter „Netzwerk" änderbar).
3. Optional unter „Netzwerk" WLAN-Zugangsdaten hinterlegen (wirkt erst nach einem Neustart) und Ethernet
   anschließend abstecken.
4. Sensoren unter „Sensoren" anlegen (bei Dallas: GPIO wählen, „Bus scannen" nutzen, um ROM-IDs zu finden).
5. Digitale IOs unter „IOs" anlegen, optional mit Schwellwert-Regel (Sensor → Ausgang) inkl. optionaler
   Hysterese gegen schnelles Ein-/Ausschalten nahe am Schwellwert (Schmitt-Trigger-Verhalten: bei "gt"/"gte"
   schaltet der Ausgang erst wieder aus, wenn der Messwert um die Hysterese unter den Schwellwert fällt; bei
   "lt"/"lte" umgekehrt).
6. MQTT/SNMP nach Bedarf unter den jeweiligen Menüpunkten aktivieren.
7. Empfohlen: unter „System" einen Login (HTTP-Basic-Auth) einrichten, sobald das Gerät im Netzwerk
   erreichbar ist – das Web-UI/die REST-API ist standardmäßig **ohne** Login erreichbar.

## Architektur

Jede Komponente unter `components/` ist eigenständig (Header + Implementierung + eigenes `CMakeLists.txt`) und
über ein `*_start()` in `main/app_main.c` verdrahtet:

| Komponente | Zweck |
|---|---|
| `board_pins` | Statische Whitelist der 28 Header-GPIOs |
| `gpio_registry` | Modul-übergreifende GPIO-Konfliktprüfung (Sensoren ↔ IOs) |
| `config_store` | NVS-Persistenz (cJSON-Blobs je Bereich) |
| `net_manager` | Ethernet (IP101) + WLAN (ESP32-C6/esp-hosted) + mDNS |
| `dallas_sensor` / `dht_sensor` | Treiber (1-Wire-Bus-Scan bzw. Bit-Banging) |
| `sensor_manager` | Sensor-Konfiguration, Poll-Scheduler, Live-Werte |
| `io_manager` | Digitale Ein-/Ausgänge, Schwellwert-Regeln |
| `mqtt_manager` | esp-mqtt-Client, Publish/Subscribe |
| `time_manager` | SNTP-Client (Default-Server `pool.ntp.org`) + konfigurierbare Zeitzone (POSIX-TZ) |
| `snmp_agent` | Eigener SNMPv1/v2c-Agent auf Basis der lwIP-Protokoll-Engine |
| `auth_manager` | HTTP-Basic-Auth (Passwort als SHA-256-Hash gespeichert) |
| `rest_api` | esp_http_server: REST-Routen + Web-UI-Auslieferung aus SPIFFS |
| `web/` | Quelle des Web-UI (wird als SPIFFS-Image in die `www`-Partition geflasht) |

## REST-API

Alle Routen unter `/api/...`, JSON-Bodies. Bei aktiviertem Login: HTTP-Basic-Auth erforderlich (gilt auch für
das Web-UI selbst).

> 📖 **Ausführliche Dokumentation mit Feldbeschreibungen, Validierungsregeln, Beispiel-Requests (`curl`) und
> Beispiel-Responses für jeden einzelnen Endpunkt: [docs/REST_API.md](docs/REST_API.md).**

| Route | Methoden | Zweck |
|---|---|---|
| `/api/system/info` | GET | Chip/Firmware-Info, Netzwerk-/MQTT-Status, freier Heap |
| `/api/system/reboot` | POST | Neustart |
| `/api/system/factory-reset` | POST | Alle Konfigurationsbereiche löschen + Neustart |
| `/api/system/ota` | POST (raw `.bin`) | Firmware-Update (App-Partition), automatischer Neustart |
| `/api/system/ota-web` | POST (raw `.bin`) | Web-UI-Update (`www`-Partition), kein Neustart nötig |
| `/api/board/pins` | GET | Liste der 28 Header-GPIOs inkl. Hinweistext |
| `/api/sensors` | GET | Live-Messwerte |
| `/api/sensors/config` | GET/PUT | Sensor-Konfiguration (Array, komplett ersetzt) |
| `/api/sensors/dallas-scan?gpio=N` | GET | 1-Wire-Bus-Scan, liefert gefundene ROM-IDs |
| `/api/io` | GET | Live-IO-Zustände |
| `/api/io/config` | GET/PUT | IO-Konfiguration (Array, komplett ersetzt) |
| `/api/io/set?id=X` | POST `{state}` | Ausgang manuell schalten (nicht bei aktiver Regel) |
| `/api/network/config` | GET/PUT | WLAN-SSID/-Passwort, Hostname |
| `/api/time/config` | GET/PUT | NTP-Server, Zeitzone (POSIX-TZ), Sync-Status |
| `/api/mqtt/config` | GET/PUT | Broker, Zugangsdaten, Topics, TLS-CA-Zertifikat |
| `/api/snmp/config` | GET/PUT | Community, sysName/Contact/Location |
| `/api/auth/config` | GET/PUT | Login aktivieren, Benutzername/Passwort |

Sensor-/IO-Konfiguration wird als vollständiges Array übertragen (PUT ersetzt die gesamte Liste) – das Web-UI
lädt die aktuelle Liste, ändert einen Eintrag lokal und sendet das komplette Array zurück.

## Hilfe im Web-UI

Die REST-API-, MQTT- und PRTG-Anleitungen (`docs/REST_API.md`, `docs/MQTT_SETUP.md`, `docs/PRTG_SETUP.md`)
sind zusätzlich direkt im Gerät unter „Hilfe" verfügbar – als vorgerenderte HTML-Fragmente in
`web/help/*.{de,en}.html` (aus den Markdown-Quellen erzeugt via `scripts/md_to_help_html.py`, kein
Markdown-Parser zur Laufzeit auf dem Gerät nötig), inkl. englischer Übersetzung (`docs/*.en.md`) und
Umschaltung über denselben DE/EN-Sprachschalter wie der Rest des Web-UI. Beim Bearbeiten eines `docs/*.md`
müssen die zugehörigen HTML-Fragmente manuell neu generiert werden:

```bash
python scripts/md_to_help_html.py docs/REST_API.md web/help/rest-api.de.html
python scripts/md_to_help_html.py docs/REST_API.en.md web/help/rest-api.en.html
# analog für MQTT_SETUP.md/.en.md -> mqtt.de/en.html und PRTG_SETUP.md/.en.md -> prtg.de/en.html
```

> Cross-Referenzen der Art „siehe README" bzw. zwischen den drei Anleitungen verlinken im Quelltext relativ
> (`../README.md`, `PRTG_SETUP.md` etc.) – das funktioniert beim Lesen auf GitHub. `scripts/md_to_help_html.py`
> schreibt diese Links beim Konvertieren automatisch um: README-Referenzen zeigen auf die echte GitHub-URL
> des Repos, Cross-Doc-Referenzen zwischen den Anleitungen werden zu SPA-internen Sprungmarken (kein
> Seitenwechsel, bleibt innerhalb der In-App-Hilfe).

## Uhrzeit (NTP)

Statuszeile am oberen Rand des Web-UI zeigt durchgehend MQTT-, SNMP- und Netzwerk-Status sowie eine live
tickende Uhrzeit. Die Zeit wird per SNTP synchronisiert (Standard-Server `pool.ntp.org`, unter „Netzwerk" im
Web-UI konfigurierbar), inkl. frei wählbarer Zeitzone (POSIX-TZ-Syntax, kuratierte Auswahl gängiger Regionen
plus Freitext-Option für alles andere). Ab Werk bereits aktiviert (Default-Zeitzone Europe/Berlin) – kein
manueller Eingriff nötig, damit die Uhr läuft, sobald eine Netzwerkverbindung besteht. Änderungen wirken
sofort, kein Neustart nötig. Details: [docs/REST_API.md → Uhrzeit (NTP)](docs/REST_API.md#uhrzeit-ntp).

## MQTT

> 📖 **Ausführliche Anleitung** (Broker-Einrichtung im Web-UI, TLS, IO-Steuerung, Home-Assistant-/Node-RED-Beispiele,
> Troubleshooting): [docs/MQTT_SETUP.md](docs/MQTT_SETUP.md).

Topics unterhalb des konfigurierten Basis-Topics (Default `temperaturwatch`):

- `<base>/status` – `online`/`offline` (retained, Last Will)
- `<base>/sensor/<id>/temperature_c`, `<base>/sensor/<id>/humidity_pct` – periodisch veröffentlicht
- `<base>/io/<id>/state` – `ON`/`OFF`, periodisch veröffentlicht
- `<base>/io/<id>/set` – abonniert, Payload `ON`/`OFF`/`1`/`0`/`true`/`false` schaltet einen Ausgang

## SNMP

Eigener SNMPv1/v2c-Agent (UDP/161) mit privater MIB unter der Platzhalter-OID `1.3.6.1.4.1.99999.1`
(PEN 99999 ist **nicht** bei der IANA registriert – bei Bedarf durch eine eigene ersetzen).

| OID-Suffix (Basis `1.3.6.1.4.1.99999.1`) | Inhalt |
|---|---|
| `.1.1.0` … `.1.5.0` | sysDescr, sysUpTime, sysName, sysContact, sysLocation |
| `.2.1.0` | sensorCount |
| `.2.2.1.{1..8}.<n>` | sensorTable: Index, ID, Label, Typ, Temperatur×10, Feuchte×10, gültig, Alter(s) |
| `.3.1.0` | ioCount |
| `.3.2.1.{1..5}.<n>` | ioTable: Index, ID, Label, Typ, Zustand |

Zusätzlich ist dieselbe System-Info vollständig unter der **Standard-MIB-II-`system`-Gruppe** erreichbar
(`1.3.6.1.2.1.1.{1..7}.0` = sysDescr, sysObjectID, sysUpTime, sysContact, sysName, sysLocation, sysServices,
Standard-Nummerierung). Das ist wichtig für Monitoring-Tools wie CheckMK/Zabbix/PRTG: die fragen bei der
Geräte-Erkennung zuerst sysDescr **und sysObjectID** ab, um überhaupt festzustellen, dass ein Host per SNMP
antwortet – ein Agent, der nur die private MIB bedient, wird von solchen Tools trotz funktionierender
SNMP-Antworten als "nicht erreichbar" eingestuft (CheckMK-Fehlermeldung z.B. "Cannot fetch system object OID
.1.3.6.1.2.1.1.2.0"). `sysObjectID` zeigt mangels registrierter PEN auf die eigene private Basis-OID
(`1.3.6.1.4.1.99999.1`), `sysServices` meldet `72` (Layer 4 + 7, Endpunkt-Gerät nach RFC 1213).

**Bekannte Einschränkung:** Es werden bewusst keine automatischen MIB-II-Paketzähler (ifInOctets etc.)
bereitgestellt – lwIPs fertiges `snmp_mib2`-Modul hätte einen inkompatiblen Speicherlayout-Konflikt mit dem
Rest des (unverändert kompilierten) lwIP-Frameworks verursacht. Details im Kommentar in
`components/snmp_agent/snmp_agent.c`. SNMP-Aktivierung/-Deaktivierung erfordert einen Neustart (lwIP bietet
keine Funktion, den UDP-Listener nach dem Start wieder zu stoppen).

**Monitoring-Integration:** Schritt-für-Schritt-Anleitung für PRTG (native `SNMP Custom Table`-Sensoren, kein
Plugin nötig) unter [docs/PRTG_SETUP.md](docs/PRTG_SETUP.md). Ein fertiges CheckMK-Plugin liegt unter
[contrib/checkmk/](contrib/checkmk/).

## Sicherheit

- HTTP-Basic-Auth ist standardmäßig **deaktiviert** (Web-UI/API frei erreichbar), um eine versehentliche
  Aussperrung bei Werksauslieferung zu vermeiden. Aktivierung unter „System" empfohlen, sobald das Gerät im
  Netzwerk hängt.
- Passwörter (Login, MQTT) werden nie im Klartext an das Frontend zurückgegeben; ein leeres Passwort-Feld beim
  Speichern behält das zuvor gesetzte Passwort bei. Das Login-Passwort wird nur als SHA-256-Hash persistiert.

## OTA-Updates

Firmware-Updates lassen sich per `POST /api/system/ota` einspielen (rohes `.bin` als Request-Body, kein
JSON) – z.B. per curl:

```bash
curl -X POST --data-binary @build/temperaturwatch.bin http://<ip>/api/system/ota
```

Im Web-UI unter „System" → „Firmware-Update" steht dafür ein Datei-Upload mit Fortschrittsanzeige bereit. Das
Image wird in den jeweils inaktiven `ota_0`/`ota_1`-Slot geschrieben; bei Erfolg startet das Gerät automatisch
damit neu. Startet die neue Firmware nicht sauber (Absturz/Hänger vor Erreichen der Hauptschleife), rollt der
Bootloader dank `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` automatisch auf die vorherige Version zurück.

> **Hinweis:** `/api/system/ota` aktualisiert **nur die Firmware** (App-Partition). Das Web-UI (`web/*`) liegt
> in einer separaten SPIFFS-Partition (`www`) und hat einen eigenen OTA-Endpunkt: `POST /api/system/ota-web`
> nimmt das per `idf.py build` erzeugte `build/www.bin` entgegen und schreibt es direkt in die `www`-Partition
> – kein Neustart nötig, kein USB erforderlich. Im Web-UI steht dafür der zweite Upload-Bereich „Web-UI-Update"
> bereit. Details zu beiden Endpunkten: [docs/REST_API.md](docs/REST_API.md#post-apisystemota-web).

```bash
curl -X POST --data-binary @build/www.bin http://<ip>/api/system/ota-web
```

## Bekannte Grenzen / mögliche Erweiterungen

- WLAN- und SNMP-Konfigurationsänderungen wirken erst nach einem Neustart (kein Live-Reconnect).
- Digitale Eingänge nutzen einen internen Pull-up ohne konfigurierbare Entprellung.
- Statische IP-Konfiguration (statt DHCP) ist nicht implementiert.
