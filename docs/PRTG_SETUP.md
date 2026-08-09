# TemperaturWatch in PRTG einbinden

Anleitung zur Einrichtung von PRTG Network Monitor für TemperaturWatch-Geräte über den eingebauten
SNMPv1/v2c-Agenten. Anders als bei [CheckMK](../contrib/checkmk/) gibt es hier **kein eigenes Plugin** zu
installieren – PRTG spricht die private MIB direkt über seine eingebauten SNMP-Sensortypen an, komplett über
die Web-Oberfläche konfigurierbar, ohne Skript-Deployment auf dem Probe-Server. Für Nutzer, die mehr
Flexibilität brauchen (z.B. Zugriff auf IO-Regeln/Hysterese), ist am Ende ein optionaler
REST-API-basierter Custom-Sensor beschrieben.

## Inhalt

- [Voraussetzungen](#voraussetzungen)
- [1. Gerät in PRTG anlegen](#1-gerät-in-prtg-anlegen)
- [2. Grundprüfung: Erreichbarkeit](#2-grundprüfung-erreichbarkeit)
- [3. Sensor-Messwerte einbinden (SNMP Custom Table)](#3-sensor-messwerte-einbinden-snmp-custom-table)
- [4. Kanäle skalieren, benennen, Limits setzen](#4-kanäle-skalieren-benennen-limits-setzen)
- [5. Digitale IOs einbinden (optional)](#5-digitale-ios-einbinden-optional)
- [6. Neue Sensoren später hinzufügen](#6-neue-sensoren-später-hinzufügen)
- [Bekannte Einschränkung: Index-Verschiebung](#bekannte-einschränkung-index-verschiebung)
- [Troubleshooting](#troubleshooting)
- [Alternative: REST-API-basierter Custom-Sensor](#alternative-rest-api-basierter-custom-sensor)
- [OID-Referenz](#oid-referenz)

## Voraussetzungen

1. **SNMP-Agent auf dem Gerät aktivieren:** Web-UI → „SNMP" → „Aktiviert" ✔, Community-String setzen
   (Default `public`), speichern, **Gerät neu starten** (SNMP-Aktivierung wirkt erst nach einem Neustart – lwIP
   kann den UDP-Listener nicht live starten/stoppen).
2. **Netzwerk:** UDP/161 muss vom PRTG-Probe-Server (Core-Server bei lokalem Probe, oder Remote-Probe-Rechner)
   aus erreichbar sein – ggf. Firewall-Regel prüfen.
3. **SNMP-Version:** Der Agent unterstützt **SNMPv1 und v2c** (kein v3/keine Verschlüsselung). In PRTG bei den
   Geräte-Credentials entsprechend „SNMP v1" oder „SNMP v2c" wählen, nicht v3.
4. PRTG-Version: jede aktuelle Version mit den Standard-SNMP-Sensortypen (kein Zusatz-Plugin/Custom-Sensor-Ordner
   für den empfohlenen Weg über „SNMP Custom Table" nötig).

## 1. Gerät in PRTG anlegen

1. Im gewünschten Ordner/in der gewünschten Gruppe: **Gerät hinzufügen**.
2. **IP-Adresse/DNS-Name** des TemperaturWatch-Geräts eintragen (z.B. `192.168.1.50` oder
   `temperaturwatch.local`, falls per mDNS/DNS auflösbar).
3. Unter **„Anmeldedaten für SNMP-Geräte"** (Credentials for SNMP Devices) in den Geräte-Einstellungen:
   - **SNMP-Version:** v1 oder v2c
   - **Community-String:** wie im Web-UI unter „SNMP" hinterlegt (Default `public`)
   - **Port:** 161 (Default, i.d.R. nicht ändern)
4. Assistent „Automatische Erkennung" (Auto-Discovery) beim Anlegen **überspringen oder nur mit dem
   Standard-Template laufen lassen** – die generischen Discovery-Templates kennen unsere private MIB nicht und
   würden nur die Standard-MIB-II-Sensoren (System, Interfaces) finden. Die eigentlichen Sensor-/IO-Werte
   werden in Schritt 3 gezielt hinzugefügt.

## 2. Grundprüfung: Erreichbarkeit

Bevor die eigentlichen Messwert-Sensoren angelegt werden, lohnt sich ein einfacher Konnektivitätstest:

1. Am Gerät: **Sensor hinzufügen** → nach `SNMP Uptime` suchen → hinzufügen.
2. Nach kurzer Zeit sollte der Sensor auf **Grün (Up)** wechseln und die Laufzeit seit dem letzten Boot
   anzeigen.

Das funktioniert, weil die Firmware neben der privaten MIB auch die **Standard-MIB-II-`system`-Gruppe**
(`1.3.6.1.2.1.1.*`) bedient – ohne die würden generische SNMP-Tools (PRTG genauso wie CheckMK, siehe unser
früheres CheckMK-Problem mit `sysObjectID`) das Gerät bei der Erkennung als nicht erreichbar einstufen, obwohl
die private MIB technisch längst antwortet. Schlägt dieser Test fehl, liegt es fast immer an Community-String,
SNMP-Version oder Firewall – siehe [Troubleshooting](#troubleshooting), bevor mit Schritt 3 fortgefahren wird.

## 3. Sensor-Messwerte einbinden (SNMP Custom Table)

PRTGs Sensortyp **„SNMP Custom Table"** liest eine SNMP-Tabelle aus und legt pro gefundener Zeile (= pro
konfiguriertem TemperaturWatch-Sensor) einen eigenen PRTG-Sensor an – das entspricht direkt der `sensorTable`
der Firmware.

1. Am Gerät: **Sensor hinzufügen** → nach `SNMP Custom Table` suchen → auswählen.
2. **Table OID** eintragen:
   ```
   1.3.6.1.4.1.99999.1.2.2.1
   ```
3. Auf **„OID auflösen" / „Test"** klicken – PRTG führt einen SNMP-Walk gegen die Tabelle aus und zeigt die
   gefundenen Spalten und Zeilen (eine Zeile je aktuell konfiguriertem Sensor) zur Auswahl an.
4. **Index-Spalte** (die Spalte, deren Wert als Sensorname/-identifikator dient) auf Spalte **2 (`sensorId`)**
   setzen – dann heißt der PRTG-Sensor später z.B. `aussentemperatur` statt nur einer laufenden Nummer.
5. **Werte-Spalten** (die als Kanäle importiert werden) auswählen:
   - Spalte **5** – Temperatur ×10 (Pflicht)
   - Spalte **6** – Luftfeuchte ×10 (nur relevant für DHT11/AM2301-Sensoren; bei reinen Dallas-Sensoren steht
     hier dauerhaft `-1`, Kanal kann für diese Zeilen ignoriert/ausgeblendet werden)
   - Optional Spalte **7** (`sensorValid`, 0/1) als Statuskanal
6. In der Zeilen-Übersicht die gewünschten Sensoren (Zeilen) per Häkchen auswählen → **Fortfahren/Hinzufügen**.

PRTG legt daraufhin für jeden ausgewählten Sensor einen eigenen Custom-Table-Sensor mit den gewählten Spalten
als Kanälen an.

## 4. Kanäle skalieren, benennen, Limits setzen

Die Firmware liefert Temperatur und Feuchte **als Ganzzahl ×10** (SNMP kennt keine Fließkommazahlen in der hier
verwendeten Kodierung) – das muss in PRTG pro Kanal korrigiert werden:

1. Im neu angelegten Sensor: **Einstellungen** → Abschnitt **„Kanäle"** → jeweiligen Kanal öffnen.
2. **Temperatur-Kanal:**
   - „Division" / Divisionsfaktor: **10**
   - Einheit: **°C** (Custom Unit)
   - Nachkommastellen: 1
   - Optional unter „Limits": oberer Warnwert z.B. `28`, oberer Fehlerwert z.B. `32` (analog zu den
     Default-Schwellwerten des CheckMK-Plugins) – Werte nach Bedarf anpassen.
3. **Feuchte-Kanal:** Division **10**, Einheit **%**. Bei reinen Dallas-Sensoren (kein Feuchtewert) diesen
   Kanal in den Sensor-Einstellungen unter „Kanal ausblenden" deaktivieren, da er dauerhaft `-1` (÷10 = `-0.1`)
   anzeigen würde.
4. **`sensorValid`-Kanal (falls importiert):** Werteanzeige „0"/„1" reicht meist; wer möchte, kann unter
   „Limits" bei `0` eine Warnung/einen Fehler auslösen lassen, um Sensor-Ausfälle (Wackelkontakt, Kabelbruch)
   direkt als PRTG-Alarm zu bekommen.
5. Sensorname umbenennen (Stift-Symbol oben) auf einen sprechenden Namen, falls die `sensorId` allein nicht
   aussagekräftig genug ist (PRTG übernimmt beim Anlegen automatisch den Wert der Index-Spalte als Namen).

## 5. Digitale IOs einbinden (optional)

Analog zu Schritt 3/4, mit der IO-Tabelle statt der Sensor-Tabelle:

1. **Sensor hinzufügen** → `SNMP Custom Table` → **Table OID:**
   ```
   1.3.6.1.4.1.99999.1.3.2.1
   ```
2. **Index-Spalte:** Spalte 2 (`ioId`)
3. **Werte-Spalte:** Spalte 5 (`ioState`, 0/1 = Aus/Ein)
4. Kanal-Einheit z.B. auf „Custom" mit Wertebezeichnung „0=Aus, 1=Ein" setzen; kein Divisionsfaktor nötig (kein
   ×10 bei IOs).
5. Optional Limits setzen, falls ein bestimmter Zustand (z.B. Alarmausgang dauerhaft `1`) einen PRTG-Alarm
   auslösen soll.

> Spalte 4 (`ioType`, Text `output`/`input`) lässt sich nicht sinnvoll als numerischer Kanal importieren – bei
> Bedarf stattdessen als zusätzliche Index-/Beschreibungs-Spalte im Discovery-Dialog anzeigen lassen, aber
> nicht als Werte-Spalte auswählen.

## 6. Neue Sensoren später hinzufügen

Wird im TemperaturWatch-Web-UI ein neuer physischer Sensor angelegt, erscheint er als **neue Zeile** in der
SNMP-Tabelle – PRTG übernimmt neue Zeilen aber **nicht automatisch** in bereits bestehende
„SNMP Custom Table"-Sensoren. Zwei Wege, das nachzuziehen:

- **Manuell (einfach):** Am Gerät erneut **Sensor hinzufügen** → `SNMP Custom Table` mit derselben Table-OID
  wie in Schritt 3 → PRTG walkt die Tabelle erneut und zeigt jetzt auch die neue Zeile zur Auswahl an → nur den
  neuen Sensor abhaken und hinzufügen (bereits vorhandene Sensoren nicht doppelt anlegen).
- **Automatisiert (für viele Geräte/häufige Änderungen):** Ein **Gerätevorlagen-basiertes Auto-Discovery** mit
  Zeitplan einrichten (Geräte-Einstellungen → „Automatische Erkennung" → Zeitplan z.B. „Täglich" + Vorlage, die
  einen `SNMP Custom Table`-Sensor auf o.g. Table-OID referenziert). Damit prüft PRTG regelmäßig selbstständig
  auf neue Tabellenzeilen. Das Einrichten einer solchen Vorlage ist PRTG-Versions-/Lizenzabhängig; für ein
  einzelnes Gerät ist der manuelle Weg meist der pragmatischere.

## Bekannte Einschränkung: Index-Verschiebung

Die Firmware nummeriert Sensoren/IOs in der SNMP-Tabelle **positionell** (1, 2, 3, … in der Reihenfolge der
aktuell gespeicherten Konfiguration) – die Zeilennummer ist **keine feste, dauerhafte ID**. Wird im Web-UI ein
Sensor **aus der Mitte der Liste gelöscht**, rücken alle nachfolgenden Sensoren eine Position auf.

PRTGs „SNMP Custom Table"-Sensor fragt bei jedem Poll die **exakte OID ab, die beim Anlegen ermittelt wurde**
(inkl. der numerischen Zeilennummer) – er sucht die Zeile **nicht** anhand des zuvor angezeigten Namens neu.
**Praktische Folge:** Wird ein Sensor mittendrin gelöscht, kann ein bestehender PRTG-Sensor ab diesem Zeitpunkt
unbemerkt die Werte eines **anderen** physischen Sensors unter dem alten Namen anzeigen, statt einfach auf
„keine Daten" zu gehen.

**Empfehlung:** Nach jedem Löschen eines Sensors/IOs im TemperaturWatch-Web-UI die zugehörigen PRTG-Sensoren
kontrollieren (Werte-Plausibilität prüfen) und im Zweifel neu anlegen. Wer das umgehen möchte: neue Sensoren im
Web-UI nur ans Ende der Liste anhängen und gelöschte Einträge nicht durch neue ersetzen, sondern deaktiviert
lassen (Firmware bietet dafür aktuell keinen „deaktiviert"-Schalter – alternativ: gelöschten Sensor-Slot mit
einem Platzhalter-Eintrag belegen, statt ihn zu entfernen). Dieselbe Einschränkung gilt im Prinzip auch für
CheckMK, wirkt sich dort aber weniger aus, weil dessen Discovery-Mechanismus bei jedem Lauf über die `id` statt
über die reine Tabellenposition matcht.

## Troubleshooting

| Symptom | Wahrscheinliche Ursache | Lösung |
|---|---|---|
| Gerät/Sensor bleibt „Down", keine Antwort | UDP/161 blockiert, SNMP-Agent nicht aktiviert, falsche IP | Firmware-Log/Web-UI → „SNMP" → „Aktiviert" prüfen; Neustart nach Aktivierung nicht vergessen; Firewall zwischen PRTG-Probe und Gerät prüfen (`Test-NetConnection <ip> -Port 161` in PowerShell auf dem Probe-Rechner, falls verfügbar) |
| „Down" mit Hinweis auf falsche Zugangsdaten | Community-String stimmt nicht überein, oder falsche SNMP-Version (v3 statt v1/v2c) gewählt | Community-String 1:1 mit Web-UI → „SNMP" abgleichen; explizit v1 **oder** v2c wählen |
| `SNMP Custom Table`-Assistent findet beim „OID auflösen" keine Zeilen | Noch keine Sensoren/IOs im Web-UI angelegt, oder falsche Table-OID (Tippfehler) | Erst mindestens einen Sensor im TemperaturWatch-Web-UI anlegen; OID exakt wie in [OID-Referenz](#oid-referenz) prüfen (Basis-OID **ohne** abschließende Spaltennummer) |
| Temperaturwerte wirken um Faktor 10 zu hoch/niedrig | Divisionsfaktor im Kanal vergessen | Siehe [Schritt 4](#4-kanäle-skalieren-benennen-limits-setzen): Division = 10 setzen |
| Feuchte-Kanal zeigt dauerhaft `-0.1` | Sensor ist ein Dallas-Sensor (kein Feuchtewert vorhanden), Firmware liefert bewusst `-1` als „nicht verfügbar" | Kanal für diese Zeile ausblenden |
| Nach Sensor-Löschung im Web-UI zeigt ein PRTG-Sensor plötzlich falsche/andere Werte | Index-Verschiebung, siehe [oben](#bekannte-einschränkung-index-verschiebung) | Betroffenen PRTG-Sensor neu anlegen |

## Alternative: REST-API-basierter Custom-Sensor

Für mehr Flexibilität (sprechende Namen ohne Spalten-Gefrickel, Zugriff auf Felder, die die SNMP-MIB nicht
abbildet – z.B. ob ein Ausgang gerade über eine Schwellwert-Regel automatisch gesteuert wird) kann alternativ
die [REST-API](REST_API.md) direkt angesprochen werden, über einen PRTG **„EXE/Skript Advanced"-Sensor**. Das
erfordert ein Skript im `Custom Sensors\EXEXML`-Ordner **des jeweiligen Probe-Rechners** (bei Remote-Probes
also nicht zentral am Core-Server) und damit etwas mehr Wartungsaufwand als die reine SNMP-Variante – dafür
liefert es beliebig viele/flexible Kanäle direkt aus `GET /api/sensors` und `GET /api/io`, ohne die
Index-Verschiebungs-Problematik von oben (die REST-API matcht immer über die stabile `id`, nicht über eine
Tabellenposition).

Minimalbeispiel (PowerShell, `temperaturwatch_sensors.ps1`, erwartet die Geräte-IP als Parameter `%host` aus
PRTG):

```powershell
param([string]$DeviceIp)

$ErrorActionPreference = "Stop"
$sensors = Invoke-RestMethod -Uri "http://$DeviceIp/api/sensors" -Method Get -TimeoutSec 10

$xml = New-Object System.Text.StringBuilder
[void]$xml.Append("<prtg>")
foreach ($s in $sensors) {
    $name = if ($s.label) { $s.label } else { $s.id }
    if ($s.has_reading -and $s.last_read_ok) {
        [void]$xml.Append("<result><channel>$([System.Security.SecurityElement]::Escape($name)) Temp.</channel><value>$([math]::Round($s.temperature_c * 100))</value><float>1</float><divisor>100</divisor><unit>Custom</unit><customunit>°C</customunit></result>")
        if ($s.PSObject.Properties.Name -contains "humidity_pct") {
            [void]$xml.Append("<result><channel>$([System.Security.SecurityElement]::Escape($name)) Feuchte</channel><value>$([math]::Round($s.humidity_pct * 100))</value><float>1</float><divisor>100</divisor><unit>Custom</unit><customunit>%</customunit></result>")
        }
    }
}
[void]$xml.Append("</prtg>")
Write-Output $xml.ToString()
```

Einrichtung: Skript nach `<PRTG-Installationsverzeichnis>\Custom Sensors\EXEXML\` auf dem Probe-Rechner
kopieren → am Gerät **Sensor hinzufügen** → `EXE/Skript Advanced` → Skript auswählen → als Parameter `%host`
eintragen (übergibt automatisch die im Gerät hinterlegte IP-Adresse). Für produktiven Einsatz empfiehlt sich
zusätzlich HTTP-Basic-Auth-Handling (`-Headers @{Authorization = "Basic ..."}`) analog zu den
[REST-API-Beispielen](REST_API.md#authentifizierung), falls der Login auf dem Gerät aktiviert ist, sowie
sauberes Error-Handling (PRTG erwartet bei Fehlern ein `<error>1</error><text>...</text>`-Ergebnis statt eines
Skriptabbruchs).

## OID-Referenz

Alle OIDs relativ zur privaten Basis-OID `1.3.6.1.4.1.99999.1` (Platzhalter-PEN, nicht bei der IANA
registriert). Ausführlicher Kontext im [README](../README.md#snmp).

| OID | Typ | Inhalt |
|---|---|---|
| `.1.1.0` | OCTET STRING | sysDescr (privat) |
| `.1.2.0` | TIMETICKS | sysUpTime (privat) |
| `.1.3.0` | OCTET STRING | sysName (privat) |
| `.1.4.0` | OCTET STRING | sysContact (privat) |
| `.1.5.0` | OCTET STRING | sysLocation (privat) |
| `.2.1.0` | INTEGER | sensorCount |
| `.2.2.1.1.<n>` | INTEGER | sensorIndex (1-basiert, **nicht persistent**, siehe [Einschränkung](#bekannte-einschränkung-index-verschiebung)) |
| `.2.2.1.2.<n>` | OCTET STRING | sensorId |
| `.2.2.1.3.<n>` | OCTET STRING | sensorLabel |
| `.2.2.1.4.<n>` | OCTET STRING | sensorType (`dallas`/`dht11`/`am2301`) |
| `.2.2.1.5.<n>` | INTEGER | sensorTemperatureX10 (°C ×10; `-32768` = keine gültige Messung) |
| `.2.2.1.6.<n>` | INTEGER | sensorHumidityX10 (% ×10; `-1` = nicht verfügbar, z.B. bei Dallas) |
| `.2.2.1.7.<n>` | INTEGER | sensorValid (`1`/`0`) |
| `.2.2.1.8.<n>` | INTEGER | sensorAgeSeconds (Sekunden seit letzter Messung) |
| `.3.1.0` | INTEGER | ioCount |
| `.3.2.1.1.<n>` | INTEGER | ioIndex (1-basiert, nicht persistent) |
| `.3.2.1.2.<n>` | OCTET STRING | ioId |
| `.3.2.1.3.<n>` | OCTET STRING | ioLabel |
| `.3.2.1.4.<n>` | OCTET STRING | ioType (`output`/`input`) |
| `.3.2.1.5.<n>` | INTEGER | ioState (`1`=Ein, `0`=Aus) |

Zusätzlich beantwortet das Gerät die Standard-MIB-II-`system`-Gruppe unter `1.3.6.1.2.1.1.{1..7}.0`
(sysDescr…sysServices) – relevant für PRTGs/anderer Tools generische Geräte-Erkennung, siehe
[Schritt 2](#2-grundprüfung-erreichbarkeit).
