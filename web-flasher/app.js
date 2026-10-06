const repository = "tamrit-de/LocoNetOverThread";
const channels = ["stable", "beta", "alpha"];
const channelLabels = {
  stable: "Stable",
  beta: "Beta",
  alpha: "Alpha (experimental)",
};
const supportedHardware = {
  client: {
    target: "esp32h2",
    chip: "ESP32-H2",
    board: "ESP32-H2-DevKitM-1-N4",
  },
  "border-router": {
    target: "esp32c6",
    chip: "ESP32-C6",
    board: "ESP32-C6-DevKitM-1-N4",
  },
};
const elements = Object.fromEntries(
  [
    "channel",
    "release",
    "connect",
    "chip",
    "board",
    "confirm-board",
    "firmware",
    "flash",
    "progress",
    "status",
  ].map((id) => [id, document.getElementById(id)]),
);

let manifests = [];
let serialPort;
let transport;
let loader;
let connectedChip;
let supportedBoards = [];

function setStatus(message, isError = false) {
  elements.status.textContent = message;
  elements.status.classList.toggle("error", isError);
}

function normalizeChip(chip) {
  return String(chip).toUpperCase().replace(/[^A-Z0-9]/g, "");
}

function selectedManifest() {
  return manifests.find(
    (manifest) => manifest.version === elements.release.value,
  );
}

function compatibleBoards(manifest) {
  if (!manifest || !connectedChip) return [];
  return manifest.hardware.filter(
    (hardware) => normalizeChip(hardware.chip) === normalizeChip(connectedChip),
  );
}

function updateBoardOptions() {
  const boards = compatibleBoards(selectedManifest());
  supportedBoards = boards;
  elements.board.replaceChildren();
  elements.board.add(new Option("Choose the exact board variant…", ""));
  for (const hardware of boards) {
    elements.board.add(
      new Option(
        `${hardware.board} — ${hardware.application}`,
        hardware.application,
      ),
    );
  }
  elements.board.disabled = boards.length === 0;
  elements["confirm-board"].disabled = boards.length === 0;
  elements["confirm-board"].checked = false;
  if (connectedChip && boards.length === 0) {
    elements.board.replaceChildren(
      new Option(`No ${connectedChip} firmware in this release`, ""),
    );
  }
  updateFlashButton();
}

function updateFlashButton() {
  const boardSelected = supportedBoards.some(
    (hardware) => hardware.application === elements.board.value,
  );
  elements.flash.disabled = !(
    loader &&
    selectedManifest() &&
    boardSelected &&
    elements["confirm-board"].checked
  );
  const hardware = supportedBoards.find(
    (entry) => entry.application === elements.board.value,
  );
  const manifest = selectedManifest();
  elements.firmware.textContent =
    hardware && manifest
      ? `${manifest.version} (${manifest.channel}) — ${hardware.board}`
      : "Waiting for a compatible device and release.";
}

function compareVersions(left, right) {
  return right.version.localeCompare(left.version, undefined, {
    numeric: true,
    sensitivity: "base",
  });
}

async function loadManifest(release) {
  const assetName = `manifest-${release.tag_name}.json`;
  const asset = release.assets.find((entry) => entry.name === assetName);
  if (!asset) throw new Error(`Release has no ${assetName} asset.`);
  const response = await fetch(asset.browser_download_url);
  if (!response.ok) throw new Error(`Could not load ${assetName}.`);
  const manifest = await response.json();
  if (
    manifest.schema_version !== 1 ||
    manifest.version !== release.tag_name ||
    !channels.includes(manifest.channel) ||
    !Array.isArray(manifest.hardware) ||
    manifest.hardware.length === 0 ||
    manifest.hardware.some((hardware) => {
      const supported = supportedHardware[hardware.application];
      return (
        !supported ||
        hardware.target !== supported.target ||
        hardware.chip !== supported.chip ||
        hardware.board !== supported.board ||
        !Array.isArray(hardware.images) ||
        hardware.images.length === 0 ||
        !hardware.flash ||
        !["qio", "qout", "dio", "dout"].includes(hardware.flash.flash_mode) ||
        !/^\d+m$/.test(hardware.flash.flash_freq) ||
        !/^\d+(?:KB|MB|GB)$/.test(hardware.flash.flash_size)
      );
    })
  ) {
    throw new Error(`Release manifest ${assetName} is invalid.`);
  }
  return manifest;
}

