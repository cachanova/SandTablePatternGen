#include "doctest.h"
#include "../src/EdgeDetector.h"
#include <vector>
#include <numeric>

TEST_CASE("EdgeDetector::detect_edges_from_memory") {
    // Create a 30x30 image (larger to accommodate 5px border mask)
    int width = 30;
    int height = 30;
    int channels = 1;
    std::vector<unsigned char> data(width * height, 0);

    // Draw 10x10 square at (10,10)
    for (int y = 10; y < 20; ++y) {
        for (int x = 10; x < 20; ++x) {
            data[y * width + x] = 255;
        }
    }

    // Border masking check: ensure borders are 0 first (they are init to 0)
    // Actually, let's put white pixels at the very border to test if they get ignored
    data[0] = 255;
    data[width - 1] = 255;
    
    // Using low thresholds to ensure detection of the square
    auto edges = EdgeDetector::detect_edges_from_memory(data.data(), width, height, channels, 50, 150, 3);
    auto repeated = EdgeDetector::detect_edges_from_memory(data.data(), width, height, channels, 50, 150, 3);

    // Should find points. 
    // The square is solid, so edges should be at the transition 2-3 and 6-7.
    // Given 3x3 Sobel and blur, exact pixel locations might shift slightly, 
    // but we expect *some* edges.
    
    CHECK(edges.size() > 0);
    CHECK(edges == repeated);

    // Check that NO edges are at the frame boundary (0 or width-1/height-1)
    for (const auto& p : edges) {
        CHECK(p.x > 0);
        CHECK(p.x < width - 1);
        CHECK(p.y > 0);
        CHECK(p.y < height - 1);
    }
}

TEST_CASE("EdgeDetector rejects invalid and tiny images safely") {
    std::vector<unsigned char> tiny(4, 255);
    CHECK(EdgeDetector::detect_edges_from_memory(nullptr, 2, 2, 1, 50, 150).empty());
    CHECK(EdgeDetector::detect_edges_from_memory(tiny.data(), 2, 2, 1, 50, 150).empty());
    CHECK(EdgeDetector::resize(nullptr, 2, 2, 1, 4, 4).empty());
    CHECK(EdgeDetector::resize(tiny.data(), 2, 2, 1, 0, 4).empty());
}

TEST_CASE("EdgeDetector resize preserves source corners") {
    const std::vector<unsigned char> source = {10, 20, 30, 40};
    const auto resized = EdgeDetector::resize(source.data(), 2, 2, 1, 3, 3);

    REQUIRE(resized.size() == 9);
    CHECK(resized[0] == 10);
    CHECK(resized[2] == 20);
    CHECK(resized[6] == 30);
    CHECK(resized[8] == 40);
}

TEST_CASE("Gaussian blur does not invent edges at image boundaries") {
    constexpr int size = 30;
    const std::vector<unsigned char> flat_image(size * size, 255);
    const auto edges = EdgeDetector::detect_edges_from_memory(
        flat_image.data(), size, size, 1, 10, 20, 15);
    CHECK(edges.empty());
}
