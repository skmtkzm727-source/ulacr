import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";

const distDir = path.resolve(process.argv[2] || "dist");

// Run the exact browser worker under Node with a minimal WorkerGlobalScope shim.
// This exercises the built Emscripten module, worker message protocol and codec roundtrip.
globalThis.WorkerGlobalScope = class WorkerGlobalScope {};
globalThis.self = globalThis;

const handlers = new Map();
const messages = [];
globalThis.addEventListener = (name, handler) => handlers.set(name, handler);
globalThis.postMessage = (data) => messages.push(data);
globalThis.fetch = async (input) => {
  const url = new URL(typeof input === "string" ? input : input.url);
  const filePath = fileURLToPath(url);
  const bytes = await readFile(filePath);
  return new Response(bytes, {
    status: 200,
    headers: {
      "Content-Type": filePath.endsWith(".wasm")
        ? "application/wasm"
        : "application/octet-stream"
    }
  });
};

const workerUrl = pathToFileURL(path.join(distDir, "codec-worker.js"));
workerUrl.searchParams.set("smoke-test", String(Date.now()));
await import(workerUrl.href);

const handler = handlers.get("message");
assert.equal(typeof handler, "function", "Worker message handler must be registered");

function makeWav() {
  const sampleRate = 8000;
  const frames = 800;
  const pcm = Buffer.alloc(frames * 2);
  for (let i = 0; i < frames; i++) {
    const sample = Math.round(12000 * Math.sin(2 * Math.PI * 440 * i / sampleRate));
    pcm.writeInt16LE(sample, i * 2);
  }

  const wav = Buffer.alloc(44 + pcm.length);
  wav.write("RIFF", 0);
  wav.writeUInt32LE(36 + pcm.length, 4);
  wav.write("WAVE", 8);
  wav.write("fmt ", 12);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(sampleRate, 24);
  wav.writeUInt32LE(sampleRate * 2, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write("data", 36);
  wav.writeUInt32LE(pcm.length, 40);
  pcm.copy(wav, 44);
  return wav;
}

function makeStressWav() {
  const sampleRate = 8000;
  const frames = sampleRate * 20;
  const pcm = Buffer.alloc(frames * 2);
  for (let i = 0; i < frames; i++) {
    // Alternate silence and a high-crossing square wave every 256 samples.
    // This forces the segmenter to create many minimum-size blocks and
    // stresses repeated LSH buckets, the real worst case for this index.
    const block = Math.floor(i / 256);
    const sample = block % 2 === 0 ? 0 : (i % 2 === 0 ? 12000 : -12000);
    pcm.writeInt16LE(sample, i * 2);
  }

  const wav = Buffer.alloc(44 + pcm.length);
  wav.write("RIFF", 0);
  wav.writeUInt32LE(36 + pcm.length, 4);
  wav.write("WAVE", 8);
  wav.write("fmt ", 12);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(sampleRate, 24);
  wav.writeUInt32LE(sampleRate * 2, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write("data", 36);
  wav.writeUInt32LE(pcm.length, 40);
  pcm.copy(wav, 44);
  return wav;
}

async function invoke(id, mode, bytes) {
  messages.length = 0;
  const input = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  await handler({ data: { id, mode, input } });
  const error = messages.find(message => message.type === "error");
  const result = messages.find(message => message.type === "result");
  return {
    error,
    output: result ? Buffer.from(result.output) : null,
    statuses: messages.filter(message => message.type === "status").map(message => message.message),
    progress: messages.filter(message => message.type === "progress")
  };
}

const malformed = await invoke(1, "decode", Buffer.from([0, 1, 2, 3]));
assert.ok(malformed.error, "Malformed input should return a readable error");

const original = makeWav();
const encoded = await invoke(2, "encode", new Uint8Array(original));
assert.ok(!encoded.error, encoded.error?.message);
assert.ok(encoded.output?.length > 0, "Encoding should return bytes");
assert.ok(encoded.statuses.some(status => status.includes("読み込んでいます")));
assert.ok(encoded.statuses.some(status => status.includes("圧縮しています")));
assert.ok(encoded.progress.length > 0, "Encoder should emit progress events");
assert.ok(encoded.progress.every(item => item.id === 2 && Number.isFinite(item.progress)));

const decoded = await invoke(3, "decode", new Uint8Array(encoded.output));
assert.ok(!decoded.error, decoded.error?.message);
assert.deepEqual(decoded.output, original, "Decoded WAV must match input byte-for-byte");
assert.ok(decoded.statuses.some(status => status.includes("復元しています")));
assert.ok(decoded.progress.length > 0, "Decoder should emit progress events");
assert.ok(decoded.progress.every(item => item.id === 3 && Number.isFinite(item.progress)));

const stressInput = makeStressWav();
const stressStart = Date.now();
const stressEncoded = await invoke(4, "encode", new Uint8Array(stressInput));
const stressElapsed = Date.now() - stressStart;
assert.ok(!stressEncoded.error, stressEncoded.error?.message);
assert.ok(stressEncoded.output?.length > 0);
assert.ok(stressEncoded.progress.length > 0, "Block graph should report progress");
const graphProgress = stressEncoded.progress.filter(item => item.phase === "block-graph");
assert.ok(graphProgress.length > 0, "Block graph stage progress must be emitted");
assert.ok(graphProgress.length >= 10,
  "Stress input should create enough segments to exercise incremental indexing (" + graphProgress.length + " updates)");
assert.ok(graphProgress.some(item => item.progress >= 0.099),
  "Block graph progress must finish at the 10% encoder-stage boundary");
const stressDecoded = await invoke(5, "decode", new Uint8Array(stressEncoded.output));
assert.ok(!stressDecoded.error, stressDecoded.error?.message);
assert.deepEqual(stressDecoded.output, stressInput, "Stress block-graph roundtrip must be byte-exact");
assert.ok(stressElapsed < 20000,
  "20-second high-crossing audio must not stall block-graph analysis (" + stressElapsed + " ms)");

// Cross-runtime regression: native-generated 0.2 and 0.3 streams must decode
// in the browser WASM worker to the exact original WAV byte sequence.
async function verifyNativeStream(encodedPath, wavPath, id, label) {
  const nativeStream = new Uint8Array(await readFile(path.resolve(encodedPath)));
  const expectedWav = await readFile(path.resolve(wavPath));
  const nativeDecoded = await invoke(id, "decode", nativeStream);
  assert.ok(!nativeDecoded.error, label + ": " + nativeDecoded.error?.message);
  assert.deepEqual(nativeDecoded.output, expectedWav, label + " native->WASM decode must be byte-exact");
}
if (process.argv[3] && process.argv[4]) {
  await verifyNativeStream(process.argv[3], process.argv[4], 6, "native v0.3");
}
if (process.argv[5] && process.argv[6]) {
  await verifyNativeStream(process.argv[5], process.argv[6], 7, "native v0.2");
}

console.log(
  "ULAC-R worker smoke test passed:",
  original.length, "bytes ->", encoded.output.length,
  "bytes ->", decoded.output.length, "bytes; roundtrip exact"
);
console.log(
  "Block-graph stress test passed:",
  stressInput.length, "bytes in", stressElapsed, "ms;",
  graphProgress.length, "progress updates"
);
