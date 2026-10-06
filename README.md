# LocoNet over Thread

## Kurzbeschreibung

Dieses Projekt entwickelt Firmware, um einen LocoNet-Bus über Thread zu erweitern. Sie soll einzelne Bussegmente koppeln und dedizierte Clients ausschließlich über Thread einbinden. Ein Border Router verbindet das Thread-Netz mit WiFi und stellt später die Steuerung bereit.

## Netzwerk- und Segmenttopologie

Das System verwendet genau ein WiFi-Netz und genau ein Thread-Netz. Jeder Border Router verbindet immer diese beiden gemeinsamen Netze; mehrere Border Router erzeugen keine zusätzlichen WiFi- oder Thread-Netze.

Ein LocoNet-Segment darf dagegen immer nur an genau einem Border Router angebunden sein. Eine parallele Anbindung desselben Segments an mehrere Border Router ist nicht vorgesehen.

## Aktueller Stand

Das erste Inkrement stellt eine reproduzierbar baubare ESP-IDF-Grundlage bereit. Es enthält bewusst noch keine LocoNet-Kommunikation, Thread-Kommissionierung, RCP-Implementierung oder Steuerungs-API.

| Anwendung | Ziel | Aufgabe im Grundgerüst |
| --- | --- | --- |
| `apps/client` | ESP32-H2 (aktuelles Entwicklungsziel) | Client-Minimal-Firmware mit Diagnoseausgabe und Lebenszeichen |
| `apps/border-router` | ESP32-C6 (aktuelles Entwicklungsziel) | Minimal-Firmware für die spätere WiFi- und Steuerseite des Border Routers |

Gemeinsame, hardwareunabhängige Logik liegt unter `components/lnot_common`. Target-spezifische Treiber, Pinbelegungen und die spätere SPI-Verbindung zum H2-RCP verbleiben in den jeweiligen Anwendungen.

## Hardwareausrichtung

Die Entwicklung erfolgt aktuell mit ESP32-H2-DevKitM-1-N4 für den Client und ESP32-C6-DevKitM-1-N4 für den Border Router. Die App-Namen beschreiben bewusst nur diese Rollen, nicht den Chip oder das Board.

Weitere H2-Varianten wie H2-MINI-1, mögliche neuere Chips und abweichende Flashgrößen können erst als unterstützte Ziele gelten, nachdem ESP-IDF-Kompatibilität, Board-Konfiguration, benötigte Treiber und der Speicherbedarf geprüft wurden. Insbesondere ist noch offen, ob 4 MB Flash (N4) für die finale Firmware ausreicht.

## Voraussetzungen

- [ESP-IDF v5.3.2](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/) mit den Werkzeugen für `esp32h2` und `esp32c6`
- Python 3 für die Host-Unit-Tests
- Ein C-Compiler (`cc`) für die Host-Unit-Tests

Vor dem lokalen Build muss die ESP-IDF-Umgebung aktiviert sein, zum Beispiel unter PowerShell:

```powershell
& "$env:IDF_PATH\export.ps1"
```

## Lokaler Build

Die Anwendungen werden unabhängig gebaut. `set-target` muss mindestens beim ersten Build und nach einem Target-Wechsel ausgeführt werden.

```powershell
idf.py -C apps/client set-target esp32h2
idf.py -C apps/client build

idf.py -C apps/border-router set-target esp32c6
idf.py -C apps/border-router build
```

## Border-Router-WLAN und WebUI

Der Border Router startet zunächst einen offenen Access Point `LocoNet-<letzte 8
Hex-Zeichen der Geräte-MAC>` (zum Beispiel `LocoNet-A1B2C3D4`) im Netz
`192.168.70.0/24`. Die WebUI ist unter `http://192.168.70.1/` erreichbar.
Wenn sich der Border Router anschließend mit dem konfigurierten WLAN verbindet,
bleibt der AP aktiv; ein Aufruf der initialen Seite über `192.168.70.1` wird
zur WebUI am aktuellen WLAN-IP weitergeleitet. Beim ersten Aufruf muss zuerst ein
Administratorpasswort mit mindestens 12 Zeichen gesetzt werden. Dieses
Passwort wird mit PBKDF2-HMAC-SHA256 und individuellem Salt gespeichert.
Nach dem Speichern navigiert die WebUI automatisch auf die authentifizierte
Startseite. Dasselbe passiert nach einem normalen Login; bei einem Fehler wird
die Meldung direkt im jeweiligen Formular angezeigt. Anschließend sind WebUI
und Konfigurationsendpunkte nur nach Anmeldung zugänglich.

Die WebUI ist für Desktop und Mobilgeräte ausgelegt. Sie zeigt den
Verbindungszustand, die aktuelle WebUI-Adresse und den Status der gespeicherten
WLAN-Zugangsdaten in einer Übersicht. WLAN-Daten werden über ein klar
gekennzeichnetes Formular gespeichert; erfolgreiche und fehlgeschlagene
Anfragen erscheinen direkt in der Oberfläche.

