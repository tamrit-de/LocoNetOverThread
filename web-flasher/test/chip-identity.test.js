import assert from "node:assert/strict";
import test from "node:test";

import { chipsMatch, normalizeChip } from "../chip-identity.js";

test("normalizes an esptool chip revision without changing the model", () => {
  assert.equal(normalizeChip("ESP32-H2 (revision v0.1)"), "ESP32H2");
  assert.equal(chipsMatch("ESP32-H2", "ESP32-H2 (revision v0.1)"), true);
});

test("requires an exact chip model after normalization", () => {
  assert.equal(chipsMatch("ESP32-H2", "ESP32-C6 (revision v0.1)"), false);
  assert.equal(chipsMatch("ESP32-H2", "ESP32-H2 (development board)"), false);
});
