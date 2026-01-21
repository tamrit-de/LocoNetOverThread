# LocoNetOverThread

## Kurzbeschreibung
Entstehen soll eine Firmware, um einen LocoNet Bus über Thread zu erweitern. Dabei sollen sowohl einzelne Bussegmente gekoppelt werden können, als auch dezidiert Clients ausschließlich über Thread eingebunden werden. Dieses Thread Netzwerk kann über Border Router gleichzeit mit WiFi verbunden und darüber gesteuert werden.

## Rahmenbedingungen
Das Projekt dient zusätzlich dem Erlernen der Arbeit mit Github und Github Copliot. Der Code soll weitestgehend durch Github Copilot coding agent auf Basis von Issues erstellt und anschließend per Pull Request zur Integration angeboten werden.

## Hardware Basis
es wird zunächst zwei Hardwarekonfigurationen geben:
1. Client: ESP32-H2, sowohl dauerhaft mit Strom versorgt, als auch Batterie betrieben, für die Anbindung einzelner Komponenten
2. Border Router: ESP32-C6 für WiFi und BLE, zusammen mit einem ESP32-H2 als RCP für Thread (per SPI verbunden)

Beide Varianten stehen zu Testzwecken mehrfach zur Verfügung, eine entsprechende Pipeline wird im Rahmen des Projektes entstehen. 

## Software
Als Basis soll hier das ESP-IDF dienen. Alle Softwarefeatures sollen aber in einer Codebasis entwickelt werden und per sdkconfig zusammengestellt werden.

## Offene Fragestellungen

### Technische Architektur
- **LocoNet Protokoll**: Welche Version des LocoNet Protokolls soll unterstützt werden? Gibt es spezifische Message-Typen, die priorisiert werden müssen?
- **Thread Netzwerk Topologie**: Wie soll die Netzwerktopologie aussehen? Mesh oder Star? Wie viele Geräte müssen maximal unterstützt werden?
- **Latenzanforderungen**: Welche maximale Latenz ist für die LocoNet-Thread Kommunikation akzeptabel? Gibt es Echtzeitanforderungen?
- **Fehlerbehandlung**: Wie soll mit Netzwerkausfällen umgegangen werden? Soll es einen Fallback-Mechanismus geben?

### Hardware & Energie
- **Stromverbrauch**: Welche Anforderungen gibt es an den Stromverbrauch für batteriebetriebene Clients? Welche Sleep-Modi werden benötigt?
- **LocoNet Interface**: Welche Hardware wird für die physikalische Anbindung an den LocoNet Bus benötigt? (z.B. LN_UR92, eigene Schaltung?)
- **Erweiterbarkeit**: Sollen weitere Hardware-Plattformen unterstützt werden? (z.B. andere ESP32 Varianten)

### Software Features
- **Konfiguration**: Wie sollen die Geräte konfiguriert werden? OTA? Web-Interface? MQTT?
- **Monitoring & Debugging**: Welche Logging- und Monitoring-Funktionen werden benötigt?
- **OTA Updates**: Wie soll die Firmware-Aktualisierung erfolgen? Über WiFi oder Thread?
- **Sicherheit**: Welche Sicherheitsanforderungen gibt es? Verschlüsselung, Authentifizierung?

### Entwicklungsprozess
- **CI/CD Pipeline**: Welche Schritte sollen in der Build-Pipeline automatisiert werden?
- **Testing**: Wie soll getestet werden? Unit-Tests, Integration-Tests, Hardware-in-the-Loop?
- **Dokumentation**: Welche Dokumentation wird benötigt? API-Docs, Benutzerhandbuch, Schaltpläne?

### Use Cases & Szenarien
- **Primäre Anwendungsfälle**: Welche konkreten Anwendungsfälle sollen zuerst implementiert werden?
- **Kompatibilität**: Mit welchen bestehenden LocoNet-Komponenten muss das System kompatibel sein?

## TODO Liste - Nächste Schritte

### Phase 1: Projektsetup & Grundlagen (Priorität: Hoch)
- [ ] ESP-IDF Entwicklungsumgebung einrichten
  - [ ] ESP-IDF Installation und Konfiguration
  - [ ] Toolchain für ESP32-H2 und ESP32-C6 einrichten
  - [ ] VSCode/IDE Setup mit ESP-IDF Extension
