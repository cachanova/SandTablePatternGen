let currentThrContent = "";
let currentPngUrl = "";
let currentThumbUrl = "";
let abortController = null;
let currentThrFile = null;
let processing = false;

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

        const reader = new FileReader();
        reader.onload = (e) => {
            imagePreview.src = e.target.result;
            imagePreview.classList.remove('hidden');
            uploadPlaceholder.classList.add('hidden');
        };
        reader.readAsDataURL(file);
        setNote('sourceNote', file.name);
    }

    // Keep the "Table" nav link pointed at the configured table address
    const tableIpInput = document.getElementById('tableIp');
    const tableLink = document.getElementById('tableLink');
    if (tableIpInput && tableLink) {
        const syncTableLink = () => {
            const base = normalizeTableAddress(tableIpInput.value);
            if (base) tableLink.href = base;
        };
        tableIpInput.addEventListener('change', syncTableLink);
        syncTableLink();
    }

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
        setNote('sourceNote', file.name);
    }
});

function setNote(id, text) {
    const el = document.getElementById(id);
    if (el) el.textContent = text;
}

function normalizeTableAddress(value) {
    const trimmed = (value || '').trim();
    if (!trimmed) return null;
    try {
        const address = /^https?:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
        const parsed = new URL(address);
        if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return null;
        return parsed.origin;
    } catch (_) {
        return null;
    }
}

function switchTab(tab) {
    const imageTab = document.getElementById('imageTab');
    const thrTab = document.getElementById('thrTab');
    const paramsCard = document.querySelector('.params-card');
    const processBtn = document.getElementById('processBtn');
    const processThrBtn = document.getElementById('processThrBtn');
    const tabBtns = document.querySelectorAll('.tab-btn');

    tabBtns.forEach(btn => btn.classList.remove('active'));

    if (tab === 'image') {
        imageTab.classList.remove('hidden');
        thrTab.classList.add('hidden');
        paramsCard.classList.remove('hidden');
        processBtn.classList.remove('hidden');
        processThrBtn.classList.add('hidden');
        tabBtns[0].classList.add('active');
    } else {
        imageTab.classList.add('hidden');
        thrTab.classList.remove('hidden');
        paramsCard.classList.add('hidden');
        processBtn.classList.add('hidden');
        processThrBtn.classList.remove('hidden');
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
    currentThrContent = "";
    currentPngUrl = "";
    currentThumbUrl = "";
    document.getElementById('downloadBtn').disabled = true;
    document.getElementById('uploadBtn').disabled = true;
    // A new source invalidates the previous run: never show its stages next to it.
    for (const id of ['edgesCanvas', 'pathCanvas']) {
        const canvas = document.getElementById(id);
        canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
    }
    const gifOutput = document.getElementById('gifOutput');
    gifOutput.classList.add('hidden');
    gifOutput.removeAttribute('src');
    document.getElementById('statsArea').classList.add('hidden');
    setNote('edgesNote', 'canny edge detection');
    setNote('pathNote', 'traversal order · start → end');
}

function stopProcessing() {
    if (abortController) {
        abortController.abort();
        abortController = null;
        updateStatus("Cancelled", "User stopped the process.");
        cleanupUI();
    }
}

function cleanupUI() {
    document.getElementById('loadingSpinner').classList.add('hidden');
    document.getElementById('progressContainer').classList.add('hidden');
    document.getElementById('progressBar').style.width = '0%';
    document.getElementById('stopBtn').classList.add('hidden');
    document.getElementById('processBtn').disabled = false;
    document.getElementById('processThrBtn').disabled = !currentThrFile;
}

async function processImage() {
    console.log("processImage called");
    if (processing) return;
    const input = document.getElementById('imageInput');
    if (!input.files[0]) {
        alert("Please select an image first");
        return;
    }
    processing = true;

    const processBtn = document.getElementById('processBtn');
    const gifOutput = document.getElementById('gifOutput');
    const stopBtn = document.getElementById('stopBtn');
    const spinner = document.getElementById('loadingSpinner');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');
    
    // UI Reset
    processBtn.disabled = true;
    stopBtn.classList.remove('hidden');
    progressContainer.classList.remove('hidden');
    gifOutput.classList.add('hidden');
    
    updateStatus("Uploading...", "Sending image to server...");

    // Setup AbortController
    abortController = new AbortController();
    const signal = abortController.signal;

    const formData = new FormData();
    formData.append('image', input.files[0]);
    formData.append('low_threshold', document.getElementById('low').value);
    formData.append('high_threshold', document.getElementById('high').value);
    formData.append('blur', document.getElementById('blur').value);

    try {
        const data = await new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/process');
            
            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const percent = (e.loaded / e.total) * 100;
                    progressBar.style.width = percent + '%';
                    if (percent >= 100) {
                        updateStatus("Processing...", "Server is analyzing image and generating path...");
                        spinner.classList.remove('hidden');
                        progressContainer.classList.add('hidden');
                    }
                }
            };

            xhr.onload = () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        resolve(JSON.parse(xhr.responseText));
                    } catch(e) {
                        reject(new Error("Invalid server response"));
                    }
                } else {
                    reject(new Error(xhr.responseText || `Server returned ${xhr.status}`));
                }
            };

            xhr.onerror = () => reject(new Error("Network error"));
            xhr.onabort = () => reject(new DOMException("Request cancelled", "AbortError"));

            signal.addEventListener('abort', () => xhr.abort());

            xhr.send(formData);
        });

        updateStatus("Finalizing...", "Generating visualization and animation...");
        
        currentThrContent = data.thr;
        currentPngUrl = data.png_url;
        currentThumbUrl = data.thumb_url || "";
        
        // Draw views
        const size = Math.max(data.width, data.height);
        drawEdges(data.edges || [], data.width, data.height, size);
        drawPath(data.preview, data.width, data.height, size);
        
        // Set GIF
        gifOutput.src = data.gif_url;
        gifOutput.classList.remove('hidden');
        
        // Update stats
        document.getElementById('pointCount').textContent = data.preview.length;
        document.getElementById('statsArea').classList.remove('hidden');
        setNote('sourceNote', `${input.files[0].name} · ${data.width}×${data.height}`);
        setNote('edgesNote', `${(data.edges || []).length.toLocaleString()} edge px · canny ${document.getElementById('low').value}/${document.getElementById('high').value}`);
        setNote('pathNote', `${data.preview.length.toLocaleString()} pts · start → end`);

        const downloadBtn = document.getElementById('downloadBtn');
        downloadBtn.disabled = false;
        const uploadBtn = document.getElementById('uploadBtn');
        uploadBtn.disabled = false;

        updateStatus("Success!", `Processed at ${data.width}x${data.height}. Generated ${data.preview.length} points.`);

    } catch (e) {
        if (e.name === 'AbortError') {
            // Handled in stopProcessing
        } else {
            updateStatus("Error", e.message);
            alert("Error: " + e.message);
        }
    } finally {
        processing = false;
        if (!signal.aborted) {
            cleanupUI();
        }
        abortController = null;
    }
}

