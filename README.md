# LocoNet over Thread

## Kurzbeschreibung

Dieses Projekt entwickelt Firmware, um einen LocoNet-Bus über Thread zu erweitern. Sie soll einzelne Bussegmente koppeln und dedizierte Clients ausschließlich über Thread einbinden. Ein Border Router verbindet das Thread-Netz mit WiFi und stellt später die Steuerung bereit.

## Aktueller Stand

Das erste Inkrement stellt eine reproduzierbar baubare ESP-IDF-Grundlage bereit. Es enthält bewusst noch keine LocoNet-Kommunikation, Thread-Kommissionierung, RCP-Implementierung oder Steuerungs-API.

| Anwendung | Ziel | Aufgabe im Grundgerüst |
| --- | --- | --- |
| `apps/client-h2` | ESP32-H2 | Client-Minimal-Firmware mit Diagnoseausgabe und Lebenszeichen |
| `apps/border-router-c6` | ESP32-C6 | Minimal-Firmware für die spätere WiFi- und Steuerseite des Border Routers |

Gemeinsame, hardwareunabhängige Logik liegt unter `components/lnot_common`. Target-spezifische Treiber, Pinbelegungen und die spätere SPI-Verbindung zum H2-RCP verbleiben in den jeweiligen Anwendungen.

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
idf.py -C apps/client-h2 set-target esp32h2
idf.py -C apps/client-h2 build

idf.py -C apps/border-router-c6 set-target esp32c6
idf.py -C apps/border-router-c6 build
```

Die gemeinsame Komponente besitzt einen portablen Host-Unit-Test. Er prüft dieselbe C-Implementierung, die auch von ESP-IDF eingebunden wird:

```powershell
python tests/run_host_tests.py
```

Falls der Compiler nicht als `cc` im `PATH` verfügbar ist, kann er über `CC` angegeben werden, zum Beispiel `$env:CC = "clang"`.

## CI

Der GitHub-Actions-Workflow baut beide Firmware-Ziele mit ESP-IDF v5.3.2 und führt den Host-Unit-Test bei Pull Requests sowie Änderungen an `main` aus. Hardware-in-the-loop wird ergänzt, sobald die Testhardware und deren Runner-Anbindung festgelegt sind.

## Nächste fachliche Schritte

1. LocoNet-Treiber und die Schnittstelle zu den angebundenen Komponenten definieren.
2. Thread-Client-Funktionalität auf dem ESP32-H2 ergänzen.
3. SPI-Protokoll und RCP-Firmware für die C6/H2-Kopplung festlegen.
4. Border-Router-Steuerung über WiFi implementieren.

## Rahmenbedingungen

Das Projekt dient auch dem Erlernen der Arbeit mit GitHub und GitHub Copilot. Der Code soll weitestgehend durch den GitHub Copilot Coding Agent auf Basis von Issues erstellt und anschließend per Pull Request zur Integration angeboten werden.
