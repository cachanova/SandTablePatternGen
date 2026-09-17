# ThrGen C++ Optimization Task List

This document tracks performance optimizations for the ThrGen engine based on the current implementation.

## 1. Path Planner: Efficient Global Search
- [x] Maintain a spatial grid of already-visited path points to accelerate nearest searches.
- [x] Sample component points with a stride during global searches to reduce scan cost on large components.
- [x] Reuse a preallocated parent buffer for BFS backtracking over the existing path.

## 2. Parallelize Component Labeling & Hysteresis
- [x] Use a race-free linear Disjoint Set Union (DSU) pass for component labeling.
- [x] Parallelize the strong-seed scan in hysteresis thresholding.

## 3. Imaging Pipeline Efficiency
- [x] Implement native C++ bilinear downsampling for resizing.
- [x] Optimize `GifGenerator` background initialization with a parallel fill.
- [x] Optimize `Bridge Gaps` neighbor counting with parallelization and a spatial grid of endpoints.
- [ ] Remove ImageMagick from the common path for unsupported formats (currently still a fallback).

## 4. Data Structure Improvements
- [x] Use `std::vector<uint8_t>` instead of `std::vector<bool>` to avoid bit-packing overhead in parallel sections.

## 5. Benchmarking & Profiling
- [x] Add timing instrumentation for the main pipeline stages (edge detection, path planning, THR generation, GIF/PNG).
- [x] Include timing data in the JSON API response.
