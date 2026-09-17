#include "EdgeDetector.h"
#include "stb_image.h"
#include "Utils.h"
#include <cmath>
#include <stack>
#include <iostream>
#include <vector>
#include <algorithm>

namespace {
constexpr double kPi = 3.14159265358979323846;

bool valid_image(const unsigned char* data, int width, int height, int channels) {
    return data != nullptr && width > 0 && height > 0 && channels > 0;
}
}

std::vector<Point> EdgeDetector::detect_edges(const std::string& image_path, int low_threshold, int high_threshold, int blur_kernel_size) {
    int width, height, channels;
    unsigned char* data = stbi_load(image_path.c_str(), &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Error loading image: " << image_path << std::endl;
        return {};
    }

    std::vector<Point> edges = detect_edges_from_memory(data, width, height, channels, low_threshold, high_threshold, blur_kernel_size);
    stbi_image_free(data);
    return edges;
}

std::vector<Point> EdgeDetector::detect_edges_from_memory(const unsigned char* data, int width, int height, int channels, int low_threshold, int high_threshold, int blur_kernel_size) {
    if (!valid_image(data, width, height, channels) || width < 3 || height < 3) return {};

    low_threshold = std::clamp(low_threshold, 0, 255);
    high_threshold = std::clamp(high_threshold, 0, 255);
    if (low_threshold > high_threshold) std::swap(low_threshold, high_threshold);

    blur_kernel_size = std::max(1, blur_kernel_size);
    if (blur_kernel_size % 2 == 0) ++blur_kernel_size;
    int max_kernel = std::min(width, height);
    if (max_kernel % 2 == 0) --max_kernel;
    blur_kernel_size = std::min(blur_kernel_size, max_kernel);

    std::vector<uint8_t> gray = grayscale(data, width, height, channels);
    std::vector<uint8_t> blurred = gaussian_blur(gray, width, height, blur_kernel_size);

    std::vector<float> magnitude(width * height);
    std::vector<float> angle(width * height);
    sobel(blurred, width, height, magnitude, angle);

    std::vector<uint8_t> suppressed = non_max_suppression(magnitude, angle, width, height);

    // Mask borders to prevent frame detection
    const int margin = std::min({5, width / 2, height / 2});
    for (int i = 0; i < margin; ++i) {
        for (int x = 0; x < width; ++x) {
            suppressed[i * width + x] = 0; // Top
            suppressed[(height - 1 - i) * width + x] = 0; // Bottom
        }
        for (int y = 0; y < height; ++y) {
            suppressed[y * width + i] = 0; // Left
            suppressed[y * width + (width - 1 - i)] = 0; // Right
        }
    }

    std::vector<Point> edges = hysteresis(suppressed, width, height, low_threshold, high_threshold);
    std::sort(edges.begin(), edges.end(), [](const Point& left, const Point& right) {
        return left.y < right.y || (left.y == right.y && left.x < right.x);
    });
    return bridge_gaps(edges, width, height, 10);
}

std::vector<uint8_t> EdgeDetector::resize(const unsigned char* data, int width, int height, int channels, int new_width, int new_height) {
    if (!valid_image(data, width, height, channels) || new_width <= 0 || new_height <= 0) return {};

    const size_t output_size = static_cast<size_t>(new_width) * new_height * channels;
    std::vector<uint8_t> resized(output_size);
    const float x_ratio = new_width > 1 ? static_cast<float>(width - 1) / (new_width - 1) : 0.0f;
    const float y_ratio = new_height > 1 ? static_cast<float>(height - 1) / (new_height - 1) : 0.0f;

    Utils::parallel_for(0, new_height, [&](int i) {
        for (int j = 0; j < new_width; ++j) {
            const float source_x = x_ratio * j;
            const float source_y = y_ratio * i;
            int x = static_cast<int>(source_x);
            int y = static_cast<int>(source_y);
            const int x1 = std::min(x + 1, width - 1);
            const int y1 = std::min(y + 1, height - 1);
            float x_diff = source_x - x;
            float y_diff = source_y - y;

            for (int c = 0; c < channels; ++c) {
                uint8_t a = data[(y * width + x) * channels + c];
                uint8_t b = data[(y * width + x1) * channels + c];
                uint8_t d = data[(y1 * width + x) * channels + c];
                uint8_t e = data[(y1 * width + x1) * channels + c];

                float val = a * (1 - x_diff) * (1 - y_diff) +
                            b * (x_diff) * (1 - y_diff) +
                            d * (y_diff) * (1 - x_diff) +
                            e * (x_diff * y_diff);

                resized[(i * new_width + j) * channels + c] = (uint8_t)val;
            }
        }
    });
    return resized;
}

