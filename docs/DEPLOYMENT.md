# Hardware-Deployment

Dieser Ablauf trennt Build und Auslieferung: GitHub Actions baut beide
Firmwares und führt die Host-Tests auf GitHub-gehosteten Runnern aus. Erst wenn
beides erfolgreich ist, wird die bereits gebaute Firmware auf einem
zugeordneten Hardware-Runner geflasht. Ein Deployment wird bei einem Push auf
`main` oder durch einen manuellen Workflow-Lauf auf `main` ausgelöst.

| Anwendung | GitHub-Runner-Label | GitHub-Environment | Firmwareziel |
| --- | --- | --- | --- |
| `apps/client` | `ESP32-H2` | `hardware-client` | ESP32-H2 |
| `apps/border-router` | `ESP32-C6` | `hardware-border-router` | ESP32-C6 |

Jeder Runner steuert genau ein Board. Dadurch kann kein paralleler Workflow
dasselbe serielle Gerät öffnen; zusätzlich serialisiert der Workflow
Deployments je Anwendung.

## GitHub-Konfiguration

1. Erstellen Sie zwei Self-hosted Runner, jeweils in einem eigenen LXC, und
   vergeben Sie zusätzlich zum Standardlabel `self-hosted` genau das Label aus
   der Tabelle.
2. Erstellen Sie in **Settings > Environments** die Environments
   `hardware-client` und `hardware-border-router`. Beschränken Sie sie auf den
   Branch `main` und hinterlegen Sie die für Hardware-Deployments zuständigen
   Required Reviewers. Ohne konfigurierte Schutzregel wird der automatisch
   angelegte Environment-Name nicht abgesichert.
3. Halten Sie die Runner nicht in Gruppen vor, die Pull-Request-Workflows
   ausführen dürfen. Das Deployment-Job ist auf `main` beschränkt; die
   Branch- und Environment-Regeln schützen zusätzlich vor versehentlichem
   Flashen.

## Proxmox-LXC vorbereiten

Verwenden Sie pro Board einen eigenen, privilegierten Debian- oder
Ubuntu-LXC. Ein privilegierter Container ist hier erforderlich, weil Docker
den exklusiv durchgereichten USB-Adapter an den ESP-IDF-Container weitergibt.
Der Container darf keine anderen nicht benötigten Host-Geräte erhalten.

Erstellen Sie den Container im Proxmox-Webinterface mit **Unprivileged
container** deaktiviert. Ergänzen Sie anschließend auf dem Proxmox-Host in
`/etc/pve/lxc/<CTID>.conf` die folgenden Einstellungen. Ersetzen Sie den
Pfad nach `/dev/serial/by-id/` durch den eindeutigen Pfad des angeschlossenen
Boards aus `ls -l /dev/serial/by-id/`.

```ini
unprivileged: 0
features: nesting=1,keyctl=1

# Für /dev/ttyACM*: USB CDC ACM (Major 166).
lxc.cgroup2.devices.allow: c 166:* rwm
lxc.mount.entry: /dev/serial/by-id/<eindeutiger-board-pfad> dev/lnot-board none bind,optional,create=file 0 0
```

Für Adapter, die auf dem Host als `/dev/ttyUSB*` erscheinen, verwenden Sie
stattdessen `lxc.cgroup2.devices.allow: c 188:* rwm`. Der Bind-Mount gibt das
Gerät im Container als `/dev/lnot-board` frei, sodass der flüchtige
`ttyACM`-/`ttyUSB`-Name nicht in der Runner-Konfiguration verwendet wird.
Starten Sie den Container nach einer Änderung mit `pct restart <CTID>` neu.
Wird ein Board getrennt oder ausgetauscht, prüfen Sie den `by-id`-Pfad und
starten Sie den Container erneut, damit der Bind-Mount auf das aktuelle Gerät
zeigt.

Im gestarteten Container muss nur dieses Gerät sichtbar und als Zeichengerät
erkennbar sein:

```bash
test -c /dev/lnot-board
```

## Bestehende Runner für Deployment vorbereiten

Die LXC-Container und die GitHub Actions Runner existieren bereits. Der
Deployment-Workflow installiert Docker nur dann, wenn der Befehl noch nicht
vorhanden ist, startet den Dienst und führt den ESP-IDF-Container mit `sudo
docker` aus. Dadurch kann der laufende Runner den Container unmittelbar nach
der Installation verwenden.

Der Runner-Servicebenutzer benötigt dafür passwortloses `sudo` ausschließlich
für Paketverwaltung, Docker und den Docker-Dienst. Ersetzen Sie
`github-runner`, falls Ihr Runner unter einem anderen Benutzer läuft. Führen
Sie dies im LXC als root aus:

```bash
cat >/etc/sudoers.d/github-runner-lnot-deploy <<'EOF'
github-runner ALL=(root) NOPASSWD: /usr/bin/apt-get, /usr/bin/systemctl, /usr/bin/docker
EOF
chmod 440 /etc/sudoers.d/github-runner-lnot-deploy
visudo --check --file=/etc/sudoers.d/github-runner-lnot-deploy
```

Der Workflow verwendet fest `/dev/lnot-board`, den Zielpfad des oben
konfigurierten Bind-Mounts. Es ist daher keine Runner-Umgebungsvariable und
kein Neustart des Runner-Dienstes erforderlich. Der Workflow bricht vor dem
Flashen mit einer konkreten Fehlermeldung ab, wenn dieser Pfad kein
Zeichengerät bezeichnet.

> **Sicherheitsgrenze:** Docker-Zugriff und die erlaubten `sudo`-Befehle sind
> im LXC effektiv Root-Rechte. Beschränken Sie den Zugriff auf diese
> Hardware-Runner auf vertrauenswürdige Workflows von `main` und verwenden Sie
> sie nicht für Pull Requests oder andere fremde Ausführungskontexte.

## Abnahme und Betrieb

Melden Sie sich als Runner-Benutzer an und prüfen Sie vor der Registrierung
beziehungsweise nach Änderungen den auf das eine Gerät begrenzten
Docker-Zugriff:

```bash
su - github-runner
ESPPORT=/dev/lnot-board
test -c "$ESPPORT"
sudo --non-interactive docker run --rm --device="$ESPPORT" -e "ESPPORT=$ESPPORT" espressif/idf:v5.3.2 \
  bash -c 'test -c "$ESPPORT"'
```

Danach führen Sie den Workflow **Firmware CI and hardware deployment** manuell
auf dem Branch `main` aus. Nach der Environment-Freigabe muss der jeweilige
Job den Adapter melden, die Firmware flashen und die Ready-Meldung im
Monitor-Log finden. Bei einem fehlenden Gerät, einem falschen Label oder einer
fehlenden Freigabe darf kein Flash-Vorgang stattfinden.
