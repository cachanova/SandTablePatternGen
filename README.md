# ThrGen C++

A high-performance C++ tool for generating Sisyphus kinetic sand table tracks from images. This application processes input images (PNG, JPG, WebP) to detect outlines and optimizes them into a continuous polar coordinate path (`.thr` file).

## Features

-   **High Performance**: Built with C++17 for fast image processing and path optimization.
-   **No OpenCV Dependency**: Uses a custom, optimized Canny Edge Detector.
-   **Path Planning for Continuous Output**:
    -   **Parallel Component Labeling**: Finds connected edge components with a DSU-based pass.
    -   **Spatial Grid Acceleration**: Uses a grid of already-visited path points to speed global nearest searches.
    -   **Component Sampling**: Strides through large components to reduce global search cost.
    -   **Path Backtracking**: Uses BFS over existing path points to keep a continuous pen-down path.
    -   **Gap Bridging**: Connects nearby edge endpoints to reduce small discontinuities.
-   **Modern Web Interface**:
    -   **Paper & Ink UI**: A pipeline-style interface matching the Sisyphus table firmware's design language.
    -   **Drag-and-Drop**: Easy file upload support (images and `.thr` files).
    -   **Visualization**: View detected edges, the optimized path (gradient-colored for direction), and a simulated animation.
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
-   `cmake` (version 3.10+)
-   `g++` (or any C++17 compatible compiler)
-   `make`
-   `imagemagick` (runtime dependency for format conversion)

### Compilation
```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Web Server
1.  Run the server: `./ThrGenCpp`
2.  Open your browser to: `http://localhost:8080`
3.  Upload an image and click "Generate".

### CLI Tool
Batch processing and visualization:
```bash
./ThrGenCLI gen input.jpg output.thr --low 50 --high 150 --gif preview.gif --png path.png --edges edges.png
./ThrGenCLI vis input.thr --gif preview.gif --png path.png --size 1024
./ThrGenCLI bench
```

## Documentation

For a deep dive into the system design, algorithms, and data flow, please refer to the [Architecture Documentation](docs/ARCHITECTURE.md).

## Testing

```bash
# Run unit and integration tests
./build/ThrGenCpp_Tests
```
