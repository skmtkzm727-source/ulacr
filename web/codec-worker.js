import createULACRModule from "./ulacr.js?v=ulacr-ultra-20261109d";

let codecPromise = null;
let processing = false;

function getCodec() {
  if (!codecPromise) {
    codecPromise = createULACRModule({
      locateFile: function(path) {
        return new URL(path + "?v=ulacr-ultra-20261109d", import.meta.url).href;
      },
      print: function(message) {
        console.log("[ULAC-R]", message);
      },
      printErr: function(message) {
        console.error("[ULAC-R]", message);
      }
    }).catch(function(error) {
      codecPromise = null;
      throw error;
    });
  }
  return codecPromise;
}

function errorForCode(code) {
  const messages = {
    "-1": "WAVまたはULAC-Rファイルの内容を確認してください。",
    "-2": "対応していない音声形式です。PCM形式のWAVをお試しください。",
    "-3": "音声ファイルの読み書きに失敗しました。",
    "-4": "ULAC-Rファイルが破損しているか、形式が一致しません。",
    "-5": "メモリ不足です。短い音声ファイルでお試しください。"
  };
  return messages[String(code)] || ("変換エラー（" + code + "）");
}

self.addEventListener("message", async function(event) {
  const data = event.data || {};
  const id = data.id;
  const mode = data.mode;
  const input = data.input;
  let module = null;
  let inputPtr = 0;

  function status(message) {
    self.postMessage({ type: "status", id: id, message: message });
  }

  if (processing) {
    self.postMessage({ type: "error", id: id, message: "別の音声処理が実行中です。完了後にもう一度お試しください。" });
    return;
  }
  processing = true;

  try {
    if (!Number.isInteger(id)) throw new Error("処理IDが不正です。");
    if (mode !== "encode" && mode !== "decode") throw new Error("処理モードが不正です。");
    if (!(input instanceof ArrayBuffer) || input.byteLength === 0) {
      throw new Error("音声ファイルが空か、読み込めませんでした。");
    }

    status("WebAssembly音声エンジンを読み込んでいます…");
    module = await getCodec();

    status(mode === "encode"
      ? "WAVをULAC-Rへ圧縮しています…（画面は操作できます）"
      : "ULAC-RをWAVへ復元しています…（画面は操作できます）");

    const inputBytes = new Uint8Array(input);
    inputPtr = module._malloc(inputBytes.byteLength);
    if (!inputPtr) throw new Error("ブラウザのメモリを確保できませんでした。短い音声ファイルでお試しください。");

    module.HEAPU8.set(inputBytes, inputPtr);
    self.__ulacr_active_request_id = id;
    const code = mode === "encode"
      ? module._ulacr_encode(inputPtr, inputBytes.byteLength)
      : module._ulacr_decode(inputPtr, inputBytes.byteLength);

    if (code !== 0) {
      const error = new Error(errorForCode(code));
      error.code = code;
      throw error;
    }

    const outputPtr = module._ulacr_output_ptr();
    const outputSize = module._ulacr_output_size();
    if (!outputPtr || !outputSize ||
        outputPtr + outputSize > module.HEAPU8.byteLength) {
      throw new Error("変換結果を正しく取得できませんでした。");
    }

    const output = module.HEAPU8.slice(outputPtr, outputPtr + outputSize);
    self.postMessage(
      { type: "result", id: id, output: output.buffer },
      [output.buffer]
    );
  } catch (error) {
    console.error("[ULAC-R worker]", error);
    self.postMessage({
      type: "error",
      id: id,
      message: error && error.message ? error.message : String(error),
      code: error && Number.isInteger(error.code) ? error.code : null
    });
  } finally {
    if (module) {
      try { module._ulacr_clear_output(); } catch (_) {}
      if (inputPtr) {
        try { module._free(inputPtr); } catch (_) {}
      }
    }
    delete self.__ulacr_active_request_id;
    processing = false;
  }
});