async function processThr() {
    if (processing) return;
    if (!currentThrFile) {
        alert("Please select a .thr file first");
        return;
    }
    processing = true;

    const processThrBtn = document.getElementById('processThrBtn');
    const gifOutput = document.getElementById('gifOutput');
    const stopBtn = document.getElementById('stopBtn');
    const spinner = document.getElementById('loadingSpinner');
    const progressContainer = document.getElementById('progressContainer');
    const progressBar = document.getElementById('progressBar');

    // UI Reset
    processThrBtn.disabled = true;
    stopBtn.classList.remove('hidden');
    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
    gifOutput.classList.add('hidden');

    updateStatus("Uploading...", "Sending .thr file to server...");

    // Setup AbortController
    abortController = new AbortController();
    const signal = abortController.signal;

    const formData = new FormData();
    formData.append('thr', currentThrFile);

    try {
        const data = await new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/process_thr');

            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const percent = (e.loaded / e.total) * 100;
                    progressBar.style.width = percent + '%';
                    if (percent >= 100) {
                        updateStatus("Processing...", "Generating visualization...");
                        spinner.classList.remove('hidden');
                        progressContainer.classList.add('hidden');
                    }
                }
            };

            xhr.onload = () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        resolve(JSON.parse(xhr.responseText));
                    } catch(e) {
                        reject(new Error("Invalid server response"));
                    }
                } else {
                    reject(new Error(xhr.responseText || `Server returned ${xhr.status}`));
                }
            };

            xhr.onerror = () => reject(new Error("Network error"));
            xhr.onabort = () => reject(new DOMException("Request cancelled", "AbortError"));

            signal.addEventListener('abort', () => xhr.abort());

            xhr.send(formData);
        });

        updateStatus("Finalizing...", "Generating visualization and animation...");

        currentThrContent = data.thr;
        currentPngUrl = data.png_url;
        currentThumbUrl = data.thumb_url || "";

        // Draw views
        const size = Math.max(data.width, data.height);
        drawEdges(data.edges || [], data.width, data.height, size);
        drawPath(data.preview, data.width, data.height, size);

        // Set GIF
        gifOutput.src = data.gif_url;
        gifOutput.classList.remove('hidden');

        // Update stats
        document.getElementById('pointCount').textContent = data.preview.length;
        document.getElementById('statsArea').classList.remove('hidden');
        const thrEdges = (data.edges || []).length;
        setNote('edgesNote', thrEdges ? `${thrEdges.toLocaleString()} edge px` : 'not applicable — imported .thr');
        setNote('pathNote', `${data.preview.length.toLocaleString()} pts · start → end`);

        const downloadBtn = document.getElementById('downloadBtn');
        downloadBtn.disabled = false;
        const uploadBtn = document.getElementById('uploadBtn');
        uploadBtn.disabled = false;

        updateStatus("Success!", `Generated preview with ${data.preview.length} points.`);

    } catch (e) {
        if (e.name === 'AbortError') {
            // Handled in stopProcessing
        } else {
            updateStatus("Error", e.message);
            alert("Error: " + e.message);
        }
    } finally {
        processing = false;
        if (!signal.aborted) {
            cleanupUI();
        }
        abortController = null;
    }
}

