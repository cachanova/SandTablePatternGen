#!/usr/bin/env python3
"""Integration checks against an already-running ThrGenCpp server."""
import json
from pathlib import Path
import struct
import time
import urllib.error
import urllib.request

BASE = 'http://127.0.0.1:8080'


def process(route, name, data, fields):
    boundary = 'ThrGenIntegrationBoundary'
    body = b''
    for key, value in fields.items():
        body += f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode()
    body += (f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"; filename="source"\r\n'
             'Content-Type: application/octet-stream\r\n\r\n').encode() + data
    body += f'\r\n--{boundary}--\r\n'.encode()
    start = time.monotonic()
    request = urllib.request.Request(BASE+route, data=body, headers={'Content-Type': 'multipart/form-data; boundary='+boundary})
    try:
        response = urllib.request.urlopen(request, timeout=180)
    except urllib.error.HTTPError as e:
        response = e
    with response:
        return response.status, response.read(), time.monotonic()-start


def assets(result):
    for key, size in [('png_url', 800), ('thumb_url', 128)]:
        data = urllib.request.urlopen(BASE+result[key], timeout=30).read()
        assert data[:8] == b'\x89PNG\r\n\x1a\n'
        assert struct.unpack('>II', data[16:24]) == (size, size)
    assert result['point_count'] > 0 and result['preview'] == []


def main():
    output = {}
    source = b'0 0.5\n12.566370614359172 0.5\n'
    for animation in (0, 1):
        status, body, seconds = process('/process_thr', 'thr', source, {'animation': animation})
        assert status == 200, body
        result = json.loads(body)
        assert result['thr'].encode() == source
        assert bool(result.get('gif_url')) == bool(animation)
        assets(result)
        if animation:
            gif = urllib.request.urlopen(BASE+result['gif_url']).read()
            assert gif[:3] == b'GIF' and gif[-1:] == b';'
        output['animation_'+str(animation)+'_seconds'] = seconds
    for invalid in [b'0 0\nBROKEN .5', b'0 nan', b'0 1.1', b'0 0\x00hidden', b'']:
        status, body, _ = process('/process_thr', 'thr', invalid, {'animation': 0})
        assert status == 400, (status, body)
    status, body, _ = process('/process_thr', 'thr', b'  // note\n0,0\n1,1 # end\n', {'animation': 0})
    assert status == 200, body
    status, body, _ = process('/process_thr', 'thr', source, {'animation': 2})
    assert status == 400, body
    status, body, seconds = process('/process', 'image', Path('tests/images/shapes.png').read_bytes(), {'animation': 0})
    assert status == 200, body
    result = json.loads(body);assets(result);assert 'gif_url' not in result
    # The image-produced PNG must match re-importing exactly the serialized THR.
    status, imported, _ = process('/process_thr', 'thr', result['thr'].encode(), {'animation': 0})
    assert status == 200, imported
    imported = json.loads(imported)
    assert urllib.request.urlopen(BASE+result['png_url']).read() == urllib.request.urlopen(BASE+imported['png_url']).read()
    output['image_without_animation_seconds'] = seconds
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    main()