Nach dem Speichern von WLAN-Zugangsdaten bleibt der Access Point während eines
Verbindungsversuchs von höchstens 30 Sekunden verfügbar. Bei Erfolg bleibt der
Access Point zusammen mit dem WLAN-Client aktiv; andernfalls wird der Access
Point wiederhergestellt. Zugangsdaten können in der WebUI geändert oder gelöscht
werden. Die SSID darf 1–32 Zeichen enthalten. Für ein geschütztes WLAN werden
8–63 Zeichen oder ein 64-stelliger Hex-PSK akzeptiert; ein leeres Passwort
konfiguriert ein offenes WLAN.
WLAN-Zugangsdaten werden nicht in Antworten oder Diagnosemeldungen ausgegeben.
Die Border-Router-Konfiguration verwendet NVS-Verschlüsselung mit einem
HMAC-Schlüssel in eFuse. Beim ersten Start wird dafür eFuse-Schlüsselblock 0
irreversibel belegt; vor dem Flashen ist sicherzustellen, dass dieser
Schlüsselblock nicht anderweitig verwendet wird. Ein Werksreset löscht
Konfigurationen, aber nicht diesen gerätespezifischen eFuse-Schlüssel.

Dieselben Vorgänge sind über die serielle Konsole möglich:

- `status` zeigt Betriebsmodus und ob Zugangsdaten vorhanden sind.
- `wifi set <SSID><TAB><Passwort>` speichert WLAN-Zugangsdaten und startet
  einen Verbindungsversuch. Für ein offenes WLAN bleibt das Passwort leer.
- `wifi clear` löscht die WLAN-Zugangsdaten und aktiviert den Access Point.
- `admin set <Passwort>` setzt oder ersetzt das WebUI-Administratorpasswort
  (12–128 Zeichen).
- `factory-reset` löscht die gespeicherte WLAN-Konfiguration und das
  Administratorpasswort und startet das Gerät neu. Der Befehl benötigt Zugriff
  auf die physische serielle Konsole; danach beginnt die Einrichtung erneut.

Die serielle Konsole erwartet beim Befehl `wifi set` einen Tabulator zwischen
SSID und Passwort. Zugangsdaten nicht in Terminalmitschnitten speichern. Die
WebUI nutzt HTTP und sollte nur in einem vertrauenswürdigen lokalen Netzwerk
verwendet werden; sie bietet keinen Fernzugriffsschutz für nicht vertrauenswürdige
Netze.

Die gemeinsame Komponente besitzt einen portablen Host-Unit-Test. Er prüft dieselbe C-Implementierung, die auch von ESP-IDF eingebunden wird:

```powershell
python tests/run_host_tests.py
```

Falls der Compiler nicht als `cc` im `PATH` verfügbar ist, kann er über `CC` angegeben werden, zum Beispiel `$env:CC = "clang"`.

## CI und Hardware-Deployment

Der GitHub-Actions-Workflow baut die aktuellen ESP32-H2- und ESP32-C6-Ziele
mit ESP-IDF v5.3.2 und führt die Host-Unit-Tests bei Pull Requests sowie
Änderungen an `main` aus. Für jeden Build wird ein Firmware-Artefakt 14 Tage
bereitgestellt.

Das Flashen echter Hardware erfolgt ausschließlich manuell über
`workflow_dispatch` auf `main`. Dabei wird genau ein Ziel (`client` oder
`border-router`) ausgewählt. Der zugeordnete Self-hosted Runner läuft in einem
eigenen Proxmox-LXC mit dem vorhandenen Runner-Label `ESP32-H2` oder
`ESP32-C6`. Der Job installiert `esptool` und `pyserial` in einer temporären
Python-Umgebung und verwendet genau ein per Proxmox-`dev0` durchgereichtes
Espressif-Gerät unter `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_*`.
ESP-IDF wird auf dem Hardware-Runner nicht benötigt. Nach dem Flashen wird die
passende Firmware-Ready-Meldung über die serielle Schnittstelle verifiziert.

Diese Prüfung bestätigt derzeit nur den Start der vorhandenen Minimal-Firmware.
Thread-Netzwerkaufbau und Kommunikation zwischen Border Router und Client sind
noch nicht implementiert und können daher noch nicht als
Hardware-in-the-loop-Test verifiziert werden.

Die vollständige Einrichtung der Hardware-Runner und Proxmox-LXC-Container ist
in [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) beschrieben.

## Nächste fachliche Schritte

1. LocoNet-Treiber und die Schnittstelle zu den angebundenen Komponenten definieren.
2. Thread-Client-Funktionalität auf dem ESP32-H2 ergänzen.
3. SPI-Protokoll und RCP-Firmware für die C6/H2-Kopplung festlegen.
4. Border-Router-Steuerung über WiFi implementieren.

## Rahmenbedingungen

Das Projekt dient auch dem Erlernen der Arbeit mit GitHub und GitHub Copilot. Der Code soll weitestgehend durch den GitHub Copilot Coding Agent auf Basis von Issues erstellt und anschließend per Pull Request zur Integration angeboten werden.
