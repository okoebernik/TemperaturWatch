# TemperaturWatch über MQTT anbinden

Anleitung zur Einrichtung der MQTT-Integration: Broker-Konfiguration im Web-UI, vollständige Topic-Referenzmit
Beispiel-Payloads, IO-Steuerung per MQTT, TLS-Absicherung sowie Beispiele für Home Assistant, Node-RED und die
Kommandozeile (`mosquitto_pub`/`mosquitto_sub`). Für REST-API und SNMP siehe
[docs/REST_API.md](REST_API.md) bzw. [docs/PRTG_SETUP.md](PRTG_SETUP.md).

## Inhalt

- [Voraussetzungen](#voraussetzungen)
- [1. MQTT im Web-UI einrichten](#1-mqtt-im-web-ui-einrichten)
- [2. Verbindung testen](#2-verbindung-testen)
- [3. Topic-Referenz](#3-topic-referenz)
- [4. Sensor-Werte empfangen](#4-sensor-werte-empfangen)
- [5. Digitale Ausgänge per MQTT schalten](#5-digitale-ausgänge-per-mqtt-schalten)
- [6. Verbindungsstatus überwachen (Last Will)](#6-verbindungsstatus-überwachen-last-will)
- [7. TLS absichern (mqtts://)](#7-tls-absichern-mqtts)
- [8. Home Assistant einbinden](#8-home-assistant-einbinden)
- [9. Node-RED einbinden](#9-node-red-einbinden)
- [Troubleshooting](#troubleshooting)
- [Sicherheitshinweise](#sicherheitshinweise)

## Voraussetzungen

- Ein erreichbarer MQTT-Broker (z.B. [Mosquitto](https://mosquitto.org/), EMQX, HiveMQ, oder ein Cloud-Broker).
  Für lokale Tests reicht eine einfache Mosquitto-Instanz ohne Zusatzkonfiguration.
- Netzwerkverbindung vom TemperaturWatch-Gerät zum Broker (Standardport **1883** unverschlüsselt, **8883** für
  TLS – abhängig von der Broker-Konfiguration).
- Optional: [`mosquitto_pub`/`mosquitto_sub`](https://mosquitto.org/man/mosquitto_pub-1.html) auf einem
  Test-Rechner, um Topics ohne zusätzliche Software zu beobachten/zu senden.

## 1. MQTT im Web-UI einrichten

1. Web-UI öffnen → Menüpunkt **„MQTT"**.
2. **„Aktiviert"** einschalten.
3. **Broker-URI** eintragen – **Pflichtfeld, sobald aktiviert**, muss mit `mqtt://` (unverschlüsselt) oder
   `mqtts://` (TLS, siehe [Abschnitt 7](#7-tls-absichern-mqtts)) beginnen, z.B.:
   ```
   mqtt://192.168.1.20:1883
   ```
4. **Benutzername/Passwort** nur ausfüllen, falls der Broker Authentifizierung verlangt (leer lassen = anonyme
   Verbindung). Ein leer gelassenes Passwort beim späteren Bearbeiten behält das zuvor gesetzte bei – es wird
   aus Sicherheitsgründen nie im Klartext zurückgegeben.
5. **Basis-Topic** – Präfix aller Topics dieses Geräts (Default `temperaturwatch`). Bei **mehreren
   TemperaturWatch-Geräten am selben Broker** hier pro Gerät einen eindeutigen Wert vergeben (z.B.
   `temperaturwatch/keller`, `temperaturwatch/dachboden`), sonst überschreiben sich die retained Messages der
   Geräte gegenseitig.
6. **Publish-Intervall (s)** – wie oft Sensor-/IO-Werte automatisch veröffentlicht werden (Default 30s,
   Minimum 5s – kleinere Werte werden serverseitig auf 5s angehoben).
7. **Client-ID** optional – leer lassen, dann generiert die MQTT-Bibliothek automatisch eine eindeutige ID.
   Explizit setzen, wenn der Broker feste Client-IDs erwartet (z.B. für ACLs) oder bei mehreren Geräten, um sie
   in Broker-Logs eindeutig zu unterscheiden.
8. **Speichern.**

> **Kein Neustart nötig:** Anders als WLAN- oder SNMP-Einstellungen wirkt eine MQTT-Konfigurationsänderung
> **sofort** – der bestehende Client wird beim Speichern automatisch getrennt und mit der neuen Konfiguration
> neu verbunden.

## 2. Verbindung testen

Alle Topics auf einmal live mitschneiden (Test-Rechner mit `mosquitto_sub`, im selben Netz wie der Broker):

```bash
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/#" -v
```

Bei Erfolg erscheint direkt nach dem Verbindungsaufbau des Geräts:
```
temperaturwatch/status online
temperaturwatch/sensor/aussentemperatur/temperature_c 21.50
temperaturwatch/sensor/keller/temperature_c 17.80
temperaturwatch/sensor/keller/humidity_pct 62.30
temperaturwatch/io/lueftungsrelais/state ON
```

Erscheint nichts: siehe [Troubleshooting](#troubleshooting). Alternativ zeigt das Web-UI unter „MQTT" einen
Status-Badge (**verbunden** / **getrennt** / **deaktiviert**) sowie unter „System" → „Geräteinformationen" den
Live-Verbindungsstatus.

## 3. Topic-Referenz

Alle Topics unterhalb des konfigurierten **Basis-Topics** (`<base>`, Default `temperaturwatch`):

| Topic | Richtung | QoS | Retained | Payload | Beschreibung |
|---|---|---|---|---|---|
| `<base>/status` | Publish | 1 | ja | `online` / `offline` | Verbindungsstatus; `offline` wird als **Last Will** vom Broker gesendet, falls die Verbindung unsauber abbricht (siehe [Abschnitt 6](#6-verbindungsstatus-überwachen-last-will)) |
| `<base>/sensor/<id>/temperature_c` | Publish | 0 | nein | z.B. `21.50` | Nur wenn eine gültige Messung vorliegt; `<id>` = Sensor-`id` aus der Konfiguration |
| `<base>/sensor/<id>/humidity_pct` | Publish | 0 | nein | z.B. `62.30` | Nur bei DHT11/AM2301 mit gültiger Messung (Dallas-Sensoren liefern kein Feuchte-Topic) |
| `<base>/io/<id>/state` | Publish | 0 | **ja** | `ON` / `OFF` | Für jeden konfigurierten IO (Ein- **und** Ausgänge), bei jedem Publish-Zyklus |
| `<base>/io/<id>/set` | **Subscribe** | 1 | – | `ON`/`OFF`/`1`/`0`/`true`/`false` | Schaltet einen Ausgang – siehe [Abschnitt 5](#5-digitale-ausgänge-per-mqtt-schalten) |

**Timing:** Der erste Publish-Zyklus (alle Sensoren/IOs) läuft **unmittelbar nach dem Verbindungsaufbau**, nicht
erst nach Ablauf des ersten Intervalls. Danach im konfigurierten `publish_interval_s`-Takt.

**Retained vs. nicht retained:** `status` und `io/<id>/state` sind *retained* – ein neu verbundener Client
(z.B. `mosquitto_sub`, Node-RED, Home Assistant) sieht den letzten bekannten Wert sofort, auch ohne dass das
Gerät gerade neu published hat. Temperatur-/Feuchte-Werte sind **nicht** retained, damit alte Messwerte nicht
dauerhaft als "aktuell" hängen bleiben, wenn ein Sensor entfernt oder das Gerät lange offline ist.

## 4. Sensor-Werte empfangen

Payload ist jeweils ein reiner Zahlenstring (`%.2f`, Punkt als Dezimaltrennzeichen), **kein JSON** – bewusst
minimalistisch, um mit praktisch jedem MQTT-fähigen System (Home Assistant, Node-RED, Grafana via
MQTT-Datenquelle, ioBroker, …) ohne Payload-Parsing kompatibel zu sein.

```bash
# Einzelnen Sensor beobachten
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/sensor/aussentemperatur/temperature_c"
# -> 21.50
# -> 21.60
# -> 21.50
```

Ein Sensor ohne gültige Messung (Fühler nicht angeschlossen, Lesefehler) veröffentlicht schlicht **kein**
Update für diesen Zyklus – es gibt kein explizites Fehler-Topic. Um Sensorausfälle zu erkennen, eignet sich
entweder die REST-API (`has_reading`/`last_read_ok` in [`GET /api/sensors`](REST_API.md#get-apisensors)) oder
ein Timeout auf Anwendungsseite (z.B. Home-Assistant-`expire_after`, siehe [Abschnitt 8](#8-home-assistant-einbinden)).

## 5. Digitale Ausgänge per MQTT schalten

Publish auf `<base>/io/<id>/set` mit einem der folgenden Payloads: `ON`, `on`, `true`, `1` → schaltet ein;
**alles andere** (auch `OFF`, `false`, `0`, leerer String) → schaltet aus. Groß-/Kleinschreibung ist bei
`ON`/`TRUE` egal, bei `1` muss es exakt die Ziffer `1` sein.

```bash
mosquitto_pub -h 192.168.1.20 -t "temperaturwatch/io/lueftungsrelais/set" -m "ON"
mosquitto_pub -h 192.168.1.20 -t "temperaturwatch/io/lueftungsrelais/set" -m "OFF"
```

Der neue Zustand wird danach automatisch (innerhalb des nächsten Publish-Zyklus) auf
`<base>/io/lueftungsrelais/state` bestätigt.

> ⚠️ **Regel-Konflikt:** Genau wie beim manuellen Schalten über REST-API/Web-UI wirkt `.../set` **nicht** bei
> Ausgängen mit aktiver Schwellwert-Regel (siehe [`POST /api/io/set`](REST_API.md#post-apiioset)) – die
> Regel behält in dem Fall die Kontrolle. Der MQTT-Handler quittiert das nicht mit einem Fehler-Topic, es
> erscheint nur eine Warnung im Geräte-Log; von außen ist nur erkennbar, dass sich `.../state` trotz gesendetem
> `.../set` nicht ändert.

## 6. Verbindungsstatus überwachen (Last Will)

`<base>/status` wird direkt beim Verbindungsaufbau als `online` (retained) veröffentlicht. Bricht die
Verbindung **unsauber** ab (Stromausfall, Netzwerkausfall, Absturz – nicht bei geplantem Neustart über die
Web-UI), erkennt der **Broker** dies über das MQTT-Last-Will-Mechanismus und veröffentlicht automatisch
`offline` (retained) an derselben Stelle – auch wenn das Gerät selbst längst nicht mehr antwortet. Das eignet
sich gut als einfacher, brokerseitig garantierter Online/Offline-Indikator, unabhängig von Sensor-Timeouts:

```bash
mosquitto_sub -h 192.168.1.20 -t "temperaturwatch/status" -v
# -> temperaturwatch/status online
# (Gerät stromlos geschaltet)
# -> temperaturwatch/status offline
```

## 7. TLS absichern (mqtts://)

Für Verbindungen über unsichere Netze (Internet, gemeinsam genutzte VLANs) sollte TLS verwendet werden:

1. Broker-URI auf `mqtts://` umstellen, z.B. `mqtts://mein-broker.example.com:8883`.
2. Im Feld **„CA-Zertifikat (für mqtts://, optional)"** das PEM-kodierte CA-Zertifikat des Brokers einfügen
   (mehrzeilig, inkl. `-----BEGIN CERTIFICATE-----`/`-----END CERTIFICATE-----`). Bei öffentlich signierten
   Zertifikaten (z.B. Let's Encrypt über einen Cloud-Broker) reicht oft das Root-CA-Zertifikat der jeweiligen
   Zertifizierungsstelle; bei selbstsignierten/internen Brokern das eigene CA- bzw. Server-Zertifikat.
3. Speichern – die Verbindung wird sofort mit TLS neu aufgebaut (kein Neustart nötig, siehe
   [Abschnitt 1](#1-mqtt-im-web-ui-einrichten)).

Client-Zertifikate (mTLS) werden derzeit **nicht** unterstützt – nur Server-Zertifikatsprüfung über das
hinterlegte CA-Zertifikat sowie optional Benutzername/Passwort-Authentifizierung über TLS.

## 8. Home Assistant einbinden

Die Firmware implementiert **kein** MQTT-Discovery-Protokoll (`homeassistant/...`-Topics) – Entitäten müssen
manuell in der `configuration.yaml` angelegt werden. Beispiel für einen Temperatur-/Feuchte-Sensor und ein
schaltbares Relais:

```yaml
mqtt:
  sensor:
    - name: "Außentemperatur"
      state_topic: "temperaturwatch/sensor/aussentemperatur/temperature_c"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      expire_after: 120   # als "unavailable" markieren, wenn 2 Zyklen (Default-Intervall 30s×4) ausbleiben
    - name: "Keller Luftfeuchte"
      state_topic: "temperaturwatch/sensor/keller/humidity_pct"
      unit_of_measurement: "%"
      device_class: humidity
      state_class: measurement
      expire_after: 120

  switch:
    - name: "Lüfter-Relais"
      state_topic: "temperaturwatch/io/lueftungsrelais/state"
      command_topic: "temperaturwatch/io/lueftungsrelais/set"
      payload_on: "ON"
      payload_off: "OFF"
      state_on: "ON"
      state_off: "OFF"
      # Bei Ausgängen mit aktiver Schwellwert-Regel wirkt command_topic nicht (siehe Abschnitt 5) -
      # der switch zeigt dann zwar den Zustand korrekt an, lässt sich aber nicht manuell umschalten.
```

`expire_after` ist empfehlenswert, da die Firmware – wie in [Abschnitt 4](#4-sensor-werte-empfangen) beschrieben
– bei Sensorausfall einfach kein Update mehr sendet, statt einen Fehlerwert zu veröffentlichen; ohne
`expire_after` würde Home Assistant sonst dauerhaft den letzten (veralteten) Messwert als aktuell anzeigen.

## 9. Node-RED einbinden

Ein `mqtt in`-Node auf das Topic `temperaturwatch/#` (Wildcard) abonnieren, liefert `msg.topic` und
`msg.payload` (String) für jede Nachricht. Beispiel-Function-Node zum Aufsplitten:

```javascript
const parts = msg.topic.split("/");
// ["temperaturwatch", "sensor", "<id>", "temperature_c"] bzw. ["temperaturwatch", "io", "<id>", "state"]
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

Zum Schalten: `mqtt out`-Node auf `temperaturwatch/io/<id>/set` mit Payload `ON`/`OFF` (z.B. aus einem
Dashboard-Switch-Node gespeist).

## Troubleshooting

| Symptom | Wahrscheinliche Ursache | Lösung |
|---|---|---|
| Web-UI-Badge zeigt dauerhaft „getrennt" | Broker nicht erreichbar, falscher Port, Firewall | `mosquitto_sub`/`telnet <broker-ip> 1883` von einem anderen Rechner im selben Netz testen; Broker-Log auf ankommende Verbindungsversuche prüfen |
| Speichern schlägt mit 400 fehl | `broker_uri` fehlt oder beginnt nicht mit `mqtt://`/`mqtts://` | Präfix prüfen, siehe [Abschnitt 1](#1-mqtt-im-web-ui-einrichten) |
| Verbindung baut auf, bricht aber sofort wieder ab (`status` pendelt online/offline) | Broker verlangt Authentifizierung, aber Benutzername/Passwort fehlen oder sind falsch; oder Client-ID-Kollision mit einem anderen Client am selben Broker | Zugangsdaten prüfen; falls mehrere Geräte/Clients dieselbe feste Client-ID verwenden, eindeutige Client-IDs vergeben oder das Feld leer lassen (automatische Generierung) |
| `mqtts://`-Verbindung schlägt fehl, `mqtt://` funktioniert | CA-Zertifikat fehlt/falsch, oder Broker-Zertifikat nicht durch das hinterlegte CA-Zertifikat gedeckt | Zertifikatskette prüfen (`openssl s_client -connect <broker>:8883 -showcerts`); exakt das Root-/Intermediate-CA-Zertifikat einfügen, das den Broker signiert hat |
| `.../set` ändert `.../state` nicht | Ausgang hat eine aktive Schwellwert-Regel | Regel im Web-UI unter „IOs" entfernen, oder Regel-Bedingung so anpassen, dass der gewünschte Zustand erreicht wird – manuelles Schalten ist bei aktiver Regel grundsätzlich gesperrt |
| Feuchte-Topic erscheint nie für einen Sensor | Sensor ist vom Typ `dallas` (liefert konstruktionsbedingt keine Feuchte) | Erwartet – nur DHT11/AM2301 veröffentlichen `humidity_pct` |
| Werte werden erst nach langer Verzögerung sichtbar | `publish_interval_s` hoch eingestellt | Intervall im Web-UI unter „MQTT" reduzieren (Minimum 5s); Erst-Publish nach Verbindungsaufbau erfolgt aber immer sofort |

## Sicherheitshinweise

- Ist das Web-UI-Login (HTTP-Basic-Auth, siehe [`/api/auth/config`](REST_API.md#login-http-basic-auth))
  aktiviert, schützt das **nur** den Zugriff auf Web-UI/REST-API – **nicht** den MQTT-Zugang. Der Schutz des
  MQTT-Kanals liegt vollständig beim Broker (Benutzername/Passwort, ACLs, TLS).
- MQTT-Zugangsdaten und CA-Zertifikat werden wie alle anderen Konfigurationsdaten unverschlüsselt im internen
  NVS-Speicher abgelegt, aber nie unmaskiert über REST-API/Web-UI zurückgegeben (siehe
  [Grundlagen der REST-API](REST_API.md#grundlagen)).
- Standard-QoS für Sensor-Werte ist bewusst **0** (kein Zustellnachweis, kein Doppel-Handling nötig) – bei
  instabilen Verbindungen können daher einzelne Messwerte verloren gehen, ohne dass das im laufenden Betrieb
  auffällt. Für kritische Anwendungsfälle (z.B. Alarmausgänge) empfiehlt sich zusätzlich eine Überwachung über
  REST-API-Polling oder SNMP als Gegenprobe, statt sich ausschließlich auf MQTT zu verlassen.
- Bei Broker-Zugriff aus dem Internet: unbedingt TLS (`mqtts://`) **und** Authentifizierung verwenden, sowie
  broker-seitige ACLs, damit nicht jeder verbundene Client beliebige Ausgänge schalten kann (die Firmware
  selbst prüft beim `.../set`-Handler keine Berechtigung – jeder, der auf das Topic publishen kann, kann den
  jeweiligen Ausgang schalten).
