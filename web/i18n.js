"use strict";

// ============================================================
// Übersetzungen / Translations
// ============================================================

const I18N_STRINGS = {
  de: {
    "nav.dashboard": "Dashboard",
    "nav.sensors": "Sensoren",
    "nav.io": "IOs",
    "nav.network": "Netzwerk",
    "nav.mqtt": "MQTT",
    "nav.snmp": "SNMP",
    "nav.system": "System",

    "footer.noip": "keine IP",

    "dashboard.lead": "Live-Messwerte und Ausgangszustände.",
    "dashboard.sensors.title": "Sensoren",
    "dashboard.io.title": "Digitale IOs",
    "common.loading": "Lade…",
    "dashboard.sensors.empty.title": "Keine Sensoren konfiguriert",
    "dashboard.sensors.empty.hint": "Unter „Sensoren“ hinzufügen.",
    "dashboard.io.empty.title": "Keine IOs konfiguriert",
    "dashboard.io.empty.hint": "Unter „IOs“ hinzufügen.",
    "badge.waiting": "wartet",
    "badge.ok": "OK",
    "badge.error": "Fehler",
    "io.output": "Ausgang",
    "io.input": "Eingang",
    "io.state.on": "EIN",
    "io.state.off": "AUS",
    "io.turn.on": "Einschalten",
    "io.turn.off": "Ausschalten",
    "io.auto": "Automatisch: {rule}",
    "io.digital.input": "Digitaler Eingang",

    "sensors.lead": "Dallas-1-Wire-, DHT11- und AM2301-Sensoren verwalten.",
    "sensors.configured.title": "Konfigurierte Sensoren",
    "sensors.add": "+ Sensor",
    "sensors.empty.title": "Keine Sensoren konfiguriert",
    "sensors.empty.hint": "Füge einen Dallas-, DHT11- oder AM2301-Sensor hinzu.",
    "table.label": "Bezeichnung",
    "table.type": "Typ",
    "table.gpio": "GPIO",
    "table.romid": "ROM-ID",
    "table.interval": "Intervall",
    "table.status": "Status",
    "table.rule": "Regel",
    "table.state": "Zustand",
    "btn.edit": "Bearbeiten",
    "btn.delete": "Löschen",

    "io.lead": "Digitale Ein- und Ausgänge, optional mit Schwellwert-Regel.",
    "io.configured.title": "Konfigurierte IOs",
    "io.add": "+ IO",
    "io.empty.title": "Keine IOs konfiguriert",
    "io.empty.hint": "Füge einen digitalen Ein- oder Ausgang hinzu.",

    "network.lead": "Verbindungsstatus, WLAN-Zugangsdaten (über den ESP32-C6), Hostname und optionale feste IP-Adressen. Ethernet ist immer aktiv.",
    "network.wifi.title": "WLAN",
    "field.hostname": "Hostname (mDNS)",
    "hint.hostname": "Gerät erreichbar unter http://&lt;hostname&gt;.local/",
    "btn.save": "Speichern",

    "network.eth.static.title": "Ethernet – Statische IP",
    "network.wifi.static.title": "WLAN – Statische IP",
    "field.ip.address": "IP-Adresse",
    "field.netmask": "Subnetzmaske",
    "field.gateway": "Gateway",
    "field.dns.optional": "DNS-Server (optional)",
    "hint.static.ip": "Wenn deaktiviert, bezieht das jeweilige Interface seine Adresse per DHCP (Default).",
    "toast.static.ip.invalid": "Ungültige statische IP-Konfiguration (Ethernet oder WLAN) – bitte IP-Adresse, Subnetzmaske und Gateway prüfen.",

    "mqtt.lead": "Sensor-/IO-Werte veröffentlichen und Ausgänge per MQTT steuern.",
    "mqtt.broker.title": "MQTT-Broker",
    "field.enabled": "Aktiviert",
    "field.broker.uri": "Broker-URI",
    "field.username": "Benutzername",
    "field.base.topic": "Basis-Topic",
    "field.publish.interval": "Publish-Intervall (s)",
    "field.client.id": "Client-ID (optional)",
    "field.ca.cert": "CA-Zertifikat (für mqtts://, optional)",
    "status.disabled": "deaktiviert",
    "status.connected": "verbunden",
    "status.disconnected": "getrennt",

    "snmp.lead": "SNMPv1/v2c-Agent (UDP/161) mit eigener MIB für Sensor-/IO-Werte.",
    "snmp.agent.title": "SNMP-Agent",
    "field.community": "Community-String",
    "field.sysname": "sysName",
    "field.syscontact": "sysContact",
    "field.syslocation": "sysLocation",
    "hint.snmp.base": "Basis-OID: 1.3.6.1.4.1.99999.1 (Platzhalter-PEN). Aktivieren/Deaktivieren erfordert einen Neustart.",
    "status.active": "aktiv",
    "status.restart.needed": "Neustart nötig",

    "system.lead": "Geräteinformationen, Zugriffsschutz und Wartung.",
    "system.login.title": "Web-UI-Login",
    "field.auth.enabled": "HTTP-Basic-Auth aktiviert",
    "hint.password.set": "Ein Passwort ist gesetzt. Leer lassen, um es beizubehalten.",
    "hint.password.notset": "Noch kein Passwort gesetzt - erforderlich, um den Login zu aktivieren.",
    "btn.reboot": "Neustart",
    "btn.factory.reset": "Werkseinstellungen",
    "system.ota.title": "Firmware-Update",
    "field.ota.file": "App-Firmware (temperaturwatch.bin)",
    "field.ota.web.file": "Web-UI-Image (www.bin)",

    "info.chip": "Chip",
    "info.uptime": "Laufzeit",
    "info.free.heap": "Freier Heap",

    "sensor.dialog.add.title": "Sensor hinzufügen",
    "sensor.dialog.edit.title": "Sensor bearbeiten",
    "field.type": "Typ",
    "sensor.type.dallas": "Dallas DS18B20 (1-Wire-Bus)",
    "sensor.type.dht11": "DHT11",
    "sensor.type.am2301": "AM2301 / DHT21 / DHT22",
    "field.label": "Bezeichnung",
    "placeholder.sensor.label": "z.B. Außentemperatur",
    "field.gpio": "GPIO",
    "field.romid": "1-Wire-ROM-ID",
    "btn.scan.bus": "Bus scannen",
    "option.first.device": "Erstes gefundenes Gerät",
    "field.poll.interval": "Poll-Intervall (s)",
    "btn.cancel": "Abbrechen",
    "scan.select.gpio.first": "Bitte zuerst einen GPIO auswählen.",
    "scan.scanning": "Scanne …",
    "scan.found": "{n} Gerät(e) gefunden.",
    "scan.none.found": "Keine Geräte gefunden.",
    "scan.error": "Fehler: {msg}",

    "io.dialog.add.title": "IO hinzufügen",
    "io.dialog.edit.title": "IO bearbeiten",
    "io.type.output": "Digitaler Ausgang",
    "io.type.input": "Digitaler Eingang",
    "placeholder.io.label": "z.B. Lüfter-Relais",
    "field.invert": "Invertiert (active-low)",
    "field.initial.state": "Start-Zustand: EIN",
    "field.rule.enabled": "Schwellwert-Regel",
    "field.rule.sensor": "Sensor",
    "field.rule.field": "Messwert",
    "option.temperature": "Temperatur",
    "option.humidity": "Luftfeuchtigkeit",
    "field.rule.op": "Bedingung",
    "op.gt": "> größer als",
    "op.gte": "≥ größer/gleich",
    "op.lt": "< kleiner als",
    "op.lte": "≤ kleiner/gleich",
    "field.rule.threshold": "Schwellwert",
    "field.rule.hysteresis": "Hysterese (optional)",
    "hint.rule": "Ausgang wird automatisch geschaltet, solange die Bedingung zutrifft. Manuelles Schalten ist dann deaktiviert. Hysterese verhindert schnelles Ein-/Ausschalten nahe am Schwellwert: bei „&gt;“/„&ge;“ schaltet der Ausgang erst wieder aus, wenn der Messwert um die Hysterese unter den Schwellwert fällt (bei „&lt;“/„&le;“ umgekehrt erst aus, wenn er darüber steigt).",
    "option.no.sensors": "Keine Sensoren konfiguriert",

    "confirm.default.title": "Bist du sicher?",
    "confirm.ok": "Bestätigen",
    "confirm.reboot.title": "Neustart",
    "confirm.reboot.msg": "Das Gerät wird neu gestartet. Fortfahren?",
    "confirm.factory.title": "Werkseinstellungen",
    "confirm.factory.msg": "Alle Konfigurationen werden gelöscht und das Gerät startet neu. Fortfahren?",
    "confirm.delete.sensor.title": "Sensor löschen",
    "confirm.delete.sensor.msg": "Sensor \"{id}\" wirklich löschen?",
    "confirm.delete.io.title": "IO löschen",
    "confirm.delete.io.msg": "IO \"{id}\" wirklich löschen?",
    "confirm.ota.firmware.title": "Firmware-Update",
    "confirm.ota.web.title": "Web-UI-Update",
    "confirm.ota.msg": "\"{name}\" ({size} KB) hochladen und installieren?",

    "toast.auth.required": "Anmeldung erforderlich oder falsches Passwort",
    "toast.load.error": "Fehler beim Laden: {msg}",
    "toast.refresh.error": "Aktualisierung fehlgeschlagen: {msg}",
    "toast.toggle.error": "Schalten fehlgeschlagen: {msg}",
    "toast.select.gpio": "Bitte einen GPIO auswählen",
    "toast.sensor.saved": "Sensor gespeichert",
    "toast.sensor.deleted": "Sensor gelöscht",
    "toast.io.saved": "IO gespeichert",
    "toast.io.deleted": "IO gelöscht",
    "toast.error": "Fehler: {msg}",
    "toast.mqtt.saved": "MQTT-Einstellungen gespeichert",
    "toast.snmp.saved": "SNMP-Einstellungen gespeichert",
    "toast.auth.saved": "Login-Einstellungen gespeichert",
    "toast.reboot.running": "Neustart wird ausgeführt …",
    "toast.factory.running": "Werksreset wird ausgeführt …",
    "toast.boardpins.error": "Board-Pins konnten nicht geladen werden: {msg}",
    "toast.select.bin.file": "Bitte eine .bin-Datei auswählen",
    "error.network": "Netzwerkfehler",

    "age.no.reading": "noch keine Messung",
    "age.just.now": "gerade eben",
    "age.seconds.ago": "vor {n}s",
    "age.minutes.ago": "vor {n}min",
    "age.hours.ago": "vor {n}h",

    "uptime.hm": "{h}h {m}min",
    "uptime.m": "{m}min",

    "field.short.humidity": "Feuchte",
    "field.short.temp": "Temp.",
    "rule.hysteresis.suffix": "(Hysterese {n})",
    "pin.free": "frei",

    "statusbar.network": "Netzwerk",
    "statusbar.iface.eth": "Ethernet",
    "statusbar.iface.wifi": "WLAN",
    "statusbar.clock.synced": "Uhrzeit per NTP synchronisiert",
    "statusbar.clock.unsynced": "Uhrzeit noch nicht per NTP synchronisiert",

    "time.title": "Uhrzeit (NTP)",
    "field.ntp.server": "NTP-Server",
    "field.timezone": "Zeitzone",
    "field.timezone.custom": "Benutzerdefinierter POSIX-TZ-String",
    "hint.timezone.custom": "Format nach POSIX-TZ-Syntax, z.B. \"CET-1CEST,M3.5.0,M10.5.0/3\".",
    "option.timezone.custom": "Benutzerdefiniert …",
    "status.time.synced": "synchronisiert",
    "status.time.unsynced": "wartet auf Sync",
    "toast.time.saved": "Uhrzeit-Einstellungen gespeichert",

    "nav.help": "Hilfe",
    "help.lead": "Anleitungen zu REST-API, MQTT und PRTG – auch offline auf dem Gerät verfügbar.",
    "help.topic.restapi": "REST-API",
    "help.topic.mqtt": "MQTT",
    "help.topic.prtg": "PRTG",
    "toast.help.load.error": "Hilfeseite konnte nicht geladen werden: {msg}",

    "app.name": "TemperaturWatch",
    "theme.toggle.title": "Tag-/Nachtmodus umschalten",
    "aria.menu": "Menü",
    "aria.lang": "Sprache",
    "footer.tagline": "ESP32-P4 Temperatur-Gateway",
    "common.password_unchanged": "Passwort (leer = unverändert)",
    "network.status.title": "Status",
    "network.static.title": "Statische IP-Adresse",
    "network.save_reboot_btn": "Speichern & neu starten",
    "toast.network.saved_rebooting": "Netzwerk-Einstellungen gespeichert – Neustart …",
    "system.card_firmware": "Firmware",
    "system.build_prefix": "Build: ",
    "system.chip_foot": "{cores} Kerne · ESP-IDF {ver}",
    "system.heap_min_foot": "Minimum: {kb} KB",
    "system.ota.desc": "App-Firmware und Web-UI werden unabhängig voneinander aktualisiert. Die App-Firmware wird in den inaktiven OTA-Slot geschrieben und startet nach erfolgreichem Schreiben automatisch neu; startet die neue Firmware nicht sauber, rollt der Bootloader automatisch auf die vorherige zurück.",
    "system.upload_reboot_btn": "Hochladen & neu starten",
    "system.upload_btn": "Hochladen",
    "system.uploading": "Lade hoch …",
    "system.upload_done_reboot": "Fertig – Gerät startet neu …",
    "system.upload_done": "Fertig – Seite neu laden, um die neue Version zu sehen.",
    "system.upload_failed": "Fehlgeschlagen: {msg}",
    "system.update_success": "Update installiert",
    "system.update_failed": "Update fehlgeschlagen",
    "system.time_desc": "Synchronisiert die Geräteuhr per NTP und speist die Uhr in der Statuszeile. Wirkt sofort, kein Neustart nötig.",
    "system.access_desc": "Schützt Web-UI und REST-API mit einem Passwort (HTTP-Basic-Auth). Wirkt sofort, kein Neustart nötig.",
  },

  en: {
    "nav.dashboard": "Dashboard",
    "nav.sensors": "Sensors",
    "nav.io": "IOs",
    "nav.network": "Network",
    "nav.mqtt": "MQTT",
    "nav.snmp": "SNMP",
    "nav.system": "System",

    "footer.noip": "no IP",

    "dashboard.lead": "Live sensor readings and output states.",
    "dashboard.sensors.title": "Sensors",
    "dashboard.io.title": "Digital IOs",
    "common.loading": "Loading…",
    "dashboard.sensors.empty.title": "No sensors configured",
    "dashboard.sensors.empty.hint": "Add one under “Sensors”.",
    "dashboard.io.empty.title": "No IOs configured",
    "dashboard.io.empty.hint": "Add one under “IOs”.",
    "badge.waiting": "waiting",
    "badge.ok": "OK",
    "badge.error": "Error",
    "io.output": "Output",
    "io.input": "Input",
    "io.state.on": "ON",
    "io.state.off": "OFF",
    "io.turn.on": "Turn on",
    "io.turn.off": "Turn off",
    "io.auto": "Automatic: {rule}",
    "io.digital.input": "Digital input",

    "sensors.lead": "Manage Dallas 1-Wire, DHT11, and AM2301 sensors.",
    "sensors.configured.title": "Configured sensors",
    "sensors.add": "+ Sensor",
    "sensors.empty.title": "No sensors configured",
    "sensors.empty.hint": "Add a Dallas, DHT11, or AM2301 sensor.",
    "table.label": "Label",
    "table.type": "Type",
    "table.gpio": "GPIO",
    "table.romid": "ROM ID",
    "table.interval": "Interval",
    "table.status": "Status",
    "table.rule": "Rule",
    "table.state": "State",
    "btn.edit": "Edit",
    "btn.delete": "Delete",

    "io.lead": "Digital inputs and outputs, optionally with a threshold rule.",
    "io.configured.title": "Configured IOs",
    "io.add": "+ IO",
    "io.empty.title": "No IOs configured",
    "io.empty.hint": "Add a digital input or output.",

    "network.lead": "Connection status, WiFi credentials (via the ESP32-C6), hostname, and optional static IP addresses. Ethernet is always active.",
    "network.wifi.title": "WiFi",
    "field.hostname": "Hostname (mDNS)",
    "hint.hostname": "Device reachable at http://&lt;hostname&gt;.local/",
    "btn.save": "Save",

    "network.eth.static.title": "Ethernet – Static IP",
    "network.wifi.static.title": "WiFi – Static IP",
    "field.ip.address": "IP address",
    "field.netmask": "Subnet mask",
    "field.gateway": "Gateway",
    "field.dns.optional": "DNS server (optional)",
    "hint.static.ip": "If disabled, the respective interface obtains its address via DHCP (default).",
    "toast.static.ip.invalid": "Invalid static IP configuration (Ethernet or WiFi) – please check the IP address, subnet mask, and gateway.",

    "mqtt.lead": "Publish sensor/IO values and control outputs via MQTT.",
    "mqtt.broker.title": "MQTT broker",
    "field.enabled": "Enabled",
    "field.broker.uri": "Broker URI",
    "field.username": "Username",
    "field.base.topic": "Base topic",
    "field.publish.interval": "Publish interval (s)",
    "field.client.id": "Client ID (optional)",
    "field.ca.cert": "CA certificate (for mqtts://, optional)",
    "status.disabled": "disabled",
    "status.connected": "connected",
    "status.disconnected": "disconnected",

    "snmp.lead": "SNMPv1/v2c agent (UDP/161) with a custom MIB for sensor/IO values.",
    "snmp.agent.title": "SNMP agent",
    "field.community": "Community string",
    "field.sysname": "sysName",
    "field.syscontact": "sysContact",
    "field.syslocation": "sysLocation",
    "hint.snmp.base": "Base OID: 1.3.6.1.4.1.99999.1 (placeholder PEN). Enabling/disabling requires a restart.",
    "status.active": "active",
    "status.restart.needed": "restart needed",

    "system.lead": "Device information, access protection, and maintenance.",
    "system.login.title": "Web UI login",
    "field.auth.enabled": "HTTP Basic Auth enabled",
    "hint.password.set": "A password is set. Leave empty to keep it.",
    "hint.password.notset": "No password set yet - required to enable login.",
    "btn.reboot": "Restart",
    "btn.factory.reset": "Factory reset",
    "system.ota.title": "Firmware update",
    "field.ota.file": "App firmware (temperaturwatch.bin)",
    "field.ota.web.file": "Web UI image (www.bin)",

    "info.chip": "Chip",
    "info.uptime": "Uptime",
    "info.free.heap": "Free heap",

    "sensor.dialog.add.title": "Add sensor",
    "sensor.dialog.edit.title": "Edit sensor",
    "field.type": "Type",
    "sensor.type.dallas": "Dallas DS18B20 (1-Wire bus)",
    "sensor.type.dht11": "DHT11",
    "sensor.type.am2301": "AM2301 / DHT21 / DHT22",
    "field.label": "Label",
    "placeholder.sensor.label": "e.g. outdoor temperature",
    "field.gpio": "GPIO",
    "field.romid": "1-Wire ROM ID",
    "btn.scan.bus": "Scan bus",
    "option.first.device": "First device found",
    "field.poll.interval": "Poll interval (s)",
    "btn.cancel": "Cancel",
    "scan.select.gpio.first": "Please select a GPIO first.",
    "scan.scanning": "Scanning …",
    "scan.found": "{n} device(s) found.",
    "scan.none.found": "No devices found.",
    "scan.error": "Error: {msg}",

    "io.dialog.add.title": "Add IO",
    "io.dialog.edit.title": "Edit IO",
    "io.type.output": "Digital output",
    "io.type.input": "Digital input",
    "placeholder.io.label": "e.g. fan relay",
    "field.invert": "Inverted (active-low)",
    "field.initial.state": "Initial state: ON",
    "field.rule.enabled": "Threshold rule",
    "field.rule.sensor": "Sensor",
    "field.rule.field": "Measured value",
    "option.temperature": "Temperature",
    "option.humidity": "Humidity",
    "field.rule.op": "Condition",
    "op.gt": "&gt; greater than",
    "op.gte": "&ge; greater or equal",
    "op.lt": "&lt; less than",
    "op.lte": "&le; less or equal",
    "field.rule.threshold": "Threshold",
    "field.rule.hysteresis": "Hysteresis (optional)",
    "hint.rule": "The output is switched automatically as long as the condition is met. Manual switching is then disabled. Hysteresis prevents rapid on/off switching near the threshold: for “&gt;”/“&ge;” the output only switches off again once the value drops below the threshold by the hysteresis amount (for “&lt;”/“&le;” the reverse: it only switches off once the value rises above it).",
    "option.no.sensors": "No sensors configured",

    "confirm.default.title": "Are you sure?",
    "confirm.ok": "Confirm",
    "confirm.reboot.title": "Restart",
    "confirm.reboot.msg": "The device will restart. Continue?",
    "confirm.factory.title": "Factory reset",
    "confirm.factory.msg": "All configuration will be deleted and the device will restart. Continue?",
    "confirm.delete.sensor.title": "Delete sensor",
    "confirm.delete.sensor.msg": "Really delete sensor \"{id}\"?",
    "confirm.delete.io.title": "Delete IO",
    "confirm.delete.io.msg": "Really delete IO \"{id}\"?",
    "confirm.ota.firmware.title": "Firmware update",
    "confirm.ota.web.title": "Web UI update",
    "confirm.ota.msg": "Upload and install \"{name}\" ({size} KB)?",

    "toast.auth.required": "Login required or incorrect password",
    "toast.load.error": "Error loading: {msg}",
    "toast.refresh.error": "Update failed: {msg}",
    "toast.toggle.error": "Switching failed: {msg}",
    "toast.select.gpio": "Please select a GPIO",
    "toast.sensor.saved": "Sensor saved",
    "toast.sensor.deleted": "Sensor deleted",
    "toast.io.saved": "IO saved",
    "toast.io.deleted": "IO deleted",
    "toast.error": "Error: {msg}",
    "toast.mqtt.saved": "MQTT settings saved",
    "toast.snmp.saved": "SNMP settings saved",
    "toast.auth.saved": "Login settings saved",
    "toast.reboot.running": "Restarting …",
    "toast.factory.running": "Factory reset running …",
    "toast.boardpins.error": "Failed to load board pins: {msg}",
    "toast.select.bin.file": "Please select a .bin file",
    "error.network": "Network error",

    "age.no.reading": "no reading yet",
    "age.just.now": "just now",
    "age.seconds.ago": "{n}s ago",
    "age.minutes.ago": "{n}min ago",
    "age.hours.ago": "{n}h ago",

    "uptime.hm": "{h}h {m}min",
    "uptime.m": "{m}min",

    "field.short.humidity": "Hum.",
    "field.short.temp": "Temp.",
    "rule.hysteresis.suffix": "(hysteresis {n})",
    "pin.free": "free",

    "statusbar.network": "Network",
    "statusbar.iface.eth": "Ethernet",
    "statusbar.iface.wifi": "WiFi",
    "statusbar.clock.synced": "Time synced via NTP",
    "statusbar.clock.unsynced": "Time not yet synced via NTP",

    "time.title": "Time (NTP)",
    "field.ntp.server": "NTP server",
    "field.timezone": "Timezone",
    "field.timezone.custom": "Custom POSIX TZ string",
    "hint.timezone.custom": "POSIX TZ syntax, e.g. \"CET-1CEST,M3.5.0,M10.5.0/3\".",
    "option.timezone.custom": "Custom…",
    "status.time.synced": "synced",
    "status.time.unsynced": "waiting for sync",
    "toast.time.saved": "Time settings saved",

    "nav.help": "Help",
    "help.lead": "Guides for the REST API, MQTT, and PRTG – also available offline on the device.",
    "help.topic.restapi": "REST API",
    "help.topic.mqtt": "MQTT",
    "help.topic.prtg": "PRTG",
    "toast.help.load.error": "Failed to load help page: {msg}",

    "app.name": "TemperaturWatch",
    "theme.toggle.title": "Toggle day/night mode",
    "aria.menu": "Menu",
    "aria.lang": "Language",
    "footer.tagline": "ESP32-P4 Temperature Gateway",
    "common.password_unchanged": "Password (empty = unchanged)",
    "network.status.title": "Status",
    "network.static.title": "Static IP address",
    "network.save_reboot_btn": "Save & restart",
    "toast.network.saved_rebooting": "Network settings saved – restarting …",
    "system.card_firmware": "Firmware",
    "system.build_prefix": "Build: ",
    "system.chip_foot": "{cores} cores · ESP-IDF {ver}",
    "system.heap_min_foot": "Minimum: {kb} KB",
    "system.ota.desc": "The app firmware and web UI are updated independently. The app firmware is written to the inactive OTA slot and restarts automatically after a successful write; if the new firmware doesn't boot cleanly, the bootloader automatically rolls back to the previous one.",
    "system.upload_reboot_btn": "Upload & restart",
    "system.upload_btn": "Upload",
    "system.uploading": "Uploading …",
    "system.upload_done_reboot": "Done – device restarting …",
    "system.upload_done": "Done – reload the page to see the new version.",
    "system.upload_failed": "Failed: {msg}",
    "system.update_success": "Update installed",
    "system.update_failed": "Update failed",
    "system.time_desc": "Synchronizes the device clock via NTP and drives the clock in the status bar. Takes effect immediately, no restart needed.",
    "system.access_desc": "Protects the web UI and REST API with a password (HTTP Basic Auth). Takes effect immediately, no restart needed.",
  },
};

