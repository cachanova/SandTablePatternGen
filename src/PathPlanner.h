#pragma once

#include "EdgeDetector.h"
#include <vector>

class PathPlanner {
public:
    static std::vector<Point> plan_path(const std::vector<Point>& points, int width, int height);
};
