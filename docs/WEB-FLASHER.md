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
- Der Browser muss auf die GitHub-Release-API zugreifen können. Der
  Web-Container reicht die Asset-Endpunkte und deren Download-Weiterleitungen
  same-origin durch, damit CORS-Header externer Download-Hosts nicht benötigt
  werden.

Web Serial kann den ESP-Chip identifizieren, aber nicht zuverlässig das genaue
Board. Die Seite berücksichtigt dabei nur eine optionale, vom Bootloader
gemeldete Chip-Revision, etwa `ESP32-H2 (revision v0.1)`, und vergleicht das
Chipmodell ansonsten exakt. Sie zeigt daher nur Firmware für den erkannten Chip
an und verlangt vor dem Flashen, dass der Benutzer die auf dem Board
aufgedruckte Variante auswählt und bestätigt. Verwenden Sie die Firmware nicht
mit einem anderen Board, auch wenn dessen ESP-Chip gleich ist.

## Bereitstellung

### Portainer

1. Erstellen Sie in Portainer einen neuen **Stack** und wählen Sie
   **Repository** als Build-Methode.
2. Verwenden Sie als Repository-URL
   `https://github.com/tamrit-de/LocoNetOverThread.git`, wählen Sie den
   gewünschten Branch und geben Sie als Compose-Pfad `compose.yml` an.
3. Setzen Sie optional die Umgebungsvariable `WEB_FLASHER_PORT` auf einen
   freien Host-Port; ohne Variable wird Port `8080` verwendet. Stellen Sie den
   Stack bereit. Portainer baut dabei mit `web-flasher/` als Kontext und
   installiert die festgelegte Abhängigkeit `esptool-js@0.7.0`.
4. Richten Sie im TLS-Reverse-Proxy einen Host für die Flash-Domain ein, der
   auf `http://<Docker-Host>:<WEB_FLASHER_PORT>` weiterleitet. Erzwingen Sie
   HTTPS und verwenden Sie ein gültiges Zertifikat. Web Serial funktioniert
   außerhalb von `localhost` nicht über HTTP.
5. Prüfen Sie nach dem Start im Browser
   `https://<Flash-Domain>/vendor/esptool.js`. Der Abruf muss JavaScript statt
   einer 404-Seite liefern. Diese Datei wird ausschließlich beim Docker-Build
   aus `node_modules/esptool-js/bundle.js` erzeugt und liegt daher nicht im
   Git-Arbeitsbaum.

Veröffentlichen Sie nicht direkt den Ordner `web-flasher/` als statische
Website: Dort fehlt die beim Docker-Build erzeugte Datei
`vendor/esptool.js`, sodass der Modulimport in `app.js` fehlschlägt.

### Docker CLI

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

1. Öffnen Sie die Flash-Seite in Chrome oder Edge. Die Seite zeigt nur
   Release-Kanäle mit veröffentlichter Firmware an und wählt bevorzugt
   **Stable** aus; ist kein Stable-Release verfügbar, wird der nächste
   verfügbare Kanal verwendet. Wählen Sie anschließend die gewünschte Version,
   wobei die neueste Version des Kanals vorausgewählt ist.
2. Verbinden Sie das Board per USB und wählen Sie **Connect and identify
   device**. Bestätigen Sie den Browserdialog für den seriellen Port.
3. Prüfen Sie den erkannten ESP-Chip. Wählen Sie die exakt passende Board-
   Variante und bestätigen Sie die Auswahl anhand der Beschriftung auf dem
   Board.
4. Prüfen Sie Version und Kanal und starten Sie das Flashen. Die Seite lädt
   alle im Manifest aufgeführten Images, verifiziert Größe und SHA-256, zeigt
   den Schreibfortschritt an und startet das Board nach erfolgreichem Flashen
   neu. Anschließend öffnet sie den Port mit 115200 Baud erneut und zeigt
   15 Sekunden lang die Boot-Ausgabe an.

Während Download oder Flashen darf USB nicht getrennt und die Seite nicht
geschlossen werden. Bei einem Verbindungsfehler trennen Sie USB, halten beim
erneuten Anschließen die BOOT-Taste gedrückt und lassen sie nach dem Eintritt
in den Bootloader los. Versuchen Sie die Verbindung erneut. Ein unterbrochener
Flash kann nach erneutem Eintritt in den Bootloader wiederholt werden. Bei
Fehlern während des Schreibens bleibt das Gerät angeschlossen; beachten Sie
die angezeigte Fehlermeldung und starten Sie den Vorgang erneut.

Die Boot-Ausgabe wird nur im Browser angezeigt und nicht gespeichert oder
hochgeladen. Fehlt sie vollständig, obwohl das Flashen erfolgreich war, hat das
Gerät nach dem Reset möglicherweise nicht gestartet; prüfen Sie Kabel,
Stromversorgung und die angezeigte Ausgabe auf Reset- oder Bootfehler.

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
Pre-releases veröffentlicht werden. Jeder erfolgreiche Push nach `main`
veröffentlicht zusätzlich automatisch ein Alpha-Pre-Release mit dem Tag
`v0.0.0-alpha.<GitHub-Run-Nummer>`. Wählen Sie im Flasher den Kanal
**Alpha**, um diese automatisch erstellte Firmware zu sehen. Stable-Releases
werden weiterhin nur durch ein manuell veröffentlichtes GitHub Release
erstellt. Die automatische Veröffentlichung verwendet die im Workflow
`Firmware CI and hardware deployment` erzeugten Build-Artefakte; sie baut die
Firmware nicht ein zweites Mal.

Die Flash-Seite lädt alle Seiten der GitHub-Release-API und verwendet das
Manifest des jeweiligen Releases. Manifeste müssen die in dieser Version der
Seite unterstützten Chip-/Board-Zuordnungen enthalten; eine zusätzliche
Board-Variante wird nicht automatisch freigeschaltet.

Für jedes Manifest und Image verwendet die Flash-Seite den passenden
API-Asset-Endpunkt aus den Release-Metadaten statt der öffentlichen
`github.com/.../releases/download/...`-URL. Der Web-Container leitet den
API-Redirect anschließend auf einen same-origin Download-Proxy um. Dadurch
bleibt die Auslieferung nutzbar, wenn GitHub bei öffentlichen oder finalen
Download-URLs keine CORS-Header liefert.

Der WebUI-Reverse-Proxy muss dabei das ursprüngliche HTTPS-Schema beibehalten.
Die Nginx-Konfiguration gibt den Asset-Redirect deshalb relativ zurück; das
verhindert einen HTTPS-zu-HTTP-Origin-Wechsel, wenn TLS vor dem Container
terminiert wird.