function resizeCanvas(canvas, size) {
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, size, size);
    return ctx;
}

function drawEdges(points, imgW, imgH, size) {
    const canvas = document.getElementById('edgesCanvas');
    const ctx = resizeCanvas(canvas, size);
    if (points.length === 0) return;
    const ox = (size - imgW) / 2;
    const oy = (size - imgH) / 2;
    ctx.fillStyle = '#1a1917';
    for (const p of points) {
        ctx.fillRect(p[0] + ox, p[1] + oy, 1, 1);
    }
}

// Path traversal order runs from muted blue to muted red (firmware palette)
const PATH_START_COLOR = [44, 79, 158];
const PATH_END_COLOR = [158, 58, 46];

function pathColor(f) {
    const c = PATH_START_COLOR.map((s, i) => Math.round(s + f * (PATH_END_COLOR[i] - s)));
    return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
}

function drawPath(points, imgW, imgH, size) {
    const canvas = document.getElementById('pathCanvas');
    const ctx = resizeCanvas(canvas, size);
    if (points.length === 0) return;
    const ox = (size - imgW) / 2;
    const oy = (size - imgH) / 2;
    ctx.lineWidth = Math.max(1, size / 300);
    ctx.lineCap = 'round';
    const n = points.length;
    // Last segment (i = n-2) must reach the full end color to match the end marker.
    const denom = Math.max(1, n - 2);
    for (let i = 0; i < n - 1; i++) {
        ctx.beginPath();
        ctx.moveTo(points[i][0] + ox, points[i][1] + oy);
        ctx.lineTo(points[i+1][0] + ox, points[i+1][1] + oy);
        ctx.strokeStyle = pathColor(i / denom);
        ctx.stroke();
    }
    // Start/End markers
    ctx.fillStyle = pathColor(0); ctx.beginPath();
    ctx.arc(points[0][0] + ox, points[0][1] + oy, ctx.lineWidth * 3, 0, 2 * Math.PI); ctx.fill();
    ctx.fillStyle = pathColor(1); ctx.beginPath();
    ctx.arc(points[n-1][0] + ox, points[n-1][1] + oy, ctx.lineWidth * 3, 0, 2 * Math.PI); ctx.fill();
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
    if (!currentThrContent) return;

    const tableIp = document.getElementById('tableIp').value.trim();
    if (!tableIp) {
        alert("Please enter the Table IP address");
        return;
    }

    const tableBase = normalizeTableAddress(tableIp);
    if (!tableBase) {
        alert("Please enter a valid table hostname or URL");
        return;
    }

    const patternName = document.getElementById('patternName').value.trim() || 'track';
    const uploadBtn = document.getElementById('uploadBtn');
    const uploadProgressArea = document.getElementById('uploadProgressArea');
    const thrProgressBar = document.getElementById('thrProgressBar');
    const pngProgressBar = document.getElementById('pngProgressBar');

    uploadBtn.disabled = true;
    uploadProgressArea.classList.remove('hidden');
    thrProgressBar.style.width = '0%';
    pngProgressBar.style.width = '0%';
    updateStatus("Uploading...", `Sending files to ${tableIp}...`);

    try {
        // 1. Upload THR
        updateStatus("Uploading...", `Uploading ${patternName}.thr...`);
        const thrFormData = new FormData();
        const thrBlob = new Blob([currentThrContent], { type: 'text/plain' });
        thrFormData.append('file', thrBlob, patternName + ".thr");

        await uploadWithProgress(
            `${tableBase}/api/files/upload`,
            thrFormData,
            thrProgressBar
        );

        // 2. Upload PNG preview (if available)
        if (currentPngUrl) {
            updateStatus("Uploading...", `Uploading ${patternName}.png preview...`);
            await uploadAssetToTable(currentPngUrl, patternName + ".png",
                `${tableBase}/api/files/upload`, pngProgressBar);
        } else {
            pngProgressBar.style.width = '100%';
        }

        // 3. Upload thumbnail (if available). The firmware stores thumbnails per
        // pattern: same <pattern>.png name, distinguished by the thumbnail=1 flag.
        if (currentThumbUrl) {
            updateStatus("Uploading...", "Uploading thumbnail...");
            await uploadAssetToTable(currentThumbUrl, patternName + ".png",
                `${tableBase}/api/files/upload?thumbnail=1`, null);
        }

        updateStatus("Upload Complete!", `Successfully uploaded files to table.`);
        alert(`Successfully uploaded pattern and preview to table!`);
    } catch (e) {
        console.error("Upload failed", e);
        updateStatus("Upload Failed", e.message);
        alert("Upload failed: " + e.message);
    } finally {
        uploadBtn.disabled = false;
        uploadProgressArea.classList.add('hidden');
        thrProgressBar.style.width = '0%';
        pngProgressBar.style.width = '0%';
    }
}