- [ ] Repository-Struktur aufsetzen
  - [ ] Grundlegende Verzeichnisstruktur erstellen (src, include, components, etc.)
  - [ ] CMakeLists.txt für ESP-IDF Projekt erstellen
  - [ ] sdkconfig Templates für beide Hardware-Varianten erstellen
- [ ] CI/CD Pipeline initialisieren
  - [ ] GitHub Actions Workflow für Build-Prozess erstellen
  - [ ] Automatische Kompilierung für beide Hardware-Varianten
  - [ ] Code-Quality Checks (Linting, Formatierung)

### Phase 2: Grundlegende Thread-Kommunikation (Priorität: Hoch)
- [ ] Thread Netzwerk Basis implementieren
  - [ ] OpenThread Integration in ESP-IDF
  - [ ] Thread Network Commissioning
  - [ ] Einfache Thread-Kommunikation zwischen zwei ESP32-H2 Geräten testen
- [ ] Border Router Setup
  - [ ] ESP32-C6 + ESP32-H2 RCP Konfiguration
  - [ ] SPI Kommunikation zwischen C6 und H2 einrichten
  - [ ] WiFi <-> Thread Bridge Funktionalität

### Phase 3: LocoNet Integration (Priorität: Hoch)
- [ ] LocoNet Hardware Interface
  - [ ] Schaltplan für LocoNet Anbindung erstellen
  - [ ] UART-Konfiguration für LocoNet Protokoll
  - [ ] Hardware-Prototyp aufbauen und testen
- [ ] LocoNet Protokoll Implementation
  - [ ] LocoNet Message Parser/Encoder implementieren
  - [ ] Grundlegende Message-Typen unterstützen
  - [ ] LocoNet Bus Monitor Funktionalität

### Phase 4: LocoNet-Thread Bridge (Priorität: Mittel)
- [ ] Bridge Logik entwickeln
  - [ ] LocoNet Messages zu Thread-Paketen konvertieren
  - [ ] Thread-Pakete zu LocoNet Messages konvertieren
  - [ ] Routing-Logik zwischen Bussegmenten
- [ ] Protokoll-Optimierung
  - [ ] Latenz-Optimierung
  - [ ] Buffer-Management
  - [ ] Flow-Control implementieren

### Phase 5: Client-Implementierung (Priorität: Mittel)
- [ ] Batteriebetriebene Clients
  - [ ] Power-Management implementieren
  - [ ] Sleep-Modi für ESP32-H2
  - [ ] Wake-up Mechanismen
- [ ] Client-Funktionalität
  - [ ] Beispiel-Client für Weichen-Steuerung
  - [ ] Beispiel-Client für Sensor-Anbindung
  - [ ] Client-Konfiguration über Thread

### Phase 6: Management & Konfiguration (Priorität: Niedrig)
- [ ] Web-Interface
  - [ ] WiFi Webserver auf Border Router
  - [ ] Konfigurations-Seiten
  - [ ] Status-Monitoring Dashboard
- [ ] OTA Updates
  - [ ] OTA Update Mechanismus implementieren
  - [ ] Rollback-Funktionalität
  - [ ] Update über WiFi und Thread

### Phase 7: Testing & Dokumentation (Priorität: Laufend)
- [ ] Testing
  - [ ] Unit-Tests für kritische Komponenten
  - [ ] Integration-Tests aufsetzen
  - [ ] Hardware-Test-Setup dokumentieren
- [ ] Dokumentation
  - [ ] API-Dokumentation erstellen
  - [ ] Benutzer-Handbuch schreiben
  - [ ] Hardware-Aufbau dokumentieren
  - [ ] Beispiele und Tutorials erstellen

### Phase 8: Optimierung & Erweiterung (Priorität: Niedrig)
- [ ] Performance-Optimierung
  - [ ] Latenz-Messungen und Optimierung
  - [ ] Stromverbrauch optimieren
  - [ ] Netzwerk-Stabilität verbessern
- [ ] Erweiterte Features
  - [ ] MQTT Integration für Home Automation
  - [ ] Logging und Diagnostics
  - [ ] Security Features (Verschlüsselung, Authentifizierung)