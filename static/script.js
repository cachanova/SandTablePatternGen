let currentThrContent = "";
let currentPngUrl = "";
let currentThumbUrl = "";
let abortController = null;
let currentThrFile = null;
let uploadReceipt = null;
let selectionRevision = 0;

document.addEventListener('DOMContentLoaded', () => {
    const dropZone = document.getElementById('dropZone');
    const imageInput = document.getElementById('imageInput');
    const imagePreview = document.getElementById('imagePreview');
    const uploadPlaceholder = document.querySelector('#dropZone .upload-placeholder');

    // Sync range and number inputs
    syncInputs('low', 'lowNum');
    syncInputs('high', 'highNum');
    syncInputs('blur', 'blurNum');

    // Image Drag and Drop
    dropZone.addEventListener('click', () => {
        console.log("Dropzone clicked");
        imageInput.click();
    });

    dropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropZone.classList.add('dragover');
    });

    dropZone.addEventListener('dragleave', () => {
        dropZone.classList.remove('dragover');
    });

    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        if (e.dataTransfer.files.length) {
            handleImageFile(e.dataTransfer.files[0]);
        }
    });

    imageInput.addEventListener('change', () => {
        if (imageInput.files.length) {
            handleImageFile(imageInput.files[0]);
        }
    });

    function handleImageFile(file) {
        if (abortController) return;
        if (!file.type.startsWith('image/')) {
            alert("Please upload an image file");
            return;
        }

        if (file !== imageInput.files[0]) {
            const dataTransfer = new DataTransfer();
            dataTransfer.items.add(file);
            imageInput.files = dataTransfer.files;
        }

        clearGeneratedResult();

        // Set default pattern name from filename
        const originalName = file.name;
        const baseName = originalName.substring(0, originalName.lastIndexOf('.')) || originalName;
        document.getElementById('patternName').value = baseName;

        const revision = selectionRevision;
        const reader = new FileReader();
        reader.onload = (e) => {
            if (revision !== selectionRevision) return;
            imagePreview.src = e.target.result;
            imagePreview.classList.remove('hidden');
            uploadPlaceholder.classList.add('hidden');
        };
        reader.readAsDataURL(file);
        setNote("sourceNote", file.name);
    }

    const tableIp = document.getElementById('tableIp');
    const syncTableLink = () => {
        try { document.getElementById('tableLink').href = ThrGenTransfer.tableOrigin(tableIp.value.trim()); }
        catch (_) { document.getElementById('tableLink').removeAttribute('href'); }
    };
    tableIp.addEventListener('change', syncTableLink);
    syncTableLink();

    // THR File Drag and Drop
    const thrDropZone = document.getElementById('thrDropZone');
    const thrInput = document.getElementById('thrInput');
    const thrPlaceholder = document.getElementById('thrPlaceholder');
    const thrFileInfo = document.getElementById('thrFileInfo');
    const thrFileName = document.getElementById('thrFileName');
    const processThrBtn = document.getElementById('processThrBtn');

    thrDropZone.addEventListener('click', () => {
        thrInput.click();
    });

    thrDropZone.addEventListener('dragover', (e) => {
        e.preventDefault();
        thrDropZone.classList.add('dragover');
    });

    thrDropZone.addEventListener('dragleave', () => {
        thrDropZone.classList.remove('dragover');
    });

    thrDropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        thrDropZone.classList.remove('dragover');
        if (e.dataTransfer.files.length) {
            handleThrFile(e.dataTransfer.files[0]);
        }
    });

    thrInput.addEventListener('change', () => {
        if (thrInput.files.length) {
            handleThrFile(thrInput.files[0]);
        }
    });

    function handleThrFile(file) {
        if (abortController) return;
        if (!file.name.toLowerCase().endsWith('.thr')) {
            alert("Please upload a .thr file");
            return;
        }

        currentThrFile = file;
        clearGeneratedResult();

        // Set pattern name from filename
        const baseName = file.name.substring(0, file.name.lastIndexOf('.')) || file.name;
        document.getElementById('patternName').value = baseName;

        // Update UI
        thrFileName.textContent = file.name;
        thrPlaceholder.classList.add('hidden');
        thrFileInfo.classList.remove('hidden');
        processThrBtn.disabled = false;
        setNote("sourceNote", file.name);
    }
});

function setNote(id, text) { document.getElementById(id).textContent = text; }

function switchTab(tab) {
    if (abortController) return;
    clearGeneratedResult();
    const imageTab = document.getElementById('imageTab');
    const thrTab = document.getElementById('thrTab');
    const paramsCard = document.querySelector('.params-card');
    const tabBtns = document.querySelectorAll('.tab-btn');

    document.getElementById('processBtn').classList.toggle('hidden', tab !== 'image');
    document.getElementById('processThrBtn').classList.toggle('hidden', tab === 'image');
    setNote('sourceNote', (tab === 'image' ? document.getElementById('imageInput').files[0] : currentThrFile)?.name || 'awaiting input');
    tabBtns.forEach(btn => btn.classList.remove('active'));

    if (tab === 'image') {
        imageTab.classList.remove('hidden');
        thrTab.classList.add('hidden');
        paramsCard.classList.remove('hidden');
        tabBtns[0].classList.add('active');
    } else {
        imageTab.classList.add('hidden');
        thrTab.classList.remove('hidden');
        paramsCard.classList.add('hidden');
        tabBtns[1].classList.add('active');
    }
}

