// =============================================================================
//  scripts.js  —  WASM Compressor UI
// =============================================================================

// ── State ────────────────────────────────────────────────────────────────────

let resultData  = null;
let currentAlgo = 'rle';
let deltaMode   = 'self';
let wasmReady   = false;

// ── Stubs (defined immediately so onclick attributes never throw ReferenceError)
//    Real implementations are installed by initModule() once WASM is ready.
// ─────────────────────────────────────────────────────────────────────────────

function compressFile()   { if (!wasmReady) return showError('WASM still loading — please wait a moment and try again.'); }
function decompressFile() { if (!wasmReady) return showError('WASM still loading — please wait a moment and try again.'); }
function deltaCompress()  { if (!wasmReady) return showError('WASM still loading — please wait a moment and try again.'); }
function deltaDecompress(){ if (!wasmReady) return showError('WASM still loading — please wait a moment and try again.'); }
function downloadResult() { if (!resultData) return; _downloadResult(); }

function _downloadResult() {
    const blob = new Blob([resultData]);
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
    a.href = url; a.download = 'output.bin'; a.click();
    URL.revokeObjectURL(url);
}

// ── UI helpers ───────────────────────────────────────────────────────────────

function showError(msg) {
    const card = document.getElementById('output-card');
    document.getElementById('error-msg').textContent = msg;
    document.getElementById('error-box').style.display = 'block';
    document.getElementById('result-box').style.display = 'none';
    card.classList.add('visible');
}

function hideError() {
    document.getElementById('error-box').style.display = 'none';
    document.getElementById('result-box').style.display = 'block';
}

function fmtBytes(n) {
    if (n < 1024)        return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / (1024 * 1024)).toFixed(2) + ' MB';
}

function fmtTime(ms) {
    if (ms < 1)       return ms.toFixed(3) + ' ms';
    if (ms < 1000)    return ms.toFixed(1) + ' ms';
    return (ms / 1000).toFixed(2) + ' s';
}

function showResult(original, output, pipeline, isDecompress, elapsedMs) {
    resultData = output;
    hideError();

    const ratio = original.length > 0
        ? (output.length / original.length * 100).toFixed(1)
        : '0';
    document.getElementById('stat-original').textContent = fmtBytes(original.length);
    document.getElementById('stat-output').textContent   = fmtBytes(output.length);
    document.getElementById('stat-ratio').textContent    = ratio + '%';
    document.getElementById('stat-time').textContent     = elapsedMs != null ? fmtTime(elapsedMs) : '—';

    const pr = document.getElementById('pipeline-row');
    pr.innerHTML = pipeline.map((s, i) =>
        `<span class="step">${s}</span>${i < pipeline.length - 1 ? '<span class="arrow">→</span>' : ''}`
    ).join('');

    const note = document.getElementById('delta-note');
    if (currentAlgo === 'delta' && !document.getElementById('chainCheck').checked && !isDecompress) {
        note.style.display = 'block';
        note.textContent = 'Delta alone outputs ~100% of the original size — it reshapes data, not shrinks it. Enable "Chain with Huffman/RLE" above to get real compression.';
    } else {
        note.style.display = 'none';
    }

    document.getElementById('output-card').classList.add('visible');
}

// ── Algo selector ─────────────────────────────────────────────────────────────

document.querySelectorAll('.algo-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.algo-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentAlgo = btn.dataset.algo;
        const isDelta = currentAlgo === 'delta';
        document.getElementById('standard-panel').style.display = isDelta ? 'none' : 'block';
        document.getElementById('delta-panel').classList.toggle('visible', isDelta);
        document.getElementById('output-card').classList.remove('visible');
    });
});

// ── Delta mode ────────────────────────────────────────────────────────────────

function selectDeltaMode(mode) {
    deltaMode = mode;
    document.getElementById('mode-self').classList.toggle('active', mode === 'self');
    document.getElementById('mode-ref').classList.toggle('active',  mode === 'ref');
    document.getElementById('ref-file-section').classList.toggle('visible', mode === 'ref');
}

// ── File name display ─────────────────────────────────────────────────────────

function bindFilename(inputId, displayId) {
    document.getElementById(inputId).addEventListener('change', e => {
        const f = e.target.files[0];
        document.getElementById(displayId).textContent = f ? f.name : '';

        // Enable compress/decompress buttons when a file is chosen (non-delta)
        if (inputId === 'fileInput') {
            document.getElementById('btn-compress').disabled   = !f || !wasmReady;
            document.getElementById('btn-decompress').disabled = !f || !wasmReady;
        }
    });
}
bindFilename('fileInput',      'standard-filename');
bindFilename('deltaFileInput', 'delta-filename');
bindFilename('refFileInput',   'ref-filename');

