import assert from "node:assert/strict";
import test from "node:test";

import {
  bootConsoleBaudRate,
  captureBootConsole,
} from "../boot-console.js";

function createSerialPort(readResults) {
  const reader = {
    async read() {
      return readResults.shift() ?? { done: true };
    },
    async cancel() {
      readResults.push({ done: true });
    },
    releaseLock() {
      reader.released = true;
    },
    released: false,
  };
  const port = {
    async open(options) {
      port.openOptions = options;
    },
    async close() {
      port.closed = true;
    },
    readable: {
      getReader() {
        return reader;
      },
    },
    closed: false,
    openOptions: undefined,
  };
  return { port, reader };
}

test("captures boot output at the configured console baud rate", async () => {
  const { port, reader } = createSerialPort([
    { value: new TextEncoder().encode("booting\n"), done: false },
    { value: new TextEncoder().encode("ready\n"), done: false },
    { done: true },
  ]);
  const chunks = [];

  const result = await captureBootConsole(port, {
    onOutput(chunk) {
      chunks.push(chunk);
    },
  });

  assert.deepEqual(port.openOptions, { baudRate: bootConsoleBaudRate });
  assert.equal(result.output, "booting\nready\n");
  assert.deepEqual(chunks, ["booting\n", "ready\n"]);
  assert.equal(result.timedOut, false);
  assert.equal(reader.released, true);
  assert.equal(port.closed, true);
});

test("stops and closes the console when the capture window expires", async () => {
  const { port, reader } = createSerialPort([]);
  reader.read = () =>
    new Promise((resolve) => {
      reader.finishRead = resolve;
    });
  reader.cancel = async () => {
    reader.finishRead({ done: true });
  };
  let timeoutCleared = false;

  const result = await captureBootConsole(port, {
    setTimeoutFn(callback) {
      queueMicrotask(callback);
      return 1;
    },
    clearTimeoutFn() {
      timeoutCleared = true;
    },
  });

  assert.equal(result.timedOut, true);
  assert.equal(timeoutCleared, true);
  assert.equal(reader.released, true);
  assert.equal(port.closed, true);
});

test("closes a port when boot-console reading fails", async () => {
  const { port, reader } = createSerialPort([]);
  reader.read = async () => {
    throw new Error("USB disconnected");
  };

  await assert.rejects(captureBootConsole(port), /USB disconnected/);

  assert.equal(reader.released, true);
  assert.equal(port.closed, true);
});
