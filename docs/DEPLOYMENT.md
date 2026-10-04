# Physisches Hardware-Deployment

Der GitHub-Actions-Workflow baut die Firmware auf GitHub-hosted Runnern. Das
Flashen eines echten Boards wird ausschließlich manuell über
`workflow_dispatch` auf `main` gestartet. Der Lauf wählt genau ein Ziel:

| Auswahl | Firmware | Runner-Label | Environment | Board |
| --- | --- | --- | --- | --- |
| `client` | `apps/client` | `ESP32-H2` | `hardware-client` | ESP32-H2 |
| `border-router` | `apps/border-router` | `ESP32-C6` | `hardware-border-router` | ESP32-C6 |

Der Hardware-Runner benötigt dauerhaft nur:

- den GitHub Actions Runner für Linux x64,
- Python 3 mit `venv`/`pip`,
- Zugriff des Runner-Benutzers auf das durchgereichte USB-Gerät.

`esptool`, `pyserial`, ESP-IDF, Docker, CMake, Ninja und ein C-Compiler
werden nicht dauerhaft im LXC installiert. `esptool` und `pyserial` werden
bei jedem Deployment in einer temporären Python-Umgebung installiert.

## 1. Proxmox-LXC und `dev0`

Verwenden Sie pro Board einen eigenen privilegierten Debian- oder Ubuntu-LXC:

- **Unprivileged container** deaktivieren (`unprivileged: 0`).
- Im Proxmox-Dialog **Resources > Add > Device Passthrough** genau das
  betreffende Board als `dev0` hinzufügen.
- Keine weiteren USB- oder seriellen Geräte an diesen LXC durchreichen.
- Den Runner-LXC mit dem passenden Hardwarelabel registrieren:
  `ESP32-H2` oder `ESP32-C6`.

Ermitteln Sie auf dem Proxmox-Host den stabilen Gerätenamen:

```bash
ls -l /dev/serial/by-id/
```

Für diese Boards wird ein Eintrag erwartet, der mit folgendem Muster beginnt:

```text
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_
```

Der Proxmox-`dev0`-Eintrag muss auf diesen stabilen `by-id`-Pfad zeigen, nicht
auf einen wechselnden Namen wie `/dev/ttyACM0`. Nach dem Start des LXC prüfen
Sie **im Container**:

```bash
find -L /dev/serial/by-id -maxdepth 1 -type c \
  -name 'usb-Espressif_USB_JTAG_serial_*' -print
```

Der Befehl muss genau eine Zeile ausgeben. Zusätzlich muss der Pfad ein
Zeichengerät sein:

```bash
device="$(find -L /dev/serial/by-id -maxdepth 1 -type c \
  -name 'usb-Espressif_USB_JTAG_serial_*' -print -quit)"
test -n "$device" && test -c "$device"
readlink -f "$device"
```

Ein eigener `/dev/lnot-board`-Alias und ein `lxc.mount.entry` sind für den
neuen Ablauf nicht erforderlich. Der Workflow verwendet direkt den stabilen
`/dev/serial/by-id/...`-Pfad. Wird ein Board ausgetauscht, prüfen Sie den
`by-id`-Namen erneut und starten Sie den LXC neu.

## 2. Minimale Runner-Voraussetzungen

Installieren Sie im LXC nur Python und den GitHub Actions Runner. Python muss
das Modul `venv` und pip bereitstellen:

```bash
python3 --version
python3 -m venv --help
python3 -m pip --version
```

Falls `python3 -m venv` auf der Distribution fehlt, muss das passende
Python-venv-Paket einmalig über die Distribution installiert werden. Es ist
keine ESP-IDF-Installation erforderlich.

Der Benutzer des Runner-Service muss das Gerät öffnen können. Bei einer
üblichen Debian-/Ubuntu-Konfiguration:

```bash
usermod -aG dialout github-runner
```

Ersetzen Sie `github-runner` durch den tatsächlichen Servicebenutzer.
Starten Sie den Runner-Service nach der Gruppenänderung neu und testen Sie
als dieser Benutzer:

```bash
su - github-runner
device="$(find -L /dev/serial/by-id -maxdepth 1 -type c \
  -name 'usb-Espressif_USB_JTAG_serial_*' -print -quit)"
test -n "$device" && test -r "$device" && test -w "$device"
python3 -m venv "$HOME/lnot-venv-test"
rm -rf "$HOME/lnot-venv-test"
```

Der Workflow erstellt seine virtuelle Umgebung nicht im Home-Verzeichnis,
sondern unter `$RUNNER_TEMP`. Dadurch bleiben keine Python-Pakete oder
Caches auf dem Runner zurück.

## 3. GitHub-Runner und Repository-Konfiguration

Installieren und registrieren Sie im jeweiligen LXC den aktuellen Linux-x64-
GitHub-Actions-Runner nach der offiziellen GitHub-Anleitung. Verwenden Sie
pro Container einen eigenen Runnernamen und genau das passende Label:

