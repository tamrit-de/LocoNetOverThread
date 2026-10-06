# Browserbasiertes USB-Flashen

Die statische Flash-Seite liegt unter `web-flasher/`. Sie liest veröffentlichte
GitHub Releases direkt aus `tamrit-de/LocoNetOverThread`; Firmware und
versionsgebundene Manifeste werden nicht in den Web-Container eingebaut. Ein
neues Firmware-Release erfordert deshalb kein erneutes Deployment der Seite.

## Voraussetzungen

- Desktop-Chrome oder Microsoft Edge mit Web-Serial-Unterstützung.
- Eine vertrauenswürdige HTTPS-Seite. `http://localhost` ist für lokale Tests
  ebenfalls ein sicherer Browserkontext. Die Docker-Container lauscht selbst
  nur auf HTTP und muss für öffentliche Nutzung hinter einem TLS-Reverse-Proxy
  betrieben werden.
- Ein USB-Datenkabel und ein unterstütztes Board:
  - Client: ESP32-H2-DevKitM-1-N4
  - Border Router: ESP32-C6-DevKitM-1-N4
- Der Browser muss auf die GitHub-Release-API und GitHub-Release-Dateien
  zugreifen können.

Web Serial kann den ESP-Chip identifizieren, aber nicht zuverlässig das genaue
Board. Die Seite zeigt daher nur Firmware für den erkannten Chip an und verlangt
vor dem Flashen, dass der Benutzer die auf dem Board aufgedruckte Variante
auswählt und bestätigt. Verwenden Sie die Firmware nicht mit einem anderen
Board, auch wenn dessen ESP-Chip gleich ist.

## Bereitstellung

```sh
docker build -t loconet-usb-flasher ./web-flasher
docker run --rm -p 8080:80 loconet-usb-flasher
```

Für einen lokalen Test öffnen Sie `http://localhost:8080`. Für die zentrale
Flash-Seite veröffentlichen Sie den Container hinter einem vertrauenswürdigen
HTTPS-Reverse-Proxy. Die Nginx-Konfiguration setzt CORS- und Sicherheitsheader
für die statischen Webdateien; Firmware und Manifeste werden über HTTPS von
GitHub Releases geladen.

## Flash-Vorgang

1. Öffnen Sie die Flash-Seite in Chrome oder Edge und wählen Sie einen
   Release-Kanal. **Stable** ist vorausgewählt. Beta und Alpha müssen explizit
   ausgewählt werden.
2. Verbinden Sie das Board per USB und wählen Sie **Connect and identify
   device**. Bestätigen Sie den Browserdialog für den seriellen Port.
3. Prüfen Sie den erkannten ESP-Chip. Wählen Sie die exakt passende Board-
   Variante und bestätigen Sie die Auswahl anhand der Beschriftung auf dem
   Board.
4. Prüfen Sie Version und Kanal und starten Sie das Flashen. Die Seite lädt
   alle im Manifest aufgeführten Images, verifiziert Größe und SHA-256, zeigt
   den Schreibfortschritt an und startet das Board nach erfolgreichem Flashen
   neu.

Während Download oder Flashen darf USB nicht getrennt und die Seite nicht
geschlossen werden. Bei einem Verbindungsfehler trennen Sie USB, halten beim
erneuten Anschließen die BOOT-Taste gedrückt und lassen sie nach dem Eintritt
in den Bootloader los. Versuchen Sie die Verbindung erneut. Ein unterbrochener
Flash kann nach erneutem Eintritt in den Bootloader wiederholt werden. Bei
Fehlern während des Schreibens bleibt das Gerät angeschlossen; beachten Sie
die angezeigte Fehlermeldung und starten Sie den Vorgang erneut.

## Releases und Manifeste

Veröffentlichen Sie ein GitHub Release mit einem dieser Tag-Formate:

| Tag | Kanal |
| --- | --- |
| `vMAJOR.MINOR.PATCH` (zum Beispiel `v1.2.3`) | Stable |
| `vMAJOR.MINOR.PATCH-beta.N` | Beta |
| `vMAJOR.MINOR.PATCH-alpha.N` | Alpha |

Der Workflow `Publish firmware release` baut beide unterstützten Targets,
erstellt aus `flasher_args.json` die vollständige Image-/Adressliste und
veröffentlicht die Images sowie `manifest-<tag>.json` als Assets des Release.
Jeder Eintrag enthält Chip, Board, Flash-Einstellungen, Flash-Adresse,
Download-URL, Dateigröße und SHA-256-Prüfsumme. Die Kanalzuordnung stammt aus
dem versionierten Tag. Alpha- und Beta-Releases müssen als GitHub
Pre-releases veröffentlicht werden.

Die Flash-Seite listet Releases aus der GitHub-API und verwendet das Manifest
des jeweiligen Releases. Manifeste müssen die in dieser Version der Seite
unterstützten Chip-/Board-Zuordnungen enthalten; eine zusätzliche
Board-Variante wird nicht automatisch freigeschaltet.
