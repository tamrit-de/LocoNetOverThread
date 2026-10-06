import assert from "node:assert/strict";
import test from "node:test";

import { supportedHardware } from "../firmware-profiles.js";

test("defines the OpenThread RCP profile for the supported H2 board", () => {
  assert.deepEqual(supportedHardware.rcp, {
    target: "esp32h2",
    chip: "ESP32-H2",
    board: "ESP32-H2-DevKitM-1-N4",
    label: "OpenThread RCP (SPI)",
  });
});
