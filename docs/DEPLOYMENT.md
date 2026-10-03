# Physisches Hardware-Deployment

Das Repository baut die Firmware auf GitHub-hosted Runnern. Das Flashen eines
echten Boards ist davon getrennt und wird ausschließlich manuell über
`workflow_dispatch` auf `main` gestartet. Der Lauf wählt genau ein Ziel:

| Auswahl | Firmware | Runner-Label | Environment | Board |
| --- | --- | --- | --- | --- |
| `client` | `apps/client` | `lnot-client` | `hardware-client` | ESP32-H2 |
| `border-router` | `apps/border-router` | `lnot-border-router` | `hardware-border-router` | ESP32-C6 |

Für jedes Board wird ein eigener privilegierter Proxmox-LXC verwendet. Das
Board wird ausschließlich in diesen Container durchgereicht und dort unter
`/dev/lnot-board` sichtbar gemacht. Im LXC läuft ESP-IDF nativ; Docker,
Docker-in-LXC und eine automatische Installation während des Workflows sind
nicht Bestandteil dieses Ablaufs.

## 1. Proxmox-LXC pro Board

Erstellen Sie für jedes Board einen separaten Debian- oder Ubuntu-LXC:

- **Unprivileged container** deaktivieren (`unprivileged: 0`).
- Keine zusätzlichen USB- oder seriellen Geräte durchreichen.
- Einen eindeutigen USB-Pfad unter `/dev/serial/by-id/` vom Proxmox-Host
  zuordnen.
- Für den LXC einen festen Bind-Mount auf `/dev/lnot-board` konfigurieren.

Ermitteln Sie den stabilen Pfad auf dem Proxmox-Host, nachdem genau das
betreffende Board angeschlossen wurde:

```bash
ls -l /dev/serial/by-id/
readlink -f /dev/serial/by-id/<board-id>
```

Im Proxmox-Webinterface kann das Gerät unter **Resources** als USB-/Device-
Passthrough hinzugefügt werden. Alternativ ergänzen Sie die LXC-Konfiguration
auf dem Proxmox-Host in `/etc/pve/lxc/<CTID>.conf`. Der Quellpfad muss durch den
realen `by-id`-Pfad ersetzt werden:

```ini
unprivileged: 0

# USB CDC ACM; für USB-Seriell-Adapter siehe den Hinweis darunter.
lxc.cgroup2.devices.allow: c 166:* rwm
lxc.mount.entry: /dev/serial/by-id/<board-id> dev/lnot-board none bind,optional,create=file 0 0
```

Für Adapter, die auf dem Host als `/dev/ttyUSB*` erscheinen, ist zusätzlich
der Major 188 erforderlich:

```ini
lxc.cgroup2.devices.allow: c 188:* rwm
```

Starten Sie den Container nach einer Änderung neu:

```bash
pct restart <CTID>
```

Prüfen Sie im LXC, dass genau der zugewiesene Alias als Zeichengerät sichtbar
ist und auf das erwartete Gerät zeigt:

```bash
test -c /dev/lnot-board
readlink -f /dev/lnot-board
find -L /dev/serial/by-id /dev -maxdepth 1 -type c \
  \( -name 'lnot-board' -o -name 'ttyACM*' -o -name 'ttyUSB*' \) \
  -print 2>/dev/null
```

Wird ein Board ausgetauscht, muss der `by-id`-Pfad erneut geprüft und der LXC
neu gestartet werden. Der Workflow sucht nicht nach irgendeinem seriellen
Gerät: Fehlt `/dev/lnot-board` oder ist der Pfad kein Zeichengerät, wird vor
dem Flashen abgebrochen.

## 2. Native ESP-IDF-Umgebung

Installieren Sie im jeweiligen LXC die benötigten Pakete und ESP-IDF 5.3.2.
Die Version muss der Build-Version im Workflow entsprechen:

```bash
apt-get update
apt-get install --yes \
  git wget flex bison gperf cmake ninja-build ccache \
  libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  python3 python3-pip python3-venv

mkdir -p /opt/esp
git clone --branch v5.3.2 --depth 1 --recurse-submodules \
  https://github.com/espressif/esp-idf.git /opt/esp/idf
/opt/esp/idf/install.sh esp32h2,esp32c6
```

Der Runner-Service muss `IDF_PATH` kennen. Setzen Sie die Variable in der
Service-Umgebung des jeweiligen GitHub-Actions-Runners, zum Beispiel mit einem
systemd-Drop-in:

```bash
systemctl edit actions.runner.<owner>-<repo>.<runner>.service
```

```ini
[Service]
Environment=IDF_PATH=/opt/esp/idf
```

Danach den Runner-Service neu starten. Der Workflow sourced selbst
`$IDF_PATH/export.sh`; es ist nicht erforderlich, `idf.py` global in `PATH`
zu installieren:

