export function normalizeChip(chip) {
  const chipWithoutRevision = String(chip).replace(
    /\s+\(revision v?\d+(?:\.\d+)*\)\s*$/i,
    "",
  );
  return chipWithoutRevision.toUpperCase().replace(/[^A-Z0-9]/g, "");
}

export function chipsMatch(manifestChip, detectedChip) {
  return normalizeChip(manifestChip) === normalizeChip(detectedChip);
}