std::vector<uint8_t> EdgeDetector::grayscale(const unsigned char* data, int width, int height, int channels) {
    std::vector<uint8_t> gray(width * height);
    // Parallelize grayscale conversion
    Utils::parallel_for(0, width * height, [&](int i) {
        if (channels >= 3) {
            // Standard luminance weights
            gray[i] = static_cast<uint8_t>(0.299 * data[i * channels] + 0.587 * data[i * channels + 1] + 0.114 * data[i * channels + 2]);
        } else {
            gray[i] = data[i * channels];
        }
    });
    return gray;
}

std::vector<uint8_t> EdgeDetector::gaussian_blur(const std::vector<uint8_t>& image, int width, int height, int kernel_size) {
    if (kernel_size <= 1) return image;
    int r = kernel_size / 2;
    double sigma = std::max(1.0, kernel_size / 6.0); // Rough approximation

    // A separable Gaussian produces the same blur in O(k) work per pixel
    // instead of O(k^2), and clamped sampling avoids artificial black borders.
    std::vector<double> kernel(kernel_size);
    double sum = 0.0;

    for (int x = -r; x <= r; ++x) {
        const double value = std::exp(-(x * x) / (2.0 * sigma * sigma));
        kernel[x + r] = value;
        sum += value;
    }
    for (double& k : kernel) k /= sum;

    const size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<float> horizontal(pixel_count);
    Utils::parallel_for(0, height, [&](int y) {
        for (int x = 0; x < width; ++x) {
            double val = 0.0;
            for (int offset = -r; offset <= r; ++offset) {
                const int source_x = std::clamp(x + offset, 0, width - 1);
                val += image[static_cast<size_t>(y) * width + source_x] * kernel[offset + r];
            }
            horizontal[static_cast<size_t>(y) * width + x] = static_cast<float>(val);
        }
    });

    std::vector<uint8_t> output(pixel_count);
    Utils::parallel_for(0, height, [&](int y) {
        for (int x = 0; x < width; ++x) {
            double val = 0.0;
            for (int offset = -r; offset <= r; ++offset) {
                const int source_y = std::clamp(y + offset, 0, height - 1);
                val += horizontal[static_cast<size_t>(source_y) * width + x] * kernel[offset + r];
            }
            output[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(std::lround(val));
        }
    });
    return output;
}

void EdgeDetector::sobel(const std::vector<uint8_t>& image, int width, int height, std::vector<float>& magnitude, std::vector<float>& angle) {
    // Parallelize sobel (iterate over rows)
    Utils::parallel_for(1, height - 1, [&](int y) {
        const uint8_t* p_prev = &image[(y - 1) * width];
        const uint8_t* p_curr = &image[y * width];
        const uint8_t* p_next = &image[(y + 1) * width];
        
        float* mag_ptr = &magnitude[y * width];
        float* ang_ptr = &angle[y * width];

        for (int x = 1; x < width - 1; ++x) {
            // Gx = (p[y-1][x+1] + 2*p[y][x+1] + p[y+1][x+1]) - (p[y-1][x-1] + 2*p[y][x-1] + p[y+1][x-1])
            float gx = (p_prev[x + 1] + 2.0f * p_curr[x + 1] + p_next[x + 1]) - 
                       (p_prev[x - 1] + 2.0f * p_curr[x - 1] + p_next[x - 1]);
            
            // Gy = (p[y+1][x-1] + 2*p[y+1][x] + p[y+1][x+1]) - (p[y-1][x-1] + 2*p[y-1][x] + p[y-1][x+1])
            float gy = (p_next[x - 1] + 2.0f * p_next[x] + p_next[x + 1]) - 
                       (p_prev[x - 1] + 2.0f * p_prev[x] + p_prev[x + 1]);

            mag_ptr[x] = std::sqrt(gx * gx + gy * gy);
            ang_ptr[x] = std::atan2(gy, gx);
        }
    });
}

std::vector<uint8_t> EdgeDetector::non_max_suppression(const std::vector<float>& magnitude, const std::vector<float>& angle, int width, int height) {
    std::vector<uint8_t> suppressed(width * height, 0);

    // Parallelize suppression (iterate over rows)
    Utils::parallel_for(1, height - 1, [&](int y) {
        for (int x = 1; x < width - 1; ++x) {
            float ang = angle[y * width + x] * 180.0 / kPi;
            if (ang < 0) ang += 180;

            float q = 255;
            float r = 255;

            // angle 0
            if ((0 <= ang && ang < 22.5) || (157.5 <= ang && ang <= 180)) {
                q = magnitude[y * width + (x + 1)];
                r = magnitude[y * width + (x - 1)];
            }
            // angle 45
            else if (22.5 <= ang && ang < 67.5) {
                q = magnitude[(y + 1) * width + (x - 1)];
                r = magnitude[(y - 1) * width + (x + 1)];
            }
            // angle 90
            else if (67.5 <= ang && ang < 112.5) {
                q = magnitude[(y + 1) * width + x];
                r = magnitude[(y - 1) * width + x];
            }
            // angle 135
            else if (112.5 <= ang && ang < 157.5) {
                q = magnitude[(y - 1) * width + (x - 1)];
                r = magnitude[(y + 1) * width + (x + 1)];
            }

            float mag = magnitude[y * width + x];
            if (mag >= q && mag >= r) {
                suppressed[y * width + x] = static_cast<uint8_t>(std::min(255.0f, mag));
            } else {
                suppressed[y * width + x] = 0;
            }
        }
    });
    return suppressed;
}

std::vector<Point> EdgeDetector::hysteresis(const std::vector<uint8_t>& image, int width, int height, int low, int high) {
    std::vector<Point> edges;
    std::vector<uint8_t> visited(width * height, 0);
    std::mutex edges_mutex;
    std::vector<Point> strong_seeds;

    // Parallel seed finding
    Utils::parallel_for(1, height - 1, [&](int y) {
        std::vector<Point> local_seeds;
        for (int x = 1; x < width - 1; ++x) {
            int idx = y * width + x;
            if (image[idx] >= high) {
                local_seeds.push_back({x, y});
            }
        }
        if (!local_seeds.empty()) {
            std::lock_guard<std::mutex> lock(edges_mutex);
            strong_seeds.insert(strong_seeds.end(), local_seeds.begin(), local_seeds.end());
        }
    });

    std::stack<Point> stack;
    for (const auto& p : strong_seeds) {
        int idx = p.y * width + p.x;
        if (!visited[idx]) {
            stack.push(p);
            visited[idx] = 1;
            edges.push_back(p);
        }
    }

    // Connect weak edges (Flood fill)
    while (!stack.empty()) {
        Point p = stack.top();
        stack.pop();

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;

                int nx = p.x + dx;
                int ny = p.y + dy;

                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    int n_idx = ny * width + nx;
                    if (!visited[n_idx] && image[n_idx] >= low) {
                        visited[n_idx] = true;
                        stack.push({nx, ny});
                        edges.push_back({nx, ny});
                    }
                }
            }
        }
    }

    return edges;
}

