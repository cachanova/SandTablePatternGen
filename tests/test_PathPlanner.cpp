#include "doctest.h"
#include "../src/PathPlanner.h"
#include <vector>
#include <set>
#include <algorithm>
#include <cstdint>
#include <random>

namespace {
// Golden hashes below were captured from the original planner at 7ebc0a3.
// Hash coordinates in a fixed byte order so the fixtures work across platforms.
void hash_value(uint64_t& hash, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        hash ^= (value >> (8 * i)) & 255;
        hash *= UINT64_C(1099511628211);
    }
}

void hash_path(uint64_t& hash, const std::vector<Point>& path) {
    hash_value(hash, static_cast<uint32_t>(path.size()));
    for (const auto& point : path) {
        hash_value(hash, static_cast<uint32_t>(point.x));
        hash_value(hash, static_cast<uint32_t>(point.y));
    }
}
}

TEST_CASE("PathPlanner::plan_path") {
    // Center is 50,50
    int width = 100;
    int height = 100;

    std::vector<Point> points;
    
    // Create large enough components (> 15 pixels)
    // Comp 1: Line near 10,10
    for(int i=0; i<20; ++i) points.push_back({10 + i, 10});
    
    // Comp 2: Line near 50,50 (Center)
    for(int i=0; i<20; ++i) points.push_back({50 + i, 50});
    
    // Comp 3: Line near 90,90
    for(int i=0; i<20; ++i) points.push_back({70 + i, 90});

    // Note: This planner adds bridge points!
    auto path = PathPlanner::plan_path(points, width, height);

    REQUIRE(path.size() >= points.size());
    
    // Check if original points are visited (sampling one from each component)
    std::set<std::pair<int,int>> visited;
    for(const auto& p : path) {
        visited.insert({p.x, p.y});
    }

    CHECK(visited.count({10, 10}));
    CHECK(visited.count({50, 50}));
    CHECK(visited.count({70, 90}));

    // Every segment, including bridges between components, is rasterized with
    // Bresenham and must remain 8-connected.
    int max_jump = 0;
    for(size_t i=0; i<path.size()-1; ++i) {
        int dx = std::abs(path[i].x - path[i+1].x);
        int dy = std::abs(path[i].y - path[i+1].y);
        int jump = std::max(dx, dy);
        if (jump > max_jump) max_jump = jump;
    }
    CHECK(max_jump <= 1);
}

TEST_CASE("PathPlanner::TraceBack") {
    int width = 200;
    int height = 200;
    std::vector<Point> points;
    
    // U-shape component (Comp 1)
    // Left side: (50, 50) to (50, 150)
    for(int y=50; y<=150; ++y) points.push_back({50, y});
    // Bottom side: (50, 150) to (150, 150)
    for(int x=51; x<=150; ++x) points.push_back({x, 150});
    // Right side: (150, 150) to (150, 50)
    for(int y=149; y>=50; --y) points.push_back({150, y});
    
    // Detached segment (Comp 2) near the START of the U (50, 50)
    // Small line from (40, 50) to (40, 70)
    for(int y=50; y<=70; ++y) points.push_back({40, y});
    
    auto path = PathPlanner::plan_path(points, width, height);
    
    bool reached_left_end = false;
    bool reached_right_end = false;
    bool reached_detached = false;
    std::set<std::pair<int, int>> unique_points;

    for(const auto& p : path) {
        if (p.x == 50 && p.y == 50) reached_left_end = true;
        if (p.x == 150 && p.y == 50) reached_right_end = true;
        if (p.x == 40 && p.y == 50) reached_detached = true;
        unique_points.insert({p.x, p.y});
    }

    CHECK(reached_left_end);
    CHECK(reached_right_end);
    CHECK(reached_detached);
    // Reconnecting to an earlier point in the U requires a deliberate retrace.
    CHECK(path.size() > unique_points.size());
    for (size_t i = 1; i < path.size(); ++i) {
        CHECK(std::abs(path[i].x - path[i - 1].x) <= 1);
        CHECK(std::abs(path[i].y - path[i - 1].y) <= 1);
    }
}