async function loadReleases() {
  setStatus("Loading published firmware releases…");
  const releases = [];
  for (let page = 1; ; page += 1) {
    const response = await fetch(
      `https://api.github.com/repos/${repository}/releases?per_page=100&page=${page}`,
      { headers: { Accept: "application/vnd.github+json" } },
    );
    if (!response.ok) throw new Error("GitHub releases could not be loaded.");
    const releasePage = await response.json();
    if (!Array.isArray(releasePage)) {
      throw new Error("GitHub releases could not be read.");
    }
    releases.push(...releasePage);
    if (releasePage.length < 100) break;
  }
  const results = await Promise.allSettled(
    releases
      .filter((release) => !release.draft)
      .map((release) => loadManifest(release)),
  );
  manifests = results
    .filter((result) => result.status === "fulfilled")
    .map((result) => result.value)
    .sort(compareVersions);
  if (manifests.length === 0) {
    throw new Error("No published firmware releases with valid manifests were found.");
  }
  updateChannelOptions();
  setStatus(
    `Releases loaded. ${channelLabels[elements.channel.value]} is selected by default.`,
  );
}

function updateChannelOptions() {
  const availableChannels = channels.filter((channel) =>
    manifests.some((manifest) => manifest.channel === channel),
  );
  const previouslySelected = elements.channel.value;
  elements.channel.replaceChildren();
  for (const channel of availableChannels) {
    elements.channel.add(
      new Option(channelLabels[channel], channel),
    );
  }
  elements.channel.value = availableChannels.includes(previouslySelected)
    ? previouslySelected
    : availableChannels[0];
  elements.channel.disabled = false;
  updateReleaseOptions();
}

function updateReleaseOptions() {
  const channelManifests = manifests.filter(
    (manifest) => manifest.channel === elements.channel.value,
  );
  elements.release.replaceChildren();
  for (const manifest of channelManifests) {
    elements.release.add(
      new Option(manifest.version, manifest.version),
    );
  }
  if (channelManifests.length === 0) {
    elements.release.add(new Option("No releases in this channel", ""));
    elements.release.disabled = true;
  } else {
    elements.release.value = channelManifests[0].version;
    elements.release.disabled = false;
  }
  updateBoardOptions();
}

async function connectDevice() {
  if (!("serial" in navigator)) {
    throw new Error("Web Serial is unavailable. Use Chrome or Microsoft Edge.");
  }
  serialPort = await navigator.serial.requestPort();
  let esptool;
  try {
    esptool = await import("./vendor/esptool.js");
  } catch {
    const error = new Error(
      "The flasher library is missing. Deploy the WebUI using Docker and reload.",
    );
    error.name = "FlasherAssetError";
    throw error;
  }
  const { ESPLoader, Transport } = esptool;
  transport = new Transport(serialPort, true);
  loader = new ESPLoader({
    transport,
    baudrate: 115200,
    debugLogging: false,
    terminal: {
      clean() {},
      writeLine(message) {
        console.info(message);
      },
      write(message) {
        console.info(message);
      },
    },
  });
  setStatus("Connecting to the ESP bootloader…");
  connectedChip = await loader.main();
  elements.chip.textContent = `Detected chip: ${connectedChip}`;
  updateBoardOptions();
  if (supportedBoards.length === 0) {
    setStatus(
      `Detected ${connectedChip}, but no matching firmware is available.`,
      true,
    );
  } else {
    setStatus(
      `${connectedChip} detected. Select and confirm the exact board variant.`,
    );
  }
}

function validImageUrl(value, tag) {
  try {
    const url = new URL(value);
    return (
      url.protocol === "https:" &&
      url.hostname === "github.com" &&
      url.pathname.startsWith(
        `/${repository}/releases/download/${encodeURIComponent(tag)}/`,
      )
    );
  } catch {
    return false;
  }
}