```bash
./config.sh \
  --url https://github.com/tamrit-de/LocoNetOverThread \
  --token <one-time-registration-token> \
  --name lnot-esp32-h2 \
  --labels ESP32-H2 \
  --unattended
sudo ./svc.sh install github-runner
sudo ./svc.sh start
```

Für den C6-Container verwenden Sie stattdessen:

```text
--name lnot-esp32-c6 --labels ESP32-C6
```

Das Standardlabel `self-hosted` bleibt erhalten. Ein Runner darf nicht beide
Hardwarelabels tragen. Begrenzen Sie die Runner-Gruppe auf vertrauenswürdige
Workflows und lassen Sie keine Pull-Request-Jobs auf diesen Runnern laufen.
Verwenden Sie ein kurzlebiges Registrierungstoken und speichern Sie es nicht
im Repository oder in Logs.

Erstellen Sie unter **Settings > Environments**:

- `hardware-client`
- `hardware-border-router`

Beschränken Sie beide Environments auf `main` und konfigurieren Sie bei Bedarf
Required Reviewers. Ein Deployment aus einem Pull Request, einem Push oder
einem anderen Branch wird vom Workflow abgelehnt.

## 4. Was die Pipeline vorbereitet

Der Deployment-Job führt nach dem Artefakt-Download folgende Schritte aus:

1. Er erstellt mit dem vorhandenen Python eine virtuelle Umgebung unter
   `$RUNNER_TEMP/lnot-flasher`.
2. Er installiert darin `esptool==4.7.0` und `pyserial==3.5`.
3. Er sucht genau ein Gerät unter
   `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_*`.
4. Er verwendet für das durchgereichte USB-Serial/JTAG-Gerät den esptool-
   Reset-Modus `usb_reset`. Das ist im LXC erforderlich, wenn die USB-
   Deskriptoren nicht bis zum Runner durchgereicht werden.
5. Er bricht bei keinem oder mehreren Geräten vor dem Flashen ab.
6. Er schreibt das ausgewählte Build-Artefakt und prüft den passenden
   Firmware-Ready-Marker.

Die Installation erfolgt ohne `sudo`, ohne apt und ohne dauerhafte Änderung
am Runner. Das Build-Artefakt enthält bereits `flasher_args.json` und alle
Images; der Hardware-Runner muss kein ESP-IDF-Projekt bauen.

## 5. Deployment ausführen

1. Den gewünschten Commit nach `main` integrieren.
2. In **Actions > Firmware CI and hardware deployment > Run workflow** den
   Branch `main` und genau ein Ziel (`client` oder `border-router`) auswählen.
3. Die Environment-Freigabe erteilen, falls Required Reviewers konfiguriert
   sind.
4. Die Prüfung der temporären Python-Umgebung und des Espressif-`by-id`-Geräts
   abwarten.
5. Nach dem Flashen muss der passende Ready-Marker im seriellen Log erscheinen:
   `client firmware is running` beziehungsweise
   `Border Router ready; AP SSID:`.

Der Workflow verwendet die Runner-Zuordnung automatisch:

| Ziel | Verwendetes Runner-Label |
| --- | --- |
| `client` | `ESP32-H2` |
| `border-router` | `ESP32-C6` |

## 6. Fehlerdiagnose

| Fehler | Prüfung |
| --- | --- |
| Python-venv kann nicht erstellt werden | `python3 -m venv --help`; gegebenenfalls das distributionsspezifische Python-venv-Paket installieren |
| `esptool` oder `pyserial` kann nicht installiert werden | Netzwerkzugriff des Runners auf PyPI und Python-Version prüfen |
| Kein Espressif-Gerät gefunden | Proxmox-`dev0`, USB-Kabel, LXC-Neustart und `/dev/serial/by-id` prüfen |
| Mehrere Espressif-Geräte gefunden | Nur das dem LXC zugewiesene Gerät durchreichen |
| Gerät ist nicht les-/schreibbar | Runnerbenutzer zur Gruppe `dialout` hinzufügen und Service neu starten |
| `Failed to get PID` oder `Write timeout` beim USB-Serial/JTAG-Gerät | Prüfen, dass der Workflow `usb_reset` verwendet, das Board mit stabiler Stromversorgung verbunden ist und kein anderer Prozess den Port verwendet; bei Bedarf das Board manuell in den Download-Modus setzen |
| Falsches Runner-Label | Runner muss genau `ESP32-H2` oder `ESP32-C6` tragen |
| `flasher_args.json` oder Image fehlt | Build-Artefakt und Upload-Pfade im Build-Job prüfen |
| Ready-Marker fehlt | Serielle Ausgabe und Board-Versorgung prüfen; danach nur einen neuen manuellen Lauf starten |

Serielle Logs können WLAN-Zugangsdaten oder andere lokale Konfiguration
enthalten. Speichern oder veröffentlichen Sie sie nicht ungeschützt.
