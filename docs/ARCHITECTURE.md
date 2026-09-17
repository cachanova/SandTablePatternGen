# ThrGen C++ Architecture Documentation

## Overview

ThrGen C++ is a high-performance tool designed to convert raster images into kinetic art tracks (`.thr` files) for Sisyphus tables. It is built with C++17 for speed and minimal dependencies, using a single-page web interface for interaction.

## System Architecture

The application follows a monolithic architecture with a clear separation between the C++ backend and the HTML/JS frontend.

```mermaid
graph TD
    User[User Browser] -->|HTTP Request| Server[C++ Server]
    Server -->|HTTP Response| User
    Server --> EdgeDetector[Edge Detector]
    Server --> PathPlanner[Path Planner]
    Server --> ThrGenerator[THR Generator]
    Server --> ThrPreview[Polar THR Preview]
    Server --> ImgIO[Image I/O]
```

### Components

1.  **Web Server (`main.cpp`)**:
    -   Uses `cpp-httplib` to serve static files and handle the `/process` API endpoint.
    -   Also exposes `/process_thr` to preview existing `.thr` files.
    -   Orchestrates the pipeline: Upload $\to$ Decode/Resize $\to$ Edge Detect $\to$ Plan Path $\to$ Generate THR $\to$ Generate GIF/PNG.
    -   Decodes common formats directly from request memory, limits request and decoded-image sizes, and falls back to ImageMagick only if `stb_image` cannot load the file.
    -   Uses unique temporary and output names so concurrent requests cannot overwrite one another.

2.  **Edge Detector (`EdgeDetector.cpp`)**:
    -   **Input**: Raw image data.
    -   **Output**: Vector of edge points.
    -   **Algorithm**: Custom Canny Edge Detection implementation.
        -   **Grayscale**: Luminance conversion (Parallelized).
        -   **Gaussian Blur**: Noise reduction (Parallelized).
        -   **Sobel Operator**: Gradient magnitude and direction calculation (Parallelized).
        -   **Non-Max Suppression**: Thinning edges (Parallelized).
        -   **Hysteresis Thresholding**: Strong-seed collection is parallelized; flood fill is sequential.
    -   **Optimization**: Border pixels (5px margin) are explicitly masked to prevent frame detection artifacts.
    -   **Gap Bridging**: Nearby endpoints are connected with a spatial grid and Bresenham line fill.

3.  **Path Planner (`PathPlanner.cpp`)**:
    -   **Goal**: Convert a cloud of edge points into a single continuous path.
    -   **Stage 1: Component Labeling**: Uses a sequential DSU pass with path halving to group connected pixels without concurrent parent-pointer races. Roots remain unchanged, preserving component order.
    -   **Stage 2: Local Traversal**: Walks to adjacent unvisited edge pixels when possible.
    -   **Stage 3: Global Search**:
        -   Maintains a spatial grid of already-traversed path points for nearest-point search.
        -   Samples component points with a stride to reduce search cost on large components.
        -   Retains the first unvisited point as a fallback when all regular samples in a component have been visited.
        -   Seeds a shared distance bound with a real connection to the current path position. Adjacent pairs are resolved directly; longer searches enumerate grid-ring perimeters and skip cells whose minimum distance is worse than the bound.
        -   Retains equal-distance candidates and uses the same coordinate tie-break, preserving the generated route.
        -   Runs the reduced global search sequentially, avoiding fresh asynchronous workers on each restart.
        -   Uses BFS over existing path points to "backtrack" to the best connection point. Only parent entries enqueued by the previous BFS are reset, including entries left in its frontier.
        -   Connects to the chosen target with a straight Bresenham segment to keep a continuous path.

4.  **THR Generator (`ThrGenerator.cpp`)**:
    -   **Input**: Ordered Cartesian path.
    -   **Output**: Theta-Rho coordinates.
    -   **Logic**:
        -   Converts $(x, y)$ to $(\theta, \rho)$.
        -   Unwraps $\theta$ to ensure continuous rotation (avoiding jumps from $\pi$ to $-\pi$).
        -   Normalizes $\rho$ to $[0, 1]$.
        -   Adds leading/trailing $\rho=0$ points to anchor the path.

