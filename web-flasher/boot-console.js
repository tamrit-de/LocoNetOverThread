export const bootConsoleBaudRate = 115200;
export const bootConsoleDurationMs = 15_000;

export async function captureBootConsole(
  serialPort,
  {
    baudRate = bootConsoleBaudRate,
    durationMs = bootConsoleDurationMs,
    setTimeoutFn = globalThis.setTimeout,
    clearTimeoutFn = globalThis.clearTimeout,
    onOutput,
  } = {},
) {
  let reader;
  let timeoutId;
  let opened = false;
  let timedOut = false;
  let cancellationError;
  const decoder = new TextDecoder();
  let output = "";

  try {
    await serialPort.open({ baudRate });
    opened = true;
    if (!serialPort.readable) {
      throw new Error("The serial port did not provide a readable boot console.");
    }

    reader = serialPort.readable.getReader();
    timeoutId = setTimeoutFn(async () => {
      timedOut = true;
      try {
        await reader.cancel();
      } catch (error) {
        cancellationError = error;
      }
    }, durationMs);

    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) {
        const chunk = decoder.decode(value, { stream: true });
        output += chunk;
        if (chunk) onOutput?.(chunk);
      }
    }
    const finalChunk = decoder.decode();
    output += finalChunk;
    if (finalChunk) onOutput?.(finalChunk);
    if (cancellationError) {
      throw new Error(
        `Could not stop boot-console capture: ${cancellationError.message}`,
      );
    }
    return { output, timedOut };
  } finally {
    if (reader) {
      try {
        if (timeoutId !== undefined) {
          clearTimeoutFn(timeoutId);
        }
      } finally {
        try {
          reader.releaseLock();
        } finally {
          if (opened) {
            await serialPort.close();
          }
        }
      }
    } else if (opened) {
      await serialPort.close();
    }
  }
}