function syncInputs(rangeId, numId) {
    const range = document.getElementById(rangeId);
    const num = document.getElementById(numId);
    range.addEventListener('input', () => num.value = range.value);
    num.addEventListener('input', () => range.value = num.value);
}

function resetParam(type) {
    const defaults = { low: 50, high: 150, blur: 5 };
    const range = document.getElementById(type);
    const num = document.getElementById(type + 'Num');
    range.value = defaults[type];
    num.value = defaults[type];
}

function updateStatus(message, details = "") {
    document.getElementById('statusMessage').textContent = message;
    document.getElementById('statusDetails').textContent = details;
}

function clearGeneratedResult() {
    ++selectionRevision;
    stopProcessing();
    uploadReceipt = null;
    currentThrContent = "";
    currentPngUrl = "";
    currentThumbUrl = "";
    document.getElementById('downloadBtn').disabled = true;
    document.getElementById('uploadBtn').disabled = true;
    for (const id of ['edgesCanvas', 'pathCanvas']) {
        const canvas = document.getElementById(id);
        canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
    }
    const preview = document.getElementById('gifOutput');
    preview.classList.add('hidden');
    preview.removeAttribute('src');
    document.getElementById('statsArea').classList.add('hidden');
    document.getElementById('uploadProgressArea').classList.add('hidden');
    setNote('edgesNote', 'canny edge detection');
    setNote('pathNote', 'polar path preview');
    setNote('tableNote', 'preview');
}

function stopProcessing() {
    abortController?.abort();
}

function setBusy(busy) {
    document.querySelectorAll('#imageInput, #thrInput, #patternName, #tableIp, .tab-btn, #low, #lowNum, #high, #highNum, #blur, #blurNum, #includeAnimation, .reset-btn').forEach(node => node.disabled = busy);
    document.getElementById('downloadBtn').disabled = busy || !currentThrContent;
    document.getElementById('processBtn').disabled = busy;
    document.getElementById('processThrBtn').disabled = busy || !currentThrFile;
    document.getElementById('uploadBtn').disabled = busy || !currentThrContent;
    document.getElementById('stopBtn').classList.toggle('hidden', !busy);
    if (!busy) {
        document.getElementById('loadingSpinner').classList.add('hidden');
        document.getElementById('progressContainer').classList.add('hidden');
    }
}

function processImage() {
    const file = document.getElementById('imageInput').files[0];
    if (!file) { updateStatus('Select an image first'); return; }
    const body = new FormData();
    body.append('image', file);
    body.append('low_threshold', document.getElementById('low').value);
    body.append('high_threshold', document.getElementById('high').value);
    body.append('blur', document.getElementById('blur').value);
    return processFile('/process', body);
}

function processThr() {
    if (!currentThrFile) { updateStatus('Select a .thr file first'); return; }
    const body = new FormData();
    body.append('thr', currentThrFile);
    return processFile('/process_thr', body);
}

async function processFile(url, body) {
    if (abortController) return;
    clearGeneratedResult();
    const controller = new AbortController();
    abortController = controller;
    const {signal} = controller;
    setBusy(true);
    document.getElementById('uploadProgressArea').classList.add('hidden');
    document.getElementById('gifOutput').classList.add('hidden');
    document.getElementById('statsArea').classList.add('hidden');
    document.getElementById('progressContainer').classList.remove('hidden');
    document.getElementById('progressBar').style.width = '0%';
    body.append('animation', document.getElementById('includeAnimation').checked ? '1' : '0');
    updateStatus('Sending source…');
    try {
        const data = await ThrGenTransfer.request(url, {body, signal, timeout: 300000, label: 'Generate pattern',
            onProgress: fraction => {
                document.getElementById('progressBar').style.width = `${fraction * 100}%`;
                if (fraction >= 1) {
                    updateStatus('Generating pattern…');
                    document.getElementById('loadingSpinner').classList.remove('hidden');
                }
            }});
        if (signal.aborted) return;
        if (!data.thr || !data.png_url || !Number.isInteger(data.point_count) || !data.width || !data.height) {
            throw new Error('Incomplete generation response');
        }
        updateStatus('Drawing preview…');
        const size = Math.max(data.width, data.height);
        await drawEdges(data.edges || [], data.width, data.height, size, signal);
        await drawPreviewImage(data.png_url, signal);
        if (signal.aborted) return;
        currentThrContent = data.thr;
        currentPngUrl = data.png_url || '';
        currentThumbUrl = data.thumb_url || '';
        const preview = document.getElementById('gifOutput');
        preview.src = data.gif_url || data.png_url;
        preview.classList.remove('hidden');
        document.getElementById('pointCount').textContent = data.point_count;
        setNote('edgesNote', data.edges?.length ? `${data.edges.length.toLocaleString()} edge px` : 'imported .thr');
        setNote('pathNote', `${data.point_count.toLocaleString()} points · polar path`);
        setNote('tableNote', data.gif_url ? 'animation' : 'static preview');
        document.getElementById('statsArea').classList.remove('hidden');
        document.getElementById('downloadBtn').disabled = false;
        updateStatus('Ready to upload', `${data.point_count} points. ${data.gif_url ? 'Animation included.' : 'Static preview ready.'}`);
    } catch (error) {
        updateStatus(error.name === 'AbortError' ? 'Cancelled' : 'Generation failed', error.name === 'AbortError' ? '' : error.message);
    } finally {
        if (signal.aborted) updateStatus('Cancelled');
        abortController = null;
        setBusy(false);
    }
}

