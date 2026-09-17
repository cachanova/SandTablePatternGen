# ThrGen C++

A high-performance C++ tool for generating Sisyphus kinetic sand table tracks from images. This application processes input images (PNG, JPG, WebP) to detect outlines and optimizes them into a continuous polar coordinate path (`.thr` file).

## Features

-   **High Performance**: Built with C++17 for fast image processing and path optimization.
-   **No OpenCV Dependency**: Uses a custom, optimized Canny Edge Detector.
-   **Path Planning for Continuous Output**:
    -   **Component Labeling**: Finds connected edge components with a race-free DSU pass.
    -   **Spatial Grid Acceleration**: Uses a grid of already-visited path points to speed global nearest searches.
    -   **Component Sampling**: Strides through large components to reduce global search cost.
    -   **Path Backtracking**: Uses BFS over existing path points to keep a continuous pen-down path.
    -   **Gap Bridging**: Connects nearby edge endpoints to reduce small discontinuities.
-   **Modern Web Interface**:
    -   **Paper & Ink UI**: A pipeline interface matching the table firmware.
    -   **Drag-and-Drop**: Easy file upload support (images and `.thr` files).
    -   **Visualization**: View detected edges, the exact polar path preview, and an optional simulated animation.
    -   **Zen Garden Simulation**: Generates an animated GIF showing a steel ball tracing the path on a sand bed.
-   **Robustness**:
    -   Loads common formats via `stb_image`, with ImageMagick fallback for unsupported inputs (e.g., some WebP/HEIC files).
    -   Automatically resizes images larger than 2048px on either dimension.
    -   Masks image borders to prevent "frame" detection artifacts.

## Examples

### Intricate Natural Patterns (Butterfly)
| Original Image | Detected Edges | Processed Path | Sand Table Simulation |
| :---: | :---: | :---: | :---: |
| ![Original](docs/images/butterfly.jpg) | ![Edges](docs/images/butterfly_edges.png) | ![Path](docs/images/butterfly_path.png) | ![Simulation](docs/images/butterfly.gif) |

### Geometric Shapes
| Original Image | Detected Edges | Processed Path | Sand Table Simulation |
| :---: | :---: | :---: | :---: |
| ![Original](docs/images/shapes.png) | ![Edges](docs/images/shapes_edges.png) | ![Path](docs/images/shapes_path.png) | ![Simulation](docs/images/shapes.gif) |

### Landmark Architecture (Taj Mahal)
| Original Image | Detected Edges | Processed Path | Sand Table Simulation |
| :---: | :---: | :---: | :---: |
| ![Original](docs/images/taj_mahal.jpg) | ![Edges](docs/images/taj_mahal_edges.png) | ![Path](docs/images/taj_mahal_path.png) | ![Simulation](docs/images/taj_mahal.gif) |

### Detailed Portraits (David)
| Original Image | Detected Edges | Processed Path | Sand Table Simulation |
| :---: | :---: | :---: | :---: |
| ![Original](docs/images/david.webp) | ![Edges](docs/images/david_edges.png) | ![Path](docs/images/david_path.png) | ![Simulation](docs/images/david.gif) |

## Build Instructions

### Prerequisites
-   `cmake` (version 3.16+)
-   `g++` (or any C++17 compatible compiler)
-   `make`
-   `imagemagick` (runtime dependency for format conversion)

### Compilation
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Usage

### Web Server
1.  Run the server from either the repository or build directory: `./build/ThrGenCpp`
2.  Open your browser to: `http://localhost:8080`
3.  Upload an image and click "Generate Path".

### CLI Tool
Batch processing and visualization:
```bash
./ThrGenCLI gen input.jpg output.thr --low 50 --high 150 --gif preview.gif --png path.png --thumb path_thumb.png --edges edges.png
./ThrGenCLI vis input.thr --gif preview.gif --png path.png --thumb path_thumb.png --size 1024
./ThrGenCLI bench
```

## Documentation

For a deep dive into the system design, algorithms, and data flow, please refer to the [Architecture Documentation](docs/ARCHITECTURE.md).

## Testing

```bash
# Run unit and integration tests through CTest
ctest --test-dir build --output-on-failure

# Optional memory/undefined-behavior checks
cmake -S . -B build-sanitize -DTHRGEN_ENABLE_SANITIZERS=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
```

## Preview and table upload contract

All previews render the final serialized THR in polar coordinates. Full turns are
preserved; float coordinates map directly onto the 800px table overlay (center 400,
radius 380), with at most 0.05 px geometric chord approximation before antialiasing.
PNG pixel centers match browser canvas coordinates. Thumbnails use the same framing
at 128 px. Dense previews use thinner strokes (down to 0.55 pixels) to reduce solid
fills while keeping every segment; sparse drawings retain one-pixel strokes.
The renderer does not model approach moves, sand physics, or corner smoothing.

The browser skips animation by default. `/process` and `/process_thr` accept
`animation=0` or `1`; omitted values retain animation for existing callers. Both
return `thr`, `png_url`, `thumb_url`, `point_count`, dimensions and timing; `gif_url`
is present only for animation. Browser path display loads the PNG directly instead
of receiving a second large path array. `preview` is retained as an empty array.
Cancel stops browser work/transfers; an already-running server calculation can still
finish. New processing clears the previous uploadable result.

Transfers fetch preview assets concurrently, then write to the table sequentially:
`Name.thr`, `Name.png`, and `Name.png` at `/api/files/upload?thumbnail=1`. A file is
marked saved only after the table acknowledges it. Partial failure retains per-file
status and retries unsaved files; replacing a preview always resends its thumbnail.
Timeout/cancellation does not claim an unacknowledged write failed to commit.
Limits match firmware: THR 8 MiB, PNG 2 MiB, thumbnail 128 KiB, and 76-character ASCII bases.
Imported THRs are rejected on malformed lines instead of silently dropping them.

Tests: `ctest --test-dir build --output-on-failure` runs C++ and browser transport
checks. With the server running, `python tests/test_http.py` checks the HTTP pipeline,
asset readability, animation selection, parser errors, and identical previews for
image-generated and re-imported serialized THRs.