// Fetch a generated asset from our server and upload it to the table.
// Failures are tolerated: a missing preview must never fail an upload
// whose .thr already landed on the table.
async function uploadAssetToTable(assetUrl, uploadName, uploadUrl, progressBar) {
    try {
        const response = await fetch(assetUrl);
        if (!response.ok) throw new Error(`Could not retrieve ${uploadName} (${response.status})`);
        const blob = await response.blob();
        const formData = new FormData();
        formData.append('file', blob, uploadName);
        await uploadWithProgress(uploadUrl, formData, progressBar);
        return true;
    } catch (error) {
        console.warn(`${uploadName} upload failed, but the .thr was successful.`, error);
        return false;
    }
}

function uploadWithProgress(url, formData, progressBar) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('POST', url);

        // Abort only when nothing has moved for a while: a slow but progressing
        // upload (large file, weak table Wi-Fi) must be allowed to finish.
        let stallTimer;
        let stalled = false;
        const resetStallTimer = () => {
            clearTimeout(stallTimer);
            stallTimer = setTimeout(() => { stalled = true; xhr.abort(); }, 30000);
        };
        resetStallTimer();

        xhr.upload.onprogress = (e) => {
            resetStallTimer();
            if (e.lengthComputable && progressBar) {
                progressBar.style.width = (e.loaded / e.total) * 100 + '%';
            }
        };
        // The body is sent; give the table its own window to write and respond.
        xhr.upload.onload = () => resetStallTimer();

        xhr.onload = () => {
            clearTimeout(stallTimer);
            if (xhr.status >= 200 && xhr.status < 300) {
                if (progressBar) progressBar.style.width = '100%';
                resolve(xhr.responseText);
            } else {
                reject(new Error(`Upload failed: ${xhr.status}`));
            }
        };

        xhr.onerror = () => { clearTimeout(stallTimer); reject(new Error("Network error")); };
        xhr.onabort = () => {
            clearTimeout(stallTimer);
            reject(new Error(stalled ? "Upload stalled: no progress for 30 seconds" : "Upload cancelled"));
        };
        xhr.send(formData);
    });
}
