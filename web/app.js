import createULACRModule from "./ulacr.js";

const fileInput = document.getElementById("fileInput");
const chooseButton = document.getElementById("chooseButton");
const dropzone = document.getElementById("dropzone");
const filePanel = document.getElementById("filePanel");
const fileName = document.getElementById("fileName");
const fileInfo = document.getElementById("fileInfo");
const encodeButton = document.getElementById("encodeButton");
const decodeButton = document.getElementById("decodeButton");
const previewButton = document.getElementById("previewButton");
const removeButton = document.getElementById("removeButton");
const statusPanel = document.getElementById("statusPanel");
const statusText = document.getElementById("statusText");
const spinner = document.getElementById("spinner");
const resultPanel = document.getElementById("resultPanel");
const resultTitle = document.getElementById("resultTitle");
const stats = document.getElementById("stats");
const beforeSize = document.getElementById("beforeSize");
const afterSize = document.getElementById("afterSize");
const ratio = document.getElementById("ratio");
const downloadLink = document.getElementById("downloadLink");
const playerPanel = document.getElementById("playerPanel");
const audioPlayer = document.getElementById("audioPlayer");

let selectedFile = null;
let selectedKind = null;
let codecPromise = null;
let latestEncodedBytes = null;
let downloadUrl = null;
let audioUrl = null;

function formatBytes(value) {
  if (value < 1024) return value + " B";
  if (value < 1024 * 1024) return (value / 1024).toFixed(1) + " KB";
  return (value / (1024 * 1024)).toFixed(2) + " MB";
}

function setStatus(message, busy) {
  statusPanel.hidden = !message;
  statusText.textContent = message || "";
  spinner.hidden = !busy;
  encodeButton.disabled = !!busy;
  decodeButton.disabled = !!busy;
  previewButton.disabled = !!busy;
  chooseButton.disabled = !!busy;
}

function setAudio(blob) {
  if (audioUrl) URL.revokeObjectURL(audioUrl);
  audioUrl = URL.createObjectURL(blob);
  audioPlayer.src = audioUrl;
  playerPanel.hidden = false;
}

function clearOutput() {
  resultPanel.hidden = true;
  stats.hidden = true;
  previewButton.hidden = true;
  latestEncodedBytes = null;
  if (downloadUrl) URL.revokeObjectURL(downloadUrl);
  downloadUrl = null;
  downloadLink.removeAttribute("href");
  playerPanel.hidden = true;
  audioPlayer.removeAttribute("src");
  audioPlayer.load();
  if (audioUrl) URL.revokeObjectURL(audioUrl);
  audioUrl = null;
}

function setDownload(blob, name) {
  if (downloadUrl) URL.revokeObjectURL(downloadUrl);
  downloadUrl = URL.createObjectURL(blob);
  downloadLink.href = downloadUrl;
  downloadLink.download = name;
  downloadLink.textContent = name + " をダウンロード";
  resultPanel.hidden = false;
}

function showStats(inputBytes, outputBytes) {
  beforeSize.textContent = formatBytes(inputBytes);
  afterSize.textContent = formatBytes(outputBytes);
  ratio.textContent = inputBytes > 0 ? ((outputBytes / inputBytes) * 100).toFixed(1) + "%" : "—";
  stats.hidden = false;
}

function showFile(file) {
  const name = file.name || "audio";
  const lower = name.toLowerCase();
  let kind = null;
  if (lower.endsWith(".wav") || lower.endsWith(".wave") || file.type === "audio/wav") kind = "wav";
  if (lower.endsWith(".ulacr")) kind = "ulacr";
  if (!kind) {
    setStatus("WAVまたはULAC-R（.ulacr）ファイルを選んでください。", false);
    return;
  }

  clearOutput();
  selectedFile = file;
  selectedKind = kind;
  fileName.textContent = name;
  fileInfo.textContent = (kind === "wav" ? "WAV音声" : "ULAC-R圧縮音声") + " · " + formatBytes(file.size);
  dropzone.hidden = true;
  filePanel.hidden = false;
  encodeButton.hidden = kind !== "wav";
  decodeButton.hidden = kind !== "ulacr";
  setStatus("", false);

  if (kind === "wav") {
    setAudio(file);
  } else {
    playerPanel.hidden = true;
  }
}

async function getCodec() {
  if (!codecPromise) {
    setStatus("音声エンジン（WebAssembly）を読み込んでいます…", true);
    codecPromise = createULACRModule({
      locateFile: function(path) {
        return new URL(path, import.meta.url).href;
      }
    });
  }
  return codecPromise;
}

