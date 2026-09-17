/* Shared, bounded transport for processing and table uploads. */
(function (root) {
    'use strict';
    function request(url, {body, signal, timeout = 60000, stallTimeout = 0, responseType = 'json', onProgress, label = 'Request'} = {}) {
        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            let settled = false, stallTimer;
            const finish = (error, value) => {
                if (settled) return;
                settled = true;
                clearTimeout(stallTimer);
                signal?.removeEventListener('abort', abort);
                error ? reject(error) : resolve(value);
            };
            const abort = () => {
                xhr.abort();
                finish(new DOMException('Request cancelled', 'AbortError'));
            };
            const resetStallTimer = () => {
                if (!stallTimeout || settled) return;
                clearTimeout(stallTimer);
                stallTimer = setTimeout(() => {
                    finish(new Error(`${label}: no progress for ${stallTimeout / 1000} seconds. The server may have accepted the request; its outcome is unconfirmed.`));
                    xhr.abort();
                }, stallTimeout);
            };
            if (signal?.aborted) { abort(); return; }
            xhr.open(body === undefined ? 'GET' : 'POST', url);
            xhr.timeout = timeout;
            xhr.responseType = responseType === 'json' ? 'text' : responseType;
            xhr.upload.onprogress = event => {
                resetStallTimer();
                if (event.lengthComputable) onProgress?.(event.loaded / event.total);
            };
            xhr.upload.onload = resetStallTimer;
            xhr.onprogress = resetStallTimer;
            xhr.onload = async () => {
                try {
                    let value = xhr.response;
                    if (responseType === 'json') {
                        try { value = JSON.parse(xhr.responseText); }
                        catch (_) {
                            throw new Error(xhr.status >= 200 && xhr.status < 300 ?
                                `${label}: invalid JSON response` : `${label}: ${xhr.responseText || `HTTP ${xhr.status}`}`);
                        }
                    }
                    if (xhr.status < 200 || xhr.status >= 300) {
                        const message = value?.message || (value instanceof Blob ? await value.text() : value);
                        throw new Error(`${label}: ${message || `HTTP ${xhr.status}`}`);
                    }
                    if (value === null || value?.success === false) {
                        throw new Error(`${label}: ${value?.message || 'Invalid server response'}`);
                    }
                    finish(null, value);
                } catch (error) { finish(error); }
            };
            xhr.onerror = () => finish(new Error(`${label}: cannot reach server. Check the address and connection.`));
            xhr.ontimeout = () => finish(new Error(`${label}: timed out.${body === undefined ? '' : ' The server may have accepted the request; its outcome is unconfirmed.'}`));
            xhr.onabort = () => finish(new DOMException('Request cancelled', 'AbortError'));
            signal?.addEventListener('abort', abort, {once: true});
            resetStallTimer();
            try { xhr.send(body); } catch (error) { finish(error); }
        });
    }

    function tableOrigin(address) {
        if (!address.trim()) throw new Error('Enter the table address.');
        const url = new URL(/^https?:\/\//i.test(address) ? address : `http://${address}`);
        if (!['http:', 'https:'].includes(url.protocol) || !url.hostname || url.username || url.password ||
            url.pathname !== '/' || url.search || url.hash) {
            throw new Error('Enter a table hostname or HTTP(S) address without a path.');
        }
        return url.origin;
    }

    function patternBase(name) {
        const base = name.trim() || 'track';
        // The firmware allows 80 ASCII characters including the four-character extension.
        if (base.length > 76 || !/^[a-zA-Z0-9_. -]+$/.test(base) || base.includes('..')) {
            throw new Error('Pattern names allow up to 76 letters, numbers, spaces, dots, hyphens or underscores; no consecutive dots.');
        }
        return base;
    }

    async function upload({origin, name, result, completed, signal, onState}) {
        const files = [
            {id: 'thr', label: 'Pattern', limit: 8 * 1024 * 1024, blob: new Blob([result.thr], {type: 'text/plain'})},
            {id: 'png', label: 'Preview', limit: 2 * 1024 * 1024, url: result.png},
            {id: 'thumb', label: 'Thumbnail', limit: 128 * 1024, url: result.thumb}
        ];
        if (!files[0].blob.size || files[0].blob.size > files[0].limit) {
            throw new Error('The pattern must be nonempty and no larger than 8 MiB.');
        }
        // A THR replacement invalidates both images; a PNG replacement
        // invalidates its thumbnail, even if an acknowledgement is lost.
        if (!completed.has('thr')) { completed.delete('png'); completed.delete('thumb'); }
        if (!completed.has('png')) completed.delete('thumb');
        // Fetch assets from ThrGenCpp while the pattern transfers. Always handle
        // rejections immediately, even when the pattern fails first.
        for (const file of files) {
            onState(file.id, completed.has(file.id) ? 'Saved' : 'Waiting', completed.has(file.id) ? 1 : 0);
            if (file.url && !completed.has(file.id)) {
                file.download = request(file.url, {signal, responseType: 'blob', timeout: 30000, label: `Download ${file.label.toLowerCase()}`})
                    .then(blob => ({blob}), error => ({error}));
            }
        }
        const failures = [];
        for (const file of files) {
            if (signal.aborted) throw new DOMException('Request cancelled', 'AbortError');
            if (completed.has(file.id)) continue;
            if (file.id === 'thumb' && !completed.has('png')) {
                onState(file.id, 'Waiting for preview', 0);
                if (file.url) failures.push('Thumbnail: waiting for preview to be saved.');
                continue;
            }
            if (!file.blob && !file.url) { onState(file.id, 'Not included', 0); continue; }
            try {
                if (file.download) {
                    onState(file.id, 'Preparing', 0);
                    const downloaded = await file.download;
                    if (downloaded.error) throw downloaded.error;
                    file.blob = downloaded.blob;
                }
                if (!file.blob.size || file.blob.size > file.limit) throw new Error(`${file.label} is empty or exceeds the table's size limit.`);
                if (file.id !== 'thr') {
                    const signature = new Uint8Array(await file.blob.slice(0, 8).arrayBuffer());
                    if ([137,80,78,71,13,10,26,10].some((byte, i) => signature[i] !== byte))
                        throw new Error(`${file.label} is not a PNG image.`);
                }
                const body = new FormData();
                body.append('file', file.blob, `${name}.${file.id === 'thr' ? 'thr' : 'png'}`);
                const query = file.id === 'thumb' ? '?thumbnail=1' : '';
                onState(file.id, 'Sending', 0);
                const acknowledgement = await request(`${origin}/api/files/upload${query}`, {
                    body, signal, timeout: 0, stallTimeout: 30000, label: `Save ${file.label.toLowerCase()}`,
                    onProgress: fraction => onState(file.id, fraction >= 1 ? 'Saving on table' : 'Sending', fraction)
                });
                if (acknowledgement.success !== true) throw new Error('Table did not acknowledge a saved file.');
                completed.add(file.id);
                onState(file.id, 'Saved', 1);
            } catch (error) {
                onState(file.id, error.name === 'AbortError' ? 'Cancelled' : 'Failed', 0);
                if (file.id === 'thr' || error.name === 'AbortError') throw error;
                failures.push(`${file.label}: ${error.message}`);
            }
        }
        return failures;
    }
    root.ThrGenTransfer = {request, tableOrigin, patternBase, upload};
    if (typeof module !== 'undefined') module.exports = root.ThrGenTransfer;
})(globalThis);