async function downloadImages(manifest, hardware) {
  if (
    !hardware.images.length ||
    !hardware.flash ||
    !["flash_mode", "flash_freq", "flash_size"].every(
      (key) => typeof hardware.flash[key] === "string",
    )
  ) {
    throw new Error("The selected manifest has incomplete flash information.");
  }
  const files = [];
  for (const image of hardware.images) {
    if (
      !validImageUrl(image.url, manifest.version) ||
      !/^[0-9a-f]{64}$/.test(image.sha256) ||
      !Number.isSafeInteger(image.size) ||
      image.size <= 0 ||
      !/^0x[0-9a-f]+$/i.test(image.address)
    ) {
      throw new Error(`Manifest entry ${image.name} is invalid.`);
    }
    setStatus(`Downloading and verifying ${image.name}…`);
    const response = await fetch(image.url);
    if (!response.ok) throw new Error(`Could not download ${image.name}.`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== image.size) {
      throw new Error(`Size check failed for ${image.name}.`);
    }
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    const actualHash = Array.from(new Uint8Array(digest), (byte) =>
      byte.toString(16).padStart(2, "0"),
    ).join("");
    if (actualHash !== image.sha256) {
      throw new Error(`SHA-256 verification failed for ${image.name}.`);
    }
    files.push({ data: bytes, address: Number.parseInt(image.address, 16) });
  }
  return { files, flash: hardware.flash };
}

async function flashSelectedFirmware() {
  const manifest = selectedManifest();
  const hardware = supportedBoards.find(
    (entry) => entry.application === elements.board.value,
  );
  if (!manifest || !hardware || !elements["confirm-board"].checked) {
    throw new Error("Select and confirm the exact supported board first.");
  }
  if (
    !window.confirm(
      `Flash ${manifest.version} (${manifest.channel}) to ${hardware.board}?`,
    )
  ) {
    return;
  }
  elements.flash.disabled = true;
  elements.progress.hidden = false;
  elements.progress.value = 0;
  const { files, flash } = await downloadImages(manifest, hardware);
  const totalBytes = files.reduce((total, file) => total + file.data.length, 0);
  await loader.writeFlash({
    fileArray: files,
    flashMode: flash.flash_mode,
    flashFreq: flash.flash_freq,
    flashSize: flash.flash_size,
    eraseAll: false,
    compress: true,
    reportProgress(fileIndex, written) {
      const completed = files
        .slice(0, fileIndex)
        .reduce((total, file) => total + file.data.length, 0);
      elements.progress.value = Math.round(
        ((completed + written) / totalBytes) * 100,
      );
    },
  });
  setStatus("Flash complete. Restarting the device…");
  await loader.after("hard_reset");
  await transport.disconnect();
  loader = undefined;
  transport = undefined;
  serialPort = undefined;
  connectedChip = undefined;
  elements.chip.textContent = "Device restarted.";
  elements.board.replaceChildren(new Option("Connect a supported device first", ""));
  elements.board.disabled = true;
  elements["confirm-board"].checked = false;
  elements["confirm-board"].disabled = true;
  elements.progress.value = 100;
  setStatus(`Successfully flashed ${manifest.version} (${manifest.channel}).`);
}

elements.channel.addEventListener("change", updateReleaseOptions);
elements.release.addEventListener("change", updateBoardOptions);
elements.board.addEventListener("change", updateFlashButton);
elements["confirm-board"].addEventListener("change", updateFlashButton);
elements.connect.addEventListener("click", async () => {
  elements.connect.disabled = true;
  try {
    await connectDevice();
  } catch (error) {
    const message =
      error.name === "NotFoundError"
        ? "No serial port was selected."
        : error.name === "FlasherAssetError"
          ? error.message
          : `Could not connect to the device. Enter bootloader mode by holding BOOT while reconnecting USB, then try again. ${error.message}`;
    setStatus(message, true);
    if (transport) await transport.disconnect().catch(() => {});
    loader = undefined;
    transport = undefined;
    serialPort = undefined;
    connectedChip = undefined;
    updateBoardOptions();
  } finally {
    elements.connect.disabled = false;
  }
});
elements.flash.addEventListener("click", async () => {
  try {
    await flashSelectedFirmware();
  } catch (error) {
    setStatus(
      `Flashing failed: ${error.message} Keep the device connected, enter bootloader mode if needed, and retry.`,
      true,
    );
  } finally {
    updateFlashButton();
  }
});

loadReleases().catch((error) => setStatus(error.message, true));