```bash
systemctl daemon-reload
systemctl restart actions.runner.<owner>-<repo>.<runner>.service
```

Der Benutzer des Runner-Service muss das Gerät öffnen können. Bei einer
üblichen Debian-/Ubuntu-Konfiguration genügt:

```bash
usermod -aG dialout github-runner
```

Ersetzen Sie `github-runner` durch den tatsächlichen Servicebenutzer und
starten Sie den Service danach neu. Testen Sie als dieser Benutzer:

```bash
su - github-runner
test -r /dev/lnot-board && test -w /dev/lnot-board
IDF_PATH=/opt/esp/idf bash -lc 'source "$IDF_PATH/export.sh" && idf.py --version'
python3 -c 'import serial; print(serial.__version__)'
```

Der Runner benötigt weder Docker noch `sudo` für den Workflow. Installieren
Sie Pakete und ESP-IDF vor der Registrierung beziehungsweise außerhalb eines
Deployment-Laufs.

## 3. GitHub-Runner und Repository-Konfiguration

Installieren und registrieren Sie im LXC den aktuellen Linux-x64-GitHub-
Actions-Runner nach der offiziellen GitHub-Anleitung. Verwenden Sie pro
Container einen eigenen Runnernamen und das jeweilige Hardwarelabel. Ein
typischer Ablauf nach dem Entpacken des Runner-Archivs ist:

```bash
./config.sh \
  --url https://github.com/tamrit-de/LocoNetOverThread \
  --token <one-time-registration-token> \
  --name lnot-client-runner \
  --labels lnot-client \
  --unattended
sudo ./svc.sh install github-runner
sudo ./svc.sh start
```

Für den C6-Container ersetzen Sie `client` durch `border-router`. Verwenden
Sie immer ein kurzlebiges Registrierungstoken aus den Repository-Settings und
schreiben Sie es nicht in das Repository oder in Logs.

Registrieren Sie je LXC einen eigenen Self-hosted Runner im Repository und
vergeben Sie genau diese zusätzlichen Labels:

- H2-LXC: `lnot-client`
- C6-LXC: `lnot-border-router`

Das Standardlabel `self-hosted` bleibt erhalten. Ein Runner darf nicht beide
Hardwarelabels tragen. Begrenzen Sie die Runner-Gruppe auf vertrauenswürdige
Workflows des privaten Repositorys und lassen Sie keine Pull-Request-Jobs auf
diesen Runnern laufen.

Erstellen Sie unter **Settings > Environments**:

- `hardware-client`
- `hardware-border-router`

Beschränken Sie beide Environments auf `main` und konfigurieren Sie bei Bedarf
Required Reviewers. Die Environment-Namen und Runner-Labels werden im
Workflow aus der manuellen Zielauswahl abgeleitet. Ein Deployment aus einem
Pull Request, einem Push oder einem anderen Branch wird vom Job abgelehnt.

## 4. Deployment ausführen

1. Den gewünschten Commit nach `main` integrieren.
2. In **Actions > Firmware CI and hardware deployment > Run workflow**
   den Branch `main` und genau ein Ziel (`client` oder `border-router`)
   auswählen.
3. Die Environment-Freigabe erteilen, falls Required Reviewers konfiguriert
   sind.
4. Die Prüfung des nativen ESP-IDF-Runners und `/dev/lnot-board` abwarten.
5. Nach dem Flashen muss der passende Ready-Marker im seriellen Log erscheinen:
   `client firmware is running` beziehungsweise
   `Border Router ready; AP SSID:`.

Der Workflow baut beide Firmwareziele reproduzierbar und lädt anschließend nur
das ausgewählte Artefakt auf den zugeordneten Runner. Ein fehlendes, nicht
lesbares oder mehrdeutig durchgereichtes Gerät beendet den Lauf vor dem
Flash-Befehl.

## 5. Fehlerdiagnose

| Fehler | Prüfung |
| --- | --- |
| `IDF_PATH is not configured` | systemd-Drop-in prüfen, `daemon-reload` ausführen und Runner-Service neu starten |
| `export.sh` oder `idf.py` fehlt | `/opt/esp/idf` und die Installation für `esp32h2,esp32c6` prüfen |
| `/dev/lnot-board` fehlt | `by-id`-Pfad auf dem Proxmox-Host, `lxc.mount.entry` und `pct restart` prüfen |
| Gerät ist kein Zeichengerät | USB-/LXC-Passthrough und den passenden Major (166 oder 188) prüfen |
| `pyserial` fehlt | `source /opt/esp/idf/export.sh` als Runnerbenutzer testen |
| Ready-Marker fehlt | serielle Ausgabe und Board-Versorgung prüfen; danach nur einen neuen manuellen Lauf starten |

Serielle Logs können WLAN-Zugangsdaten oder andere lokale Konfiguration
enthalten. Speichern oder veröffentlichen Sie sie nicht ungeschützt.
