#include "doctest.h"
#include "../src/PathPlanner.h"
#include <vector>
#include <set>

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