std::vector<Point> EdgeDetector::bridge_gaps(const std::vector<Point>& edges, int width, int height, int max_gap) {
    if (edges.empty()) return edges;
    
    // Build a grid for fast lookup
    std::vector<uint8_t> grid(width * height, 0);
    for (const auto& p : edges) {
        if (p.x >= 0 && p.x < width && p.y >= 0 && p.y < height)
            grid[p.y * width + p.x] = 1;
    }

    // Count neighbors for each edge point (Parallelized)
    std::vector<int> neighbor_counts(edges.size());
    Utils::parallel_for(0, (int)edges.size(), [&](int i) {
        const auto& p = edges[i];
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = p.x + dx, ny = p.y + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    if (grid[ny * width + nx]) count++;
                }
            }
        }
        neighbor_counts[i] = count;
    });

    // Find endpoints
    std::vector<Point> endpoints;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (neighbor_counts[i] <= 2) {
            endpoints.push_back(edges[i]);
        }
    }

    if (endpoints.empty()) return edges;

    // Spatial grid for endpoints
    const int GRID_SIZE = 16;
    int grid_cols = (width + GRID_SIZE - 1) / GRID_SIZE;
    int grid_rows = (height + GRID_SIZE - 1) / GRID_SIZE;
    std::vector<std::vector<int>> endpoint_grid(grid_cols * grid_rows);
    for (int i = 0; i < (int)endpoints.size(); ++i) {
        int gx = endpoints[i].x / GRID_SIZE;
        int gy = endpoints[i].y / GRID_SIZE;
        endpoint_grid[gy * grid_cols + gx].push_back(i);
    }

    // Try to bridge nearby endpoints
    std::vector<Point> result = edges;
    std::vector<uint8_t> bridged(endpoints.size(), 0);

    for (int i = 0; i < (int)endpoints.size(); ++i) {
        if (bridged[i]) continue;
        const Point& p1 = endpoints[i];

        double best_dist_sq = max_gap * max_gap + 1.0;
        int best_idx = -1;

        // Search in nearby grid cells
        int gx0 = p1.x / GRID_SIZE;
        int gy0 = p1.y / GRID_SIZE;
        int r_cells = (max_gap + GRID_SIZE - 1) / GRID_SIZE;

        for (int dy = -r_cells; dy <= r_cells; ++dy) {
            for (int dx = -r_cells; dx <= r_cells; ++dx) {
                int gx = gx0 + dx, gy = gy0 + dy;
                if (gx >= 0 && gx < grid_cols && gy >= 0 && gy < grid_rows) {
                    for (int j : endpoint_grid[gy * grid_cols + gx]) {
                        if (i == j || bridged[j]) continue;
                        const Point& p2 = endpoints[j];
                        double d2 = (double)(p1.x-p2.x)*(p1.x-p2.x) + (double)(p1.y-p2.y)*(p1.y-p2.y);
                        if (d2 > 2.0 && d2 < best_dist_sq) {
                            best_dist_sq = d2;
                            best_idx = j;
                        }
                    }
                }
            }
        }

        if (best_idx != -1) {
            const Point& p2 = endpoints[best_idx];
            bridged[i] = 1;
            bridged[best_idx] = 1;

            // Draw line between p1 and p2 using Bresenham
            int x0 = p1.x, y0 = p1.y, x1 = p2.x, y1 = p2.y;
            int sdx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            int sdy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int err = sdx + sdy, e2;

            while (true) {
                if (!grid[y0 * width + x0]) {
                    result.push_back({x0, y0});
                    grid[y0 * width + x0] = 1;
                }
                if (x0 == x1 && y0 == y1) break;
                e2 = 2 * err;
                if (e2 >= sdy) { err += sdy; x0 += sx; }
                if (e2 <= sdx) { err += sdx; y0 += sy; }
            }
        }
    }

    return result;
}