5.  **THR Preview (`ThrPreview.cpp`)**:
    -   Rasterizes the vector path into a pixel grid.
    -   Uses `gif.h` to write frames.
    -   **Simulation**: Draws a persistent "track" (cumulative frames) and a temporary "ball" overlay to simulate the Sisyphus table effect.
    -   Generates an 800px PNG preview and a configurable-size thumbnail mapped to the SisyphusTable viewer's coordinate system.

## Key Optimizations

-   **Multi-threading**: `Utils::parallel_for` distributes pixel-wise operations (grayscale, blur, Sobel, NMS), hysteresis seed collection, and bridge-gap neighbor counts across cores. Component labeling and bounded global path search are sequential.
-   **Strided Sampling**: The global search samples every $K$-th point in large components to reduce scan cost.
-   **Intelligent Resizing**: Images > 2048px are downscaled before processing to keep runtime interactive.
-   **Spatial Index (Grid-of-Buckets)**: The planner uses a spatial grid of visited path points for near-neighbor lookup.
-   **DSU Path Halving**: Component labeling shortens parent chains without changing component roots or introducing synchronization overhead.
-   **Bounded Global Search**: Exact cell-distance bounds, an adjacent-pair shortcut and perimeter traversal reduce search work while preserving the original sampling and tie-breaking rules.
-   **Sparse BFS Reset**: Backtracking clears only entries touched by the previous search instead of the full image-sized parent array.
-   **Native C++ Bilinear Downsampling**: Handles common resize operations without ImageMagick; ImageMagick remains a fallback for unsupported formats.
-   **Bridge Gaps Optimization**: Spatial grid acceleration for endpoint bridging.
-   **`std::vector<uint8_t>` over `std::vector<bool>`**: Avoids bit-packing overhead in parallel sections.

## Performance Timing

The server includes per-request timing instrumentation for each pipeline stage:
-   `edge_detection`: End-to-end edge detection (grayscale → blur → sobel → NMS → hysteresis → gap-bridging)
-   `path_planning`: Path planning and traversal
-   `thr_generation`: Polar conversion and serialization
-   `gif_generation`: Animation rendering
-   `png_generation`: Static image output
-   `thumb_generation`: Thumbnail output

Timing data is returned in the JSON response under the `timing` key and printed to the console.

See [Planner performance and follow-up analysis](PLANNER_PERFORMANCE.md) for measured
planning improvements, output-preservation checks and further routing proposals.

## Data Flow

1.  **Upload**: User drops an image; the server enforces a bounded request size.
2.  **Loading**: `stb_image` decodes common formats directly from memory; unsupported formats use a uniquely named ImageMagick conversion file.
3.  **Preprocessing**: Images larger than 2048px on either axis are downscaled while preserving aspect ratio.
4.  **Detection**: Edges extracted into `std::vector<Point>`.
5.  **Planning**: Points reordered into a valid continuous path (graph traversal).
6.  **Generation**:
    -   Path converted to `.thr` string.
    -   Path rendered to `.gif`, preview PNG, and thumbnail PNG files under unique names.
7.  **Response**: JSON containing the `.thr` text, asset URLs, preview points, and timing data sent back to the browser.

Preview assets are generated from serialized THR coordinates using unwrapped polar interpolation. PNG/thumbnail generation is required; GIF encoding is optional. The browser shares one cancellable processing flow and one acknowledged, sequential table transfer flow in `static/transfer.js`.

Table writes use `static/transfer.js`: PNGs are fetched concurrently from this
server, while writes to the ESP32 remain sequential. Pattern, full preview and
thumbnail have independent acknowledgement receipts. A full preview retry
invalidates the thumbnail receipt because firmware discards old thumbnails when
replacing PNGs. Failure/cancellation never reports an unacknowledged file as saved.