const I18N_STORAGE_KEY = "temperaturwatch-lang";

function i18nGetLang() {
  // ?lang=de|en in der URL hat Vorrang vor dem gespeicherten Wert - nuetzlich
  // zum Teilen/Bookmarken einer Seite in einer bestimmten Sprache und fuer
  // Dokumentations-Screenshots (identisch zu den Schwesterprojekten).
  try {
    const urlLang = new URLSearchParams(location.search).get("lang");
    if (urlLang === "en" || urlLang === "de") return urlLang;
  } catch (e) { /* ignore */ }
  try {
    const stored = localStorage.getItem(I18N_STORAGE_KEY);
    if (stored === "en" || stored === "de") return stored;
  } catch (e) { /* localStorage kann in privaten Fenstern/gesperrten Browsern werfen */ }
  return "en";
}

let i18nCurrentLang = i18nGetLang();

function i18nSetLang(lang) {
  if (lang !== "en" && lang !== "de") return;
  i18nCurrentLang = lang;
  try { localStorage.setItem(I18N_STORAGE_KEY, lang); } catch (e) { /* ignore */ }
  document.documentElement.lang = lang;
}

// Uebersetzt `key` in die aktuelle Sprache, faellt auf Englisch zurueck, wenn
// die Sprache/der Schluessel fehlt. `vars` ersetzt {name}-Platzhalter.
function t(key, vars) {
  const dict = I18N_STRINGS[i18nCurrentLang] || I18N_STRINGS.en;
  let str = dict[key] !== undefined ? dict[key] : (I18N_STRINGS.en[key] !== undefined ? I18N_STRINGS.en[key] : key);
  if (vars) {
    for (const k in vars) {
      str = str.replace(new RegExp("\{" + k + "\}", "g"), vars[k]);
    }
  }
  return str;
}

// Setzt alle statisch uebersetzbaren Elemente neu: data-i18n (textContent),
// data-i18n-html (innerHTML, nur fuer eigene Konstanten mit Entities/<code>),
// data-i18n-placeholder, data-i18n-title und data-i18n-aria-label.
function applyStaticTranslations() {
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
  document.querySelectorAll("[data-i18n-html]").forEach((el) => {
    el.innerHTML = t(el.dataset.i18nHtml);
  });
  document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
    el.placeholder = t(el.dataset.i18nPlaceholder);
  });
  document.querySelectorAll("[data-i18n-title]").forEach((el) => {
    el.title = t(el.dataset.i18nTitle);
  });
  document.querySelectorAll("[data-i18n-aria-label]").forEach((el) => {
    el.setAttribute("aria-label", t(el.dataset.i18nAriaLabel));
  });
}
