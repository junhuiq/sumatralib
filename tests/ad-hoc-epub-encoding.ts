// Ad-hoc test: MuPDF's XML/HTML parser must not mangle UTF-8 content that
// (wrongly) declares a legacy single-byte encoding (e.g. iso-8859-1).
//
// Many EPUB/HTML files in the wild are UTF-8 but declare iso-8859-1 or
// windows-1252. Decoding the UTF-8 bytes as the declared encoding turned the
// curly apostrophe ' (U+2019, UTF-8 e2 80 99) into "a" + two invisible control
// characters, i.e. it displayed as a circumflex-a. The fix prefers valid UTF-8
// over a declared legacy single-byte encoding while still converting genuinely
// legacy-encoded content.
//
// Not registered in tests/all.ts:
//   bun tests/ad-hoc-epub-encoding.ts [--no-build]
// or as part of: bun tests/before-release.ts

import { writeFileSync } from "node:fs";
import { EXE, runStandalone, tmpPath } from "./util.ts";

function fail(msg: string): never {
  throw new Error(msg);
}

// ascii prefix followed by the given raw bytes (so we control the exact bytes
// of the file, independently of any editor encoding)
function bytes(ascii: string, ...tail: number[]): Uint8Array {
  const head = new TextEncoder().encode(ascii);
  const out = new Uint8Array(head.length + tail.length);
  out.set(head);
  out.set(tail, head.length);
  return out;
}

// runs -extract-text (debug build) and returns the hex-encoded extracted text
async function extractTextHex(file: string): Promise<string> {
  const p = Bun.spawnSync({
    cmd: [EXE, "-for-testing", "-extract-text", "1", file],
    stdout: "pipe",
    stderr: "pipe",
  });
  if (p.exitCode !== 0) {
    fail(`${EXE} -extract-text exited with ${p.exitCode}: ${p.stderr}`);
  }
  const out = p.stdout.toString();
  const m = /text on page 1: '([0-9a-f ]+)'/.exec(out);
  if (!m) {
    fail(`no extracted text in output:\n${out}`);
  }
  return m[1];
}

export async function testit(): Promise<void> {
  const utf8Apostrophe = "e2 80 99"; // U+2019 right single quotation mark

  // UTF-8 content (with a literal ' byte sequence) wrongly declared as
  // iso-8859-1; the bug rendered it as "a" (c3 a2) + controls
  const utf8AsIso = tmpPath("ad-hoc-epub-encoding-utf8-as-iso.html");
  writeFileSync(
    utf8AsIso,
    bytes(
      '<?xml version="1.0" encoding="iso-8859-1"?><html><body><p>curly: don',
      0xe2, 0x80, 0x99, // '
      't stop.</p></body></html>',
    ),
  );

  // genuinely windows-1252 encoded content must still be converted (0x92 -> ')
  const real1252 = tmpPath("ad-hoc-epub-encoding-real-1252.html");
  writeFileSync(
    real1252,
    bytes(
      '<?xml version="1.0" encoding="windows-1252"?><html><body><p>curly: don',
      0x92, // ' in windows-1252
      't stop.</p></body></html>',
    ),
  );

  // control: plain UTF-8 with correct declaration is unaffected
  const plainUtf8 = tmpPath("ad-hoc-epub-encoding-utf8.html");
  writeFileSync(
    plainUtf8,
    bytes(
      '<?xml version="1.0" encoding="utf-8"?><html><body><p>curly: don',
      0xe2, 0x80, 0x99,
      't stop.</p></body></html>',
    ),
  );

  const mislabeled = await extractTextHex(utf8AsIso);
  if (!mislabeled.includes(utf8Apostrophe) || mislabeled.includes("c3 a2")) {
    fail(`UTF-8 content declared iso-8859-1 was mangled: ${mislabeled}`);
  }

  const legacy1252 = await extractTextHex(real1252);
  if (!legacy1252.includes(utf8Apostrophe)) {
    fail(`genuine windows-1252 content no longer converted: ${legacy1252}`);
  }

  const plain = await extractTextHex(plainUtf8);
  if (!plain.includes(utf8Apostrophe)) {
    fail(`plain UTF-8 content was mangled: ${plain}`);
  }
}

if (import.meta.main) {
  await runStandalone(testit);
}