async function runCodec(mode, bytes) {
  const module = await getCodec();
  const inputPtr = module._malloc(bytes.length);
  if (!inputPtr) throw new Error("ブラウザのメモリを確保できませんでした。ファイルサイズを小さくしてお試しください。");

  let code = 0;
  try {
    module.HEAPU8.set(bytes, inputPtr);
    code = mode === "encode"
      ? module._ulacr_encode(inputPtr, bytes.length)
      : module._ulacr_decode(inputPtr, bytes.length);
  } finally {
    module._free(inputPtr);
  }

  if (code !== 0) {
    const messages = {
      "-1": "ファイル形式または入力内容を確認してください。",
      "-2": "この音声形式には対応していません。PCM WAVをお試しください。",
      "-3": "ファイルの読み書きに失敗しました。",
      "-4": "ULAC-Rファイルが破損しているか、形式が一致しません。"
    };
    throw new Error(messages[String(code)] || ("変換エラー（" + code + "）"));
  }

  const outputPtr = module._ulacr_output_ptr();
  const outputSize = module._ulacr_output_size();
  if (!outputPtr || !outputSize) throw new Error("変換結果を取得できませんでした。");
  const result = module.HEAPU8.slice(outputPtr, outputPtr + outputSize);
  module._ulacr_clear_output();
  return result;
}

function setBusy(message) {
  setStatus(message, true);
  encodeButton.disabled = true;
  decodeButton.disabled = true;
  previewButton.disabled = true;
  removeButton.disabled = true;
}

function endBusy(message) {
  setStatus(message, false);
  encodeButton.disabled = false;
  decodeButton.disabled = false;
  previewButton.disabled = false;
  removeButton.disabled = false;
}

encodeButton.addEventListener("click", async function() {
  if (!selectedFile || selectedKind !== "wav") return;
  try {
    setBusy("WAVを読み込み、ULAC-Rへ圧縮しています。ファイルサイズによって時間がかかります…");
    const source = new Uint8Array(await selectedFile.arrayBuffer());
    const bytes = await runCodec("encode", source);
    latestEncodedBytes = bytes;
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const outputName = selectedFile.name.replace(/\.(wav|wave)$/i, "") + ".ulacr";
    resultTitle.textContent = "ULAC-Rへの変換が完了しました";
    setDownload(blob, outputName);
    showStats(selectedFile.size, bytes.length);
    previewButton.hidden = false;
    endBusy("変換が完了しました。復元して試聴することもできます。");
  } catch (error) {
    endBusy("変換できませんでした: " + error.message);
  }
});

async function decodeAndPlay(bytes, sourceSize, outputName, showDownload) {
  setBusy("ULAC-Rを復元しています…");
  const wavBytes = await runCodec("decode", bytes);
  const blob = new Blob([wavBytes], { type: "audio/wav" });
  setAudio(blob);
  if (showDownload) {
    resultTitle.textContent = "WAVへの復元が完了しました";
    setDownload(blob, outputName);
    showStats(sourceSize, wavBytes.length);
  }
  endBusy("復元しました。下のプレーヤーから試聴できます。");
}

decodeButton.addEventListener("click", async function() {
  if (!selectedFile || selectedKind !== "ulacr") return;
  try {
    const bytes = new Uint8Array(await selectedFile.arrayBuffer());
    const outputName = selectedFile.name.replace(/\.ulacr$/i, "") + ".wav";
    await decodeAndPlay(bytes, selectedFile.size, outputName, true);
  } catch (error) {
    endBusy("復元できませんでした: " + error.message);
  }
});

previewButton.addEventListener("click", async function() {
  if (!latestEncodedBytes) return;
  try {
    await decodeAndPlay(latestEncodedBytes, 0, "", false);
  } catch (error) {
    endBusy("試聴用の復元に失敗しました: " + error.message);
  }
});

function resetSelection() {
  selectedFile = null;
  selectedKind = null;
  fileInput.value = "";
  filePanel.hidden = true;
  dropzone.hidden = false;
  setStatus("", false);
  clearOutput();
}
chooseButton.addEventListener("click", function() { fileInput.click(); });
fileInput.addEventListener("change", function(event) {
  const file = event.target.files && event.target.files[0];
  if (file) showFile(file);
});
removeButton.addEventListener("click", resetSelection);

["dragenter", "dragover"].forEach(function(eventName) {
  dropzone.addEventListener(eventName, function(event) {
    event.preventDefault();
    dropzone.classList.add("dragover");
  });
});
["dragleave", "drop"].forEach(function(eventName) {
  dropzone.addEventListener(eventName, function(event) {
    event.preventDefault();
    dropzone.classList.remove("dragover");
  });
});
dropzone.addEventListener("drop", function(event) {
  const file = event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0];
  if (file) showFile(file);
});