// ── Drag-over styles ──────────────────────────────────────────────────────────

['standard-drop', 'delta-drop', 'ref-drop'].forEach(id => {
    const el = document.getElementById(id);
    el.addEventListener('dragover',  e => { e.preventDefault(); el.classList.add('drag-over'); });
    el.addEventListener('dragleave', ()  => el.classList.remove('drag-over'));
    el.addEventListener('drop',      ()  => el.classList.remove('drag-over'));
});

// ── WASM init ─────────────────────────────────────────────────────────────────

function initModule() {
    // Verify key WASM exports exist before proceeding
    if (typeof Module._rleCompress !== 'function') {
        showError(
            'WASM exports not found.\n' +
            'Make sure compressor.js was built with all EXPORTED_FUNCTIONS ' +
            '(_rleCompress, _rleDecompress, _lz77Compress, _lz77Decompress, ' +
            '_huffmanCompress, _huffmanDecompress, _zstdCompress, _zstdDecompress, ' +
            '_deltaCompress, _deltaDecompress, _deltaCompressRef, _deltaDecompressRef) ' +
            'and that the file loaded without network errors.'
        );
        return;
    }

    wasmReady = true;

    // Enable buttons if a file is already chosen
    const hasFile = !!document.getElementById('fileInput').files[0];
    document.getElementById('btn-compress').disabled   = !hasFile;
    document.getElementById('btn-decompress').disabled = !hasFile;

    // ── WASM call wrappers ────────────────────────────────────────────────────

    const _rleC   = (i, o, n)       => Module._rleCompress(i, o, n);
    const _rleD   = (i, o, n)       => Module._rleDecompress(i, o, n);
    const _lzC    = (i, o, n)       => Module._lz77Compress(i, o, n);
    const _lzD    = (i, o, n)       => Module._lz77Decompress(i, o, n);
    const _hufC   = (i, o, n)       => Module._huffmanCompress(i, o, n);
    const _hufD   = (i, o, n)       => Module._huffmanDecompress(i, o, n);
    const _zstdC  = (i, o, n)       => Module._zstdCompress(i, o, n);
    const _zstdD  = (i, o, n)       => Module._zstdDecompress(i, o, n);
    const _dC     = (i, o, n)       => Module._deltaCompress(i, o, n);
    const _dD     = (i, o, n)       => Module._deltaDecompress(i, o, n);
    const _dRefC  = (s, r, o, sn, rn) => Module._deltaCompressRef(s, r, o, sn, rn);
    const _dRefD  = (s, r, o, sn, rn) => Module._deltaDecompressRef(s, r, o, sn, rn);

    const fns = {
        rle:     { compress: _rleC,  decompress: _rleD  },
        lz77:    { compress: _lzC,   decompress: _lzD   },
        huffman: { compress: _hufC,  decompress: _hufD  },
        zstd:    { compress: _zstdC, decompress: _zstdD },
    };
    const chainFns = {
        huffman: { compress: _hufC, decompress: _hufD },
        rle:     { compress: _rleC, decompress: _rleD  },
    };

    // ── Core WASM helpers ─────────────────────────────────────────────────────

    function readFileData(inputId) {
        return new Promise((resolve, reject) => {
            const file = document.getElementById(inputId).files[0];
            if (!file) return reject(new Error('no-file:' + inputId));
            const r = new FileReader();
            r.onload  = e => resolve(new Uint8Array(e.target.result));
            r.onerror = () => reject(new Error('File read error'));
            r.readAsArrayBuffer(file);
        });
    }

    function wasmRun(fn, data) {
        // 8× headroom for decompression; ALLOW_MEMORY_GROWTH covers edge cases.
        const outSize = 516 + data.length * 8 + 64;
        const inPtr   = Module._malloc(data.length);
        const outPtr  = Module._malloc(outSize);
        if (!inPtr || !outPtr) throw new Error('WASM malloc failed — not enough memory');
        try {
            Module.HEAPU8.set(data, inPtr);
            const len = fn(inPtr, outPtr, data.length);
            if (len === 0) throw new Error('WASM returned 0 bytes — input may be invalid or corrupt');
            return Module.HEAPU8.slice(outPtr, outPtr + len);
        } finally {
            Module._free(inPtr);
            Module._free(outPtr);
        }
    }

    function wasmRunRef(fn, src, ref) {
        const outSize = src.length + ref.length + 16;
        const srcPtr  = Module._malloc(src.length);
        const refPtr  = Module._malloc(ref.length);
        const outPtr  = Module._malloc(outSize);
        if (!srcPtr || !refPtr || !outPtr) throw new Error('WASM malloc failed');
        try {
            Module.HEAPU8.set(src, srcPtr);
            Module.HEAPU8.set(ref, refPtr);
            const len = fn(srcPtr, refPtr, outPtr, src.length, ref.length);
            if (len === 0) throw new Error('WASM returned 0 bytes');
            return Module.HEAPU8.slice(outPtr, outPtr + len);
        } finally {
            Module._free(srcPtr);
            Module._free(refPtr);
            Module._free(outPtr);
        }
    }

    // ── Real function implementations (replace stubs) ─────────────────────────

    compressFile = function() {
        readFileData('fileInput').then(data => {
            const t0  = performance.now();
            const out = wasmRun(fns[currentAlgo].compress, data);
            const ms  = performance.now() - t0;
            const label = currentAlgo === 'zstd' ? 'LZ77 → Huffman' : currentAlgo.toUpperCase();
            showResult(data, out, ['input', label, 'output'], false, ms);
        }).catch(err => {
            if (err.message.startsWith('no-file')) showError('Select a file first.');
            else { console.error(err); showError('Compression failed: ' + err.message); }
        });
    };

    decompressFile = function() {
        readFileData('fileInput').then(data => {
            const t0  = performance.now();
            const out = wasmRun(fns[currentAlgo].decompress, data);
            const ms  = performance.now() - t0;
            const label = currentAlgo === 'zstd'
                ? 'Huffman → LZ77 decode'
                : currentAlgo.toUpperCase() + ' decompress';
            showResult(data, out, ['input', label, 'output'], true, ms);
        }).catch(err => {
            if (err.message.startsWith('no-file')) showError('Select a file first.');
            else { console.error(err); showError('Decompression failed: ' + err.message); }
        });
    };

    deltaCompress = function() {
        const chain     = document.getElementById('chainCheck').checked;
        const chainAlgo = document.getElementById('chainAlgo').value;

        readFileData('deltaFileInput').then(async srcData => {
            let deltaOut;
            let pipeline = ['input'];
            const t0 = performance.now();

            if (deltaMode === 'ref') {
                let refData;
                try { refData = await readFileData('refFileInput'); }
                catch { return showError('Select a reference file first.'); }
                deltaOut = wasmRunRef(_dRefC, srcData, refData);
                pipeline.push('Δ ref-delta');
            } else {
                deltaOut = wasmRun(_dC, srcData);
                pipeline.push('Δ self-delta');
            }

            let finalOut = deltaOut;
            if (chain) {
                finalOut = wasmRun(chainFns[chainAlgo].compress, deltaOut);
                pipeline.push(chainAlgo.toUpperCase());
            }
            const ms = performance.now() - t0;
            pipeline.push('output');
            showResult(srcData, finalOut, pipeline, false, ms);
        }).catch(err => {
            if (err.message.startsWith('no-file')) showError('Select a target file first.');
            else { console.error(err); showError('Delta compression failed: ' + err.message); }
        });
    };

    deltaDecompress = function() {
        const chain     = document.getElementById('chainCheck').checked;
        const chainAlgo = document.getElementById('chainAlgo').value;

        readFileData('deltaFileInput').then(async srcData => {
            let pipeline  = ['input'];
            let unChained = srcData;
            const t0 = performance.now();

            if (chain) {
                unChained = wasmRun(chainFns[chainAlgo].decompress, srcData);
                pipeline.push(chainAlgo.toUpperCase() + ' decompress');
            }

            let finalOut;
            if (deltaMode === 'ref') {
                let refData;
                try { refData = await readFileData('refFileInput'); }
                catch { return showError('Select a reference file first.'); }
                finalOut = wasmRunRef(_dRefD, unChained, refData);
                pipeline.push('Δ ref-delta decompress');
            } else {
                finalOut = wasmRun(_dD, unChained);
                pipeline.push('Δ self-delta decompress');
            }
            const ms = performance.now() - t0;
            pipeline.push('output');
            showResult(srcData, finalOut, pipeline, true, ms);
        }).catch(err => {
            if (err.message.startsWith('no-file')) showError('Select a file first.');
            else { console.error(err); showError('Delta decompression failed: ' + err.message); }
        });
    };
}

// ── Bootstrap ─────────────────────────────────────────────────────────────────
// Handle both cases: WASM already ready, or callback-based init.

if (typeof Module !== 'undefined' && Module.calledRun) {
    initModule();
} else {
    // Module may not exist yet if compressor.js hasn't started loading
    if (typeof Module === 'undefined') {
        window.Module = {};
    }
    Module.onRuntimeInitialized = initModule;
}