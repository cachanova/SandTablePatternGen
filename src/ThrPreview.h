#pragma once
#include "ThrGenerator.h"
#include <functional>
#include <string>
#include <vector>

struct PreviewPoint {
    double x, y;
};

// Render the serialized THR geometry, never an image-space planning path.
class ThrPreview {
  public:
    // Pixel-space, floating-point path; retains every unwrapped revolution.
    static void visit(const std::vector<ThrPoint> &points, int size,
                      const std::function<void(PreviewPoint)> &emit);
    static bool png(const std::vector<ThrPoint> &points, const std::string &filename,
                    int size = 800);
    static bool gif(const std::vector<ThrPoint> &points, const std::string &filename,
                    int size = 800);
    static std::vector<PreviewPoint> path(const std::vector<ThrPoint> &points, int size);
};
