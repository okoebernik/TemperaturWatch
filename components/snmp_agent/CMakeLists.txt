# CONFIG_LWIP_SNMP existiert in dieser ESP-IDF-Version nicht als Kconfig-
# Symbol (siehe Kommentar in snmp_agent.c). Wir kompilieren daher die SNMP-
# Protokoll-Engine-Quellen direkt aus dem lwIP-Checkout des Frameworks mit,
# OHNE snmp_mib2*.c (ABI-Risiko, siehe snmp_agent.c) und OHNE lwipopts.h
# irgendwo zu veraendern - LWIP_SNMP=1 wird nur fuer die Uebersetzungseinheiten
# dieser Komponente per target_compile_definitions gesetzt.
set(LWIP_SNMP_SRC_DIR "${IDF_PATH}/components/lwip/lwip/src/apps/snmp")

idf_component_register(
    SRCS
        "snmp_agent.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_asn1.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_core.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_msg.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_pbuf_stream.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_raw.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_scalar.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_table.c"
        "${LWIP_SNMP_SRC_DIR}/snmp_traps.c"
    INCLUDE_DIRS "include"
    REQUIRES lwip config_store sensor_manager io_manager json esp_timer
    PRIV_REQUIRES nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LWIP_SNMP=1
    SNMP_USE_RAW=1
    SNMP_USE_NETCONN=0
    # Wir registrieren unsere eigene MIB zur Laufzeit ueber snmp_set_mibs();
    # lwIP's fertiges mib2-Modul (Paketzaehler etc.) wird bewusst NICHT
    # mitkompiliert (ABI-Risiko, siehe Kommentar oben), daher muss auch die
    # Default-Referenz auf "mib2" in snmp_core.c deaktiviert werden.
    SNMP_LWIP_MIB2=0
)

# snmp_traps.c loest mit dieser GCC-Version eine -Wdangling-pointer-Warnung
# aus (Bug/Eigenart der vendorten lwIP-Quelle, keine Auswirkung auf uns, da
# wir keine Traps versenden). Nur fuer diese Datei als Fehler deaktivieren,
# statt die Datei ganz wegzulassen (sie stellt sonst benoetigte Symbole wie
# snmp_traps_handle/snmp_authfail_trap bereit, die snmp_raw.c/snmp_msg.c
# referenzieren).
set_source_files_properties(
    "${LWIP_SNMP_SRC_DIR}/snmp_traps.c"
    PROPERTIES COMPILE_OPTIONS "-Wno-error=dangling-pointer"
)