TEST_CASE("PathPlanner filters invalid and duplicate points") {
    const std::vector<Point> points = {
        {1, 1}, {1, 1}, {2, 1}, {8, 8}, {-1, 0}, {10, 10}
    };
    const auto path = PathPlanner::plan_path(points, 10, 10);

    REQUIRE(!path.empty());
    std::set<std::pair<int, int>> visited;
    for (size_t i = 0; i < path.size(); ++i) {
        CHECK(path[i].x >= 0);
        CHECK(path[i].x < 10);
        CHECK(path[i].y >= 0);
        CHECK(path[i].y < 10);
        visited.insert({path[i].x, path[i].y});
        if (i > 0) {
            CHECK(std::abs(path[i].x - path[i - 1].x) <= 1);
            CHECK(std::abs(path[i].y - path[i - 1].y) <= 1);
        }
    }
    CHECK(visited.count({1, 1}) == 1);
    CHECK(visited.count({2, 1}) == 1);
    CHECK(visited.count({8, 8}) == 1);
    CHECK(PathPlanner::plan_path(points, 0, 10).empty());
}

TEST_CASE("PathPlanner preserves equal-distance choices and backtracking") {
    const std::vector<Point> symmetric = {{3, 3}, {5, 3}, {3, 5}, {5, 5}};
    const std::vector<Point> expected = {
        {5, 5}, {5, 5}, {5, 4}, {5, 3}, {4, 3}, {3, 3}, {3, 4}, {3, 5}
    };
    CHECK(PathPlanner::plan_path(symmetric, 10, 10) == expected);

    const std::vector<Point> separated = {{1, 1}, {2, 1}, {8, 8}};
    const std::vector<Point> expected_backtrack = {
        {5, 5}, {6, 6}, {7, 7}, {8, 8}, {7, 7}, {6, 6},
        {5, 5}, {4, 4}, {3, 3}, {3, 2}, {2, 1}, {1, 1}
    };
    CHECK(PathPlanner::plan_path(separated, 10, 10) == expected_backtrack);
}

TEST_CASE("PathPlanner preserves routes at sampling and grid boundaries") {
    // Two dense components cross bucket edges at x=15/16 and y=31/32.
    // Their sizes straddle both sampling thresholds and require repeated BFS.
    const int sizes[] = {20, 21, 100, 101};
    const uint64_t expected[] = {
        UINT64_C(9547738340907638307), UINT64_C(16578850442810686328),
        UINT64_C(1942542137837662699), UINT64_C(17412214186856042472)
    };
    for (int trial = 0; trial < 4; ++trial) {
        CAPTURE(sizes[trial]);
        std::vector<Point> points;
        for (int i = 0; i < sizes[trial]; ++i) {
            points.push_back({15 + i % 10, 15 + i / 10});
            points.push_back({55 + i % 10, 31 + i / 10});
        }
        const auto path = PathPlanner::plan_path(points, 96, 65);
        uint64_t hash = UINT64_C(14695981039346656037);
        hash_path(hash, path);
        CHECK(hash == expected[trial]);
        std::set<std::pair<int, int>> covered;
        for (auto point : path) covered.emplace(point.x, point.y);
        for (auto point : points) CHECK(covered.count({point.x, point.y}) == 1);
    }
}

TEST_CASE("PathPlanner preserves original routes on deterministic point clouds") {
    std::mt19937 random(20260917);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int trial = 0; trial < 80; ++trial) {
        const int width = 2 + random() % 70;
        const int height = 2 + random() % 70;
        const int densities[] = {2, 10, 30, 70};
        std::vector<Point> points;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (static_cast<int>(random() % 100) < densities[trial % 4]) {
                    points.push_back({x, y});
                }
            }
        }
        if (trial % 3 == 0) {
            points.push_back({-1, -1});
            points.push_back({width, height});
            points.push_back(points.front());
        }
        if (trial % 2) {
            // Explicit shuffle avoids implementation-defined distributions in
            // std::shuffle changing the fixture between standard libraries.
            for (size_t n = points.size(); n > 1; --n) {
                std::swap(points[n - 1], points[random() % n]);
            }
        }
        const auto path = PathPlanner::plan_path(points, width, height);
        hash_path(hash, path);
        CAPTURE(trial);
        for (size_t i = 1; i < path.size(); ++i) {
            REQUIRE(std::abs(path[i].x - path[i - 1].x) <= 1);
            REQUIRE(std::abs(path[i].y - path[i - 1].y) <= 1);
        }
    }
    CHECK(hash == UINT64_C(3047006404827104613));
}
