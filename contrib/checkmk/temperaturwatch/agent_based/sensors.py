#!/usr/bin/env python3
"""CheckMK-Plugin fuer TemperaturWatch (Waveshare ESP32-P4-WIFI6-POE-ETH Sensor-Gateway).

Liest die private SNMP-MIB unter 1.3.6.1.4.1.99999.1 aus (siehe README des
Firmware-Projekts, Abschnitt "SNMP") und legt pro konfiguriertem Sensor
automatisch einen Service an - inkl. Temperatur, optional Luftfeuchte, und
erkennt neue/entfernte Sensoren bei erneuter Service-Discovery von selbst.

Getestet mit CheckMK Community 2.5.0p7 (agent_based-Plugin-API v2).

Installation:
    Diese Datei auf den CheckMK-Server kopieren nach:
        ~/local/lib/python3/cmk_addons/plugins/temperaturwatch/agent_based/sensors.py
    (der Ordnername "temperaturwatch" davor ist frei waehlbar, dient nur der
    Gruppierung; "agent_based" muss aber exakt so heissen.)

    Danach in der Oberflaeche: Setup -> Hosts -> <Host> -> Service discovery
    ausfuehren, neue Services uebernehmen, Aenderungen aktivieren. Ein
    manueller Neustart/Reload des Servers ist normalerweise NICHT noetig -
    CheckMK laedt lokale Plugins automatisch bei der naechsten Discovery/
    Aktivierung. Falls doch nicht: `cmk -R` bzw. "Aenderungen aktivieren".

Schwellwerte (Standard: Warnung ab 28 °C, kritisch ab 32 °C) sind unten in
`check_default_parameters` fest hinterlegt - hier direkt anpassen, falls
gewuenscht (keine WATO-Regel mitgeliefert, siehe Hinweis im Chat).
"""

from collections.abc import Mapping
from typing import Any

from cmk.agent_based.v2 import (
    CheckPlugin,
    CheckResult,
    DiscoveryResult,
    Metric,
    Result,
    Service,
    SimpleSNMPSection,
    SNMPTree,
    State,
    StringTable,
    exists,
)

# Basis-OID der Sensortabelle (sensorEntry). Spalten (siehe Firmware-README):
# 2=id 3=label 4=type 5=temperature*10 6=humidity*10 (-1=n/a) 7=valid 8=age(s)
_SENSOR_TABLE_BASE = ".1.3.6.1.4.1.99999.1.2.2.1"

check_default_parameters: Mapping[str, Any] = {
    "temp_warn": 28.0,
    "temp_crit": 32.0,
}


def parse_temperaturwatch_sensors(string_table: StringTable) -> Mapping[str, Mapping[str, Any]]:
    parsed: dict[str, Mapping[str, Any]] = {}
    for sensor_id, label, sensor_type, temp_x10, hum_x10, valid, age in string_table:
        name = label.strip() or sensor_id
        hum_raw = int(hum_x10)
        parsed[name] = {
            "id": sensor_id,
            "type": sensor_type,
            "temperature": int(temp_x10) / 10.0,
            "humidity": (hum_raw / 10.0) if hum_raw >= 0 else None,
            "valid": valid == "1",
            "age": int(age),
        }
    return parsed


snmp_section_temperaturwatch_sensors = SimpleSNMPSection(
    name="temperaturwatch_sensors",
    # Fingerabdruck: fragt gezielt unsere eigene sensorCount-OID ab, statt
    # z.B. auf sysObjectID zu pruefen (robuster gegen Namenskollisionen).
    detect=exists(".1.3.6.1.4.1.99999.1.2.1.0"),
    fetch=SNMPTree(
        base=_SENSOR_TABLE_BASE,
        oids=["2", "3", "4", "5", "6", "7", "8"],
    ),
    parse_function=parse_temperaturwatch_sensors,
)


def discover_temperaturwatch_sensors(section: Mapping[str, Mapping[str, Any]]) -> DiscoveryResult:
    for name in section:
        yield Service(item=name)


def check_temperaturwatch_sensors(
    item: str, params: Mapping[str, Any], section: Mapping[str, Mapping[str, Any]]
) -> CheckResult:
    data = section.get(item)
    if data is None:
        # Sensor aktuell nicht (mehr) in der SNMP-Tabelle - CheckMK zeigt den
        # Service als UNKNOWN; bei dauerhaftem Entfernen im Web-UI hilft eine
        # erneute Service-Discovery, um ihn als "vanished" zu markieren.
        return

    if not data["valid"]:
        yield Result(state=State.WARN, summary="Kein gueltiger Messwert (noch keine Messung oder Lesefehler)")
        return

    temp = data["temperature"]
    warn = params.get("temp_warn")
    crit = params.get("temp_crit")
    state = State.OK
    if crit is not None and temp >= crit:
        state = State.CRIT
    elif warn is not None and temp >= warn:
        state = State.WARN
    yield Result(state=state, summary=f"Temperatur: {temp:.1f} °C")
    yield Metric("temp", temp, levels=(warn, crit) if warn is not None else None)

    if data["humidity"] is not None:
        yield Result(state=State.OK, summary=f"Luftfeuchte: {data['humidity']:.1f} %")
        yield Metric("humidity", data["humidity"])

    yield Result(
        state=State.OK,
        notice=f"Sensor-ID {data['id']} ({data['type']}), letzte Messung vor {data['age']}s",
    )


check_plugin_temperaturwatch_sensors = CheckPlugin(
    name="temperaturwatch_sensors",
    service_name="Sensor %s",
    discovery_function=discover_temperaturwatch_sensors,
    check_function=check_temperaturwatch_sensors,
    check_default_parameters=check_default_parameters,
)