// Yield between batches so large previews do not freeze controls or cancellation.
const yieldToBrowser = () => new Promise(resolve => setTimeout(resolve, 0));

function resizeCanvas(canvas, size) {
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, size, size);
    return ctx;
}

async function drawEdges(points, imgW, imgH, size, signal) {
    const canvas = document.getElementById('edgesCanvas');
    const ctx = resizeCanvas(canvas, size);
    if (points.length === 0) return;
    const ox = (size - imgW) / 2;
    const oy = (size - imgH) / 2;
    ctx.fillStyle = '#1a1917';
    for (let i = 0; i < points.length; i++) {
        if (i % 2048 === 0) { await yieldToBrowser(); if (signal.aborted) return; }
        const p = points[i];
        ctx.fillRect(p[0] + ox, p[1] + oy, 1, 1);
    }
}

async function drawPreviewImage(url, signal) {
    const blob = await ThrGenTransfer.request(url, {signal, responseType: 'blob', timeout: 30000, label: 'Load preview'});
    const bitmap = await createImageBitmap(blob);
    try {
        if (signal.aborted) return;
        const canvas = document.getElementById('pathCanvas');
        const ctx = resizeCanvas(canvas, 800);
        // Tint the white transparent table overlay for the light path panel.
        ctx.clearRect(0, 0, 800, 800);
        ctx.drawImage(bitmap, 0, 0, 800, 800);
        ctx.globalCompositeOperation = 'source-in';
        ctx.fillStyle = '#1a1917';
        ctx.fillRect(0, 0, 800, 800);
        ctx.globalCompositeOperation = 'destination-over';
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, 800, 800);
        ctx.globalCompositeOperation = 'source-over';
    } finally { bitmap.close(); }
}

function downloadThr() {
    if (!currentThrContent) return;
    const patternName = document.getElementById('patternName').value.trim() || 'track';
    const blob = new Blob([currentThrContent], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = patternName + '.thr';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

async function uploadToTable() {
    if (!currentThrContent || abortController) return;
    let origin, name;
    try {
        origin = ThrGenTransfer.tableOrigin(document.getElementById('tableIp').value.trim());
        name = ThrGenTransfer.patternBase(document.getElementById('patternName').value);
    } catch (error) { updateStatus('Check upload settings', error.message); return; }
    if (!uploadReceipt || uploadReceipt.origin !== origin || uploadReceipt.name !== name) {
        uploadReceipt = {origin, name, completed: new Set()};
    }
    const receipt = uploadReceipt;
    const result = {thr: currentThrContent, png: currentPngUrl, thumb: currentThumbUrl};
    const controller = new AbortController();
    abortController = controller;
    setBusy(true);
    document.getElementById('uploadProgressArea').classList.remove('hidden');
    updateStatus('Uploading…', `Sending ${name} to ${origin}`);
    try {
        const failures = await ThrGenTransfer.upload({origin, name, result,
            completed: receipt.completed, signal: controller.signal,
            onState: (id, state, fraction) => {
                document.getElementById(`${id}ProgressBar`).style.width = `${fraction * 100}%`;
                document.getElementById(`${id}UploadState`).textContent = state;
            }});
        updateStatus(failures.length ? 'Pattern saved; previews incomplete' : 'Upload complete',
            failures.length ? `${failures.join(' ')} Click Upload to retry failed files.` : `${name} is saved on the table.`);
    } catch (error) {
        const saved = receipt.completed.has('thr') ? 'Pattern saved. ' : '';
        updateStatus(error.name === 'AbortError' ? 'Upload cancelled' : 'Upload failed',
            `${saved}${error.name === 'AbortError' ? 'An in-flight file may already be saved. Retry to finish.' : error.message}`);
    } finally {
        controller.abort(); // Release any preview fetches still in flight.
        abortController = null;
        setBusy(false);
    }
}
