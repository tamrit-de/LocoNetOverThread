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
