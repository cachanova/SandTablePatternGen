#include "ThrPreview.h"
#include "stb_image_write.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
#include "gif.h"
}
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <numeric>

namespace {
constexpr size_t maxSamples = 50000000;
// Dense tracks otherwise merge into a nearly solid field at display size.
// Narrow their antialias profile while retaining the locations of
// every sampled segment. Sparse drawings keep the original coverage profile.
double strokeWidth(const std::vector<uint8_t> &alpha) {
    const double coverage = std::accumulate(alpha.begin(), alpha.end(), 0.0) /
                            (255.0 * alpha.size());
    return 1.0 - .45 * std::clamp((coverage - .08) / .12, 0.0, 1.0);
}
uint8_t strokeCoverage(uint8_t alpha, double width) {
    return static_cast<uint8_t>(std::lround(255.0 *
        std::clamp(alpha / 255.0 - .5 + width / 2, 0.0, width)));
}
void stroke(std::vector<uint8_t> &alpha, int size, PreviewPoint a, PreviewPoint b) {
    const double dx = b.x - a.x, dy = b.y - a.y, square = dx * dx + dy * dy;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min(a.x, b.x) - 1)));
    const int x1 = std::min(size - 1, static_cast<int>(std::ceil(std::max(a.x, b.x) + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min(a.y, b.y) - 1)));
    const int y1 = std::min(size - 1, static_cast<int>(std::ceil(std::max(a.y, b.y) + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double t =
                square > 0 ? std::clamp(((x + .5 - a.x) * dx + (y + .5 - a.y) * dy) / square, 0.0, 1.0) : 0;
            const double distance = std::hypot(x + .5 - a.x - t * dx, y + .5 - a.y - t * dy);
            const auto coverage =
                static_cast<uint8_t>(std::lround(std::clamp(1 - distance, 0.0, 1.0) * 255));
            auto &pixel = alpha[static_cast<size_t>(y) * size + x];
            pixel = std::max(pixel, coverage);
        }
}
} // namespace

void ThrPreview::visit(const std::vector<ThrPoint> &points, int size,
                       const std::function<void(PreviewPoint)> &emit) {
    if (size < 16 || size > 8192)
        throw std::invalid_argument("Preview size must be 16..8192");
    if (points.empty())
        throw std::invalid_argument("THR contains no coordinates");
    const double center = size / 2.0, radius = size * .475;
    for (const auto &p : points)
        if (!std::isfinite(p.theta) || !std::isfinite(p.rho) || p.rho < 0 || p.rho > 1)
            throw std::invalid_argument("Invalid THR coordinate");
    auto output = [&](const ThrPoint &p) {
        emit({center + radius * p.rho * std::cos(p.theta),
              center + radius * p.rho * std::sin(p.theta)});
    };
    output(points.front());
    size_t count = 1;
    for (size_t i = 1; i < points.size(); ++i) {
        const auto a = points[i - 1], b = points[i];
        const double dt = b.theta - a.theta, dr = b.rho - a.rho, r = std::max(a.rho, b.rho);
        // ||p''(u)|| <= R*(r*dt^2+2*|dr*dt|), hence chord error <= M/(8*n^2).
        // Bound travel too, keeping antialias raster work linear in path length.
        const double curvature = radius * (r * dt * dt + 2 * std::abs(dr * dt));
        const double travel = radius * (std::abs(dr) + r * std::abs(dt));
        const double needed =
            std::max({1.0, std::ceil(std::sqrt(curvature / (8 * .05))), std::ceil(travel / 2)});
        if (!std::isfinite(needed) || needed > maxSamples - count)
            throw std::length_error("THR preview exceeds 50 million samples");
        const size_t n = static_cast<size_t>(needed);
        count += n;
        for (size_t j = 1; j <= n; ++j) {
            const double u = static_cast<double>(j) / n;
            output(j == n ? b : ThrPoint{a.theta + dt * u, a.rho + dr * u});
        }
    }
}

std::vector<PreviewPoint> ThrPreview::path(const std::vector<ThrPoint> &points, int size) {
    std::vector<PreviewPoint> result;
    visit(points, size, [&](PreviewPoint p) {
        if (result.size() >= 1000000)
            throw std::length_error(
                "Browser preview exceeds one million samples; use CLI PNG output");
        result.push_back(p);
    });
    return result;
}

bool ThrPreview::png(const std::vector<ThrPoint> &points, const std::string &filename, int size) {
    if (size < 16 || size > 8192)
        throw std::invalid_argument("Preview size must be 16..8192");
    std::vector<uint8_t> alpha(static_cast<size_t>(size) * size, 0);
    PreviewPoint previous{};
    bool first = true;
    visit(points, size, [&](PreviewPoint p) {
        stroke(alpha, size, first ? p : previous, p);
        previous = p;
        first = false;
    });
    std::vector<uint8_t> rgba(alpha.size() * 4, 255);
    const double width = strokeWidth(alpha);
    for (size_t i = 0; i < alpha.size(); ++i)
        rgba[i * 4 + 3] = strokeCoverage(alpha[i], width);
    return stbi_write_png(filename.c_str(), size, size, 4, rgba.data(), size * 4) != 0;
}

bool ThrPreview::gif(const std::vector<ThrPoint> &points, const std::string &filename, int size) {
    if (size < 16 || size > 1024)
        throw std::invalid_argument("GIF size must be 16..1024");
    size_t samples = 0;
    std::vector<uint8_t> alpha(static_cast<size_t>(size) * size, 0);
    PreviewPoint previous{};
    bool first = true;
    visit(points, size, [&](PreviewPoint p) {
        stroke(alpha, size, first ? p : previous, p);
        previous = p;
        first = false;
        ++samples;
    });
    const double width = strokeWidth(alpha);
    std::fill(alpha.begin(), alpha.end(), 0);
    const size_t perFrame = std::max<size_t>(1, (samples + 99) / 100);
    GifWriter writer{};
    if (!GifBegin(&writer, filename.c_str(), size, size, 5))
        return false;
    struct CloseGif {
        GifWriter *writer;
        ~CloseGif() {
            if (writer->f)
                GifEnd(writer);
        }
    } close{&writer};
    std::vector<uint8_t> frame(alpha.size() * 4, 255);
    first = true;
    bool ok = true;
    size_t index = 0;
    visit(points, size, [&](PreviewPoint p) {
        stroke(alpha, size, first ? p : previous, p);
        previous = p;
        first = false;
        if (++index % perFrame && index != samples)
            return;
        for (size_t i = 0; i < alpha.size(); ++i) {
            const auto coverage = strokeCoverage(alpha[i], width);
            frame[i * 4] = static_cast<uint8_t>(50 + 180 * coverage / 255);
            frame[i * 4 + 1] = static_cast<uint8_t>(40 + 180 * coverage / 255);
            frame[i * 4 + 2] = static_cast<uint8_t>(30 + 170 * coverage / 255);
        }
        for (int y = std::max(0, static_cast<int>(p.y) - 4);
             y < std::min(size, static_cast<int>(p.y) + 5); ++y)
            for (int x = std::max(0, static_cast<int>(p.x) - 4);
                 x < std::min(size, static_cast<int>(p.x) + 5); ++x)
                if (std::hypot(x - p.x, y - p.y) <= 4) {
                    const size_t k = (static_cast<size_t>(y) * size + x) * 4;
                    frame[k] = frame[k + 1] = frame[k + 2] = 192;
                }
        ok = GifWriteFrame(&writer, frame.data(), size, size, 5) && ok;
    });
    return GifEnd(&writer) && ok;
}
