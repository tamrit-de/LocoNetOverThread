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

Die gemeinsame Komponente besitzt einen portablen Host-Unit-Test. Er prüft dieselbe C-Implementierung, die auch von ESP-IDF eingebunden wird:

```powershell
python tests/run_host_tests.py
```

Falls der Compiler nicht als `cc` im `PATH` verfügbar ist, kann er über `CC` angegeben werden, zum Beispiel `$env:CC = "clang"`.

## CI

Der GitHub-Actions-Workflow baut die aktuellen ESP32-H2- und ESP32-C6-Ziele mit ESP-IDF v5.3.2 und führt den Host-Unit-Test bei Pull Requests sowie Änderungen an `main` aus. Weitere verifizierte Hardwarevarianten werden als zusätzliche Matrix-Einträge ergänzt. Hardware-in-the-loop wird ergänzt, sobald die Testhardware und deren Runner-Anbindung festgelegt sind.

## Nächste fachliche Schritte

1. LocoNet-Treiber und die Schnittstelle zu den angebundenen Komponenten definieren.
2. Thread-Client-Funktionalität auf dem ESP32-H2 ergänzen.
3. SPI-Protokoll und RCP-Firmware für die C6/H2-Kopplung festlegen.
4. Border-Router-Steuerung über WiFi implementieren.

## Rahmenbedingungen

Das Projekt dient auch dem Erlernen der Arbeit mit GitHub und GitHub Copilot. Der Code soll weitestgehend durch den GitHub Copilot Coding Agent auf Basis von Issues erstellt und anschließend per Pull Request zur Integration angeboten werden.
