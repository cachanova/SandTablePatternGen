#include "doctest.h"
#include "../src/EdgeDetector.h"
#include "../src/PathPlanner.h"
#include "../src/ThrGenerator.h"
#include "stb_image.h"
#include <string>
#include <iostream>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_set>

void test_image_pipeline(const std::string& filename, int low, int high, int blur) {
    int width, height, channels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    
    REQUIRE_MESSAGE(data != nullptr, (std::string("Failed to load ") + filename).c_str());

    auto start = std::chrono::high_resolution_clock::now();

    // 1. Edge Detection
    auto edges = EdgeDetector::detect_edges_from_memory(data, width, height, channels, low, high, blur);
    stbi_image_free(data);

    // Depending on the image, we expect some edges
    CHECK_MESSAGE(edges.size() > 0, (std::string("No edges found in ") + filename).c_str());

    // 2. Path Planning
    auto path = PathPlanner::plan_path(edges, width, height);

    std::unordered_set<uint64_t> path_points;
    for (const auto& point : path) {
        path_points.insert((static_cast<uint64_t>(static_cast<uint32_t>(point.y)) << 32) |
                           static_cast<uint32_t>(point.x));
    }
    for (const auto& edge : edges) {
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(edge.y)) << 32) |
                             static_cast<uint32_t>(edge.x);
        CHECK_MESSAGE(path_points.count(key) == 1,
                      (std::string("Path omitted an edge in ") + filename).c_str());
    }
    
    // 3. THR Generation
    auto thr = ThrGenerator::generate_thr(path, width, height);
    std::string thr_content = ThrGenerator::to_string(thr);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "Processed " << filename << " (" << width << "x" << height << ") in " << diff.count() << "s. "
              << "Edges: " << edges.size() << ", Path: " << path.size() << " points." << std::endl;

    CHECK(thr_content.length() > 0);
    
    // Check path continuity
    if (!path.empty()) {
        double max_jump = 0;
        for(size_t i=0; i<path.size()-1; ++i) {
            double dx = path[i].x - path[i+1].x;
            double dy = path[i].y - path[i+1].y;
            double dist = std::sqrt(dx*dx + dy*dy);
            if(dist > max_jump) max_jump = dist;
        }
        CHECK(max_jump <= std::sqrt(2.0));
    }
}

TEST_CASE("Integration::Shapes") {
    test_image_pipeline("../tests/images/shapes.png", 50, 150, 5);
}

TEST_CASE("Integration::Butterfly") {
    test_image_pipeline("../tests/images/butterfly.jpg", 40, 120, 5);
}

TEST_CASE("Integration::Baboon") {
    // Tuned for cleaner features
    test_image_pipeline("../tests/images/baboon.jpg", 80, 200, 7);
}

TEST_CASE("Integration::TajMahal") {
    // Tuned for clear structural outlines
    test_image_pipeline("../tests/images/taj_mahal.jpg", 70, 180, 7);
}

TEST_CASE("Integration::Nature") {
    test_image_pipeline("../tests/images/nature.jpg", 50, 150, 7);
}

TEST_CASE("Integration::City") {
    // Very high detail, needs high thresholds to avoid noise explosion
    test_image_pipeline("../tests/images/city.jpg", 100, 220, 9);
}

TEST_CASE("Integration::Sketch") {
    test_image_pipeline("../tests/images/sketch.jpg", 40, 120, 5);
}
