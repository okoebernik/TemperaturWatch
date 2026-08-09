# Integrating TemperaturWatch into PRTG

Guide to setting up PRTG Network Monitor for TemperaturWatch devices via the built-in
SNMPv1/v2c agent. Unlike with [CheckMK](../contrib/checkmk/), there's **no dedicated plugin** to
install here – PRTG talks to the private MIB directly through its built-in SNMP sensor types,
fully configurable via the web interface, with no script deployment on the probe server. For users who need
more flexibility (e.g. access to IO rules/hysteresis), an optional
REST API-based custom sensor is described at the end.

## Contents

- [Prerequisites](#prerequisites)
- [1. Add the Device in PRTG](#1-add-the-device-in-prtg)
- [2. Basic Check: Reachability](#2-basic-check-reachability)
- [3. Add Sensor Readings (SNMP Custom Table)](#3-add-sensor-readings-snmp-custom-table)
- [4. Scale and Name Channels, Set Limits](#4-scale-and-name-channels-set-limits)
- [5. Add Digital IOs (Optional)](#5-add-digital-ios-optional)
- [6. Add New Sensors Later](#6-add-new-sensors-later)
- [Known Limitation: Index Shifting](#known-limitation-index-shifting)
- [Troubleshooting](#troubleshooting)
- [Alternative: REST API-Based Custom Sensor](#alternative-rest-api-based-custom-sensor)
- [OID Reference](#oid-reference)

## Prerequisites

1. **Enable the SNMP agent on the device:** Web UI → "SNMP" → "Enabled" ✔, set the community string
   (default `public`), save, **restart the device** (SNMP activation only takes effect after a restart – lwIP
   can't start/stop the UDP listener live).
2. **Network:** UDP/161 must be reachable from the PRTG probe server (the core server for a local probe, or
   the remote probe machine) – check firewall rules if needed.
3. **SNMP version:** The agent supports **SNMPv1 and v2c** (no v3/no encryption). In PRTG's device
   credentials, select "SNMP v1" or "SNMP v2c" accordingly, not v3.
4. PRTG version: any current version with the standard SNMP sensor types (no additional plugin/custom sensor
   folder is needed for the recommended approach via "SNMP Custom Table").

## 1. Add the Device in PRTG

1. In the desired folder/group: **Add Device**.
2. Enter the **IP address/DNS name** of the TemperaturWatch device (e.g. `192.168.1.50` or
   `temperaturwatch.local`, if resolvable via mDNS/DNS).
3. Under **"Credentials for SNMP Devices"** in the device settings:
   - **SNMP Version:** v1 or v2c
   - **Community String:** as set in the web UI under "SNMP" (default `public`)
   - **Port:** 161 (default, generally leave unchanged)
4. **Skip the "Auto-Discovery" wizard when creating the device, or only run it with the
   standard template** – the generic discovery templates don't know our private MIB and
   would only find the standard MIB-II sensors (System, Interfaces). The actual sensor/IO values
   are added specifically in step 3.

## 2. Basic Check: Reachability

Before setting up the actual measurement sensors, it's worth running a simple connectivity test:

1. On the device: **Add Sensor** → search for `SNMP Uptime` → add.
2. After a short while the sensor should turn **green (Up)** and show the uptime since the last boot.

This works because, alongside the private MIB, the firmware also serves the **standard MIB-II `system`
group** (`1.3.6.1.2.1.1.*`) – without it, generic SNMP tools (PRTG just like CheckMK, see our earlier
CheckMK issue with `sysObjectID`) would classify the device as unreachable during discovery, even though the
private MIB is technically already responding. If this test fails, it's almost always down to the community
string, SNMP version, or firewall – see [Troubleshooting](#troubleshooting) before continuing with step 3.

## 3. Add Sensor Readings (SNMP Custom Table)

PRTG's **"SNMP Custom Table"** sensor type reads an SNMP table and creates a separate PRTG sensor for each
row found (= for each configured TemperaturWatch sensor) – this maps directly to the firmware's `sensorTable`.

1. On the device: **Add Sensor** → search for `SNMP Custom Table` → select it.
2. Enter the **Table OID**:
   ```
   1.3.6.1.4.1.99999.1.2.2.1
   ```
3. Click **"Resolve OID" / "Test"** – PRTG performs an SNMP walk against the table and displays the
   columns and rows found (one row per currently configured sensor) for selection.
4. Set the **index column** (the column whose value serves as the sensor name/identifier) to column
   **2 (`sensorId`)** – the PRTG sensor will then be named e.g. `aussentemperatur` instead of just a running
   number.
5. Select the **value columns** (to be imported as channels):
   - Column **5** – temperature ×10 (required)
   - Column **6** – humidity ×10 (only relevant for DHT11/AM2301 sensors; for pure Dallas sensors this is
     permanently `-1`, the channel can be ignored/hidden for those rows)
   - Optionally column **7** (`sensorValid`, 0/1) as a status channel
6. In the row overview, check the desired sensors (rows) → **Continue/Add**.

PRTG then creates a separate custom table sensor for each selected sensor, with the chosen columns as
channels.

## 4. Scale and Name Channels, Set Limits

The firmware delivers temperature and humidity **as an integer ×10** (SNMP has no floating-point numbers in
the encoding used here) – this needs to be corrected per channel in PRTG:

1. In the newly created sensor: **Settings** → **"Channels"** section → open the respective channel.
2. **Temperature channel:**
   - "Division" factor: **10**
   - Unit: **°C** (custom unit)
   - Decimal places: 1
   - Optionally under "Limits": upper warning value e.g. `28`, upper error value e.g. `32` (analogous to the
     default thresholds of the CheckMK plugin) – adjust values as needed.
3. **Humidity channel:** division **10**, unit **%**. For pure Dallas sensors (no humidity reading), disable
   this channel in the sensor settings under "Hide channel", since it would permanently show `-1` (÷10 =
   `-0.1`).
4. **`sensorValid` channel (if imported):** displaying "0"/"1" is usually enough; if desired, you can trigger
   a warning/error at `0` under "Limits" to get sensor failures (loose contact, broken cable) directly as a
   PRTG alarm.
5. Rename the sensor (pencil icon at the top) to a descriptive name if the `sensorId` alone isn't meaningful
   enough (PRTG automatically uses the index column's value as the name when creating the sensor).

## 5. Add Digital IOs (Optional)

Similar to steps 3/4, but with the IO table instead of the sensor table:

1. **Add Sensor** → `SNMP Custom Table` → **Table OID:**
   ```
   1.3.6.1.4.1.99999.1.3.2.1
   ```
2. **Index column:** column 2 (`ioId`)
3. **Value column:** column 5 (`ioState`, 0/1 = Off/On)
4. Set the channel unit to e.g. "Custom" with value labels "0=Off, 1=On"; no division factor needed (no
   ×10 for IOs).
5. Optionally set limits if a particular state (e.g. an alarm output permanently at `1`) should trigger a
   PRTG alarm.

> Column 4 (`ioType`, text `output`/`input`) can't meaningfully be imported as a numeric channel – if needed,
> display it as an additional index/description column in the discovery dialog instead, but don't select it
> as a value column.

## 6. Add New Sensors Later

When a new physical sensor is created in the TemperaturWatch web UI, it appears as a **new row** in the
SNMP table – however, PRTG does **not automatically** pick up new rows into already-existing
"SNMP Custom Table" sensors. There are two ways to catch up:

- **Manual (simple):** On the device, run **Add Sensor** again → `SNMP Custom Table` with the same table OID
  as in step 3 → PRTG walks the table again and now also shows the new row for selection → check only the
  new sensor and add it (don't create already-existing sensors again).
- **Automated (for many devices/frequent changes):** Set up a **device-template-based auto-discovery** with a
  schedule (device settings → "Auto-Discovery" → schedule e.g. "Daily" + a template that references an
  `SNMP Custom Table` sensor on the table OID above). This makes PRTG periodically check for new table rows
  on its own. Setting up such a template depends on the PRTG version/license; for a single device, the manual
  approach is usually more pragmatic.

## Known Limitation: Index Shifting

The firmware numbers sensors/IOs in the SNMP table **positionally** (1, 2, 3, … in the order of the
currently stored configuration) – the row number is **not a fixed, persistent ID**. If a sensor is
**deleted from the middle of the list** in the web UI, all subsequent sensors shift up one position.

PRTG's "SNMP Custom Table" sensor queries the **exact OID that was determined when it was created** on every
poll (including the numeric row number) – it does **not** re-locate the row based on the previously displayed
name. **Practical consequence:** if a sensor in the middle is deleted, an existing PRTG sensor can, from that
point on, silently display the values of a **different** physical sensor under the old name, instead of
simply going to "no data".

**Recommendation:** After deleting a sensor/IO in the TemperaturWatch web UI, check the associated PRTG
sensors (verify value plausibility) and recreate them if in doubt. To avoid this altogether: only append new
sensors to the end of the list in the web UI, and instead of replacing deleted entries with new ones, leave
them disabled (the firmware currently has no "disabled" toggle for this – alternatively: occupy the deleted
sensor slot with a placeholder entry instead of removing it). The same limitation applies in principle to
CheckMK as well, but has less impact there because its discovery mechanism matches on the `id` rather than
pure table position on every run.

## Troubleshooting

| Symptom | Likely Cause | Solution |
|---|---|---|
| Device/sensor stays "Down", no response | UDP/161 blocked, SNMP agent not enabled, wrong IP | Check firmware log/web UI → "SNMP" → "Enabled"; don't forget to restart after enabling; check the firewall between the PRTG probe and the device (`Test-NetConnection <ip> -Port 161` in PowerShell on the probe machine, if available) |
| "Down" with a note about invalid credentials | Community string doesn't match, or wrong SNMP version selected (v3 instead of v1/v2c) | Match the community string 1:1 with the web UI → "SNMP"; explicitly select v1 **or** v2c |
| `SNMP Custom Table` wizard finds no rows when resolving the OID | No sensors/IOs created yet in the web UI, or wrong table OID (typo) | First create at least one sensor in the TemperaturWatch web UI; check the OID exactly matches the [OID Reference](#oid-reference) (base OID **without** a trailing column number) |
| Temperature readings appear off by a factor of 10 (too high/low) | Division factor forgotten in the channel | See [step 4](#4-scale-and-name-channels-set-limits): set division = 10 |
| Humidity channel permanently shows `-0.1` | The sensor is a Dallas sensor (no humidity reading available), the firmware intentionally returns `-1` for "unavailable" | Hide the channel for this row |
| After deleting a sensor in the web UI, a PRTG sensor suddenly shows wrong/different values | Index shifting, see [above](#known-limitation-index-shifting) | Recreate the affected PRTG sensor |

## Alternative: REST API-Based Custom Sensor

For more flexibility (descriptive names without column fiddling, access to fields the SNMP MIB doesn't
expose – e.g. whether an output is currently being automatically controlled by a threshold rule), the
[REST API](REST_API.en.md) can alternatively be accessed directly, via a PRTG **"EXE/Script Advanced" sensor**.
This requires a script in the `Custom Sensors\EXEXML` folder **of the respective probe machine** (for remote
probes, not centrally on the core server), and thus somewhat more maintenance effort than the plain SNMP
approach – in return it delivers any number of flexible channels directly from `GET /api/sensors` and
`GET /api/io`, without the index-shifting issue described above (the REST API always matches on the stable
`id`, not on a table position).

Minimal example (PowerShell, `temperaturwatch_sensors.ps1`, expects the device IP as parameter `%host` from
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
            [void]$xml.Append("<result><channel>$([System.Security.SecurityElement]::Escape($name)) Humidity</channel><value>$([math]::Round($s.humidity_pct * 100))</value><float>1</float><divisor>100</divisor><unit>Custom</unit><customunit>%</customunit></result>")
        }
    }
}
[void]$xml.Append("</prtg>")
Write-Output $xml.ToString()
```

Setup: copy the script to `<PRTG installation directory>\Custom Sensors\EXEXML\` on the probe machine
→ on the device: **Add Sensor** → `EXE/Script Advanced` → select the script → enter `%host` as the parameter
(automatically passes the IP address stored on the device). For production use, it's also recommended to add
HTTP Basic Auth handling (`-Headers @{Authorization = "Basic ..."}`) similar to the
[REST API examples](REST_API.en.md#authentication), if login is enabled on the device, as well as clean
error handling (PRTG expects an `<error>1</error><text>...</text>` result on errors instead of a script
crash).

## OID Reference

All OIDs relative to the private base OID `1.3.6.1.4.1.99999.1` (placeholder PEN, not registered with IANA).
More detailed context in the [README](../README.en.md#snmp).

| OID | Type | Content |
|---|---|---|
| `.1.1.0` | OCTET STRING | sysDescr (private) |
| `.1.2.0` | TIMETICKS | sysUpTime (private) |
| `.1.3.0` | OCTET STRING | sysName (private) |
| `.1.4.0` | OCTET STRING | sysContact (private) |
| `.1.5.0` | OCTET STRING | sysLocation (private) |
| `.2.1.0` | INTEGER | sensorCount |
| `.2.2.1.1.<n>` | INTEGER | sensorIndex (1-based, **not persistent**, see [limitation](#known-limitation-index-shifting)) |
| `.2.2.1.2.<n>` | OCTET STRING | sensorId |
| `.2.2.1.3.<n>` | OCTET STRING | sensorLabel |
| `.2.2.1.4.<n>` | OCTET STRING | sensorType (`dallas`/`dht11`/`am2301`) |
| `.2.2.1.5.<n>` | INTEGER | sensorTemperatureX10 (°C ×10; `-32768` = no valid reading) |
| `.2.2.1.6.<n>` | INTEGER | sensorHumidityX10 (% ×10; `-1` = not available, e.g. for Dallas sensors) |
| `.2.2.1.7.<n>` | INTEGER | sensorValid (`1`/`0`) |
| `.2.2.1.8.<n>` | INTEGER | sensorAgeSeconds (seconds since the last reading) |
| `.3.1.0` | INTEGER | ioCount |
| `.3.2.1.1.<n>` | INTEGER | ioIndex (1-based, not persistent) |
| `.3.2.1.2.<n>` | OCTET STRING | ioId |
| `.3.2.1.3.<n>` | OCTET STRING | ioLabel |
| `.3.2.1.4.<n>` | OCTET STRING | ioType (`output`/`input`) |
| `.3.2.1.5.<n>` | INTEGER | ioState (`1`=On, `0`=Off) |

The device additionally answers the standard MIB-II `system` group under `1.3.6.1.2.1.1.{1..7}.0`
(sysDescr…sysServices) – relevant for PRTG's/other tools' generic device discovery, see
[step 2](#2-basic-check-reachability).
