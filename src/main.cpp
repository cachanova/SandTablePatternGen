#include "EdgeDetector.h"
#include "ThrPreview.h"
#include "PathPlanner.h"
#include "ThrGenerator.h"
#include "Utils.h"
#include "httplib.h"
#include "json.hpp"
#include "stb_image.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kMaxDimension = 2048;
constexpr size_t kMaxUploadBytes = size_t{25} * 1024 * 1024;
constexpr size_t kMaxSourcePixels = size_t{40} * 1000 * 1000;
constexpr size_t kMaxThrPoints = 500000;

struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> pixels;
};

class TemporaryFiles {
public:
    ~TemporaryFiles() {
        std::error_code ec;
        for (const auto& path : paths_) fs::remove(path, ec);
    }

    fs::path add(const std::string& suffix) {
        static std::atomic<uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path path = fs::temp_directory_path() /
                        ("thrgen_" + std::to_string(stamp) + "_" +
                         std::to_string(sequence.fetch_add(1)) + suffix);
        paths_.push_back(path);
        return path;
    }

private:
    std::vector<fs::path> paths_;
};

std::string shell_quote(const fs::path& path) {
    std::string quoted = "'";
    for (char c : path.string()) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    return quoted + "'";
}

void error_response(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(message, "text/plain; charset=utf-8");
}

bool normalize_image(unsigned char* raw, int width, int height, int channels,
                     ImageData& image, std::string& error) {
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owned(raw, stbi_image_free);
    if (!raw || width <= 0 || height <= 0 || channels <= 0 || channels > 4) {
        error = "Invalid image data";
        return false;
    }

    const size_t pixel_count = static_cast<size_t>(width) * height;
    if (pixel_count > kMaxSourcePixels) {
        error = "Image dimensions are too large";
        return false;
    }

    image.width = width;
    image.height = height;
    image.channels = channels;
    if (width > kMaxDimension || height > kMaxDimension) {
        const double ratio = std::min(static_cast<double>(kMaxDimension) / width,
                                      static_cast<double>(kMaxDimension) / height);
        image.width = std::max(1, static_cast<int>(width * ratio));
        image.height = std::max(1, static_cast<int>(height * ratio));
        image.pixels = EdgeDetector::resize(raw, width, height, channels,
                                            image.width, image.height);
    } else {
        const size_t byte_count = pixel_count * static_cast<size_t>(channels);
        image.pixels.assign(raw, raw + byte_count);
    }

    if (image.pixels.empty()) {
        error = "Could not resize image";
        return false;
    }
    return true;
}

bool decode_image(const std::string& content, ImageData& image, std::string& error) {
    if (content.empty()) {
        error = "The uploaded image is empty";
        return false;
    }
    if (content.size() > kMaxUploadBytes || content.size() > static_cast<size_t>(INT_MAX)) {
        error = "The uploaded image is too large";
        return false;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(content.data());
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(bytes, static_cast<int>(content.size()),
                              &width, &height, &channels) != 0 &&
        width > 0 && height > 0 &&
        static_cast<size_t>(width) * height <= kMaxSourcePixels) {
        unsigned char* raw = stbi_load_from_memory(bytes, static_cast<int>(content.size()),
                                                   &width, &height, &channels, 0);
        return normalize_image(raw, width, height, channels, image, error);
    }

    // ImageMagick handles formats stb_image cannot decode (for example HEIC)
    // and downscales images too large to decode in-process, under resource
    // limits. The filenames are generated internally and shell-quoted.
    TemporaryFiles temporary;
    const fs::path input = temporary.add(".upload");
    const fs::path output = temporary.add(".png");
    {
        std::ofstream stream(input, std::ios::binary);
        if (!stream || !stream.write(content.data(), static_cast<std::streamsize>(content.size()))) {
            error = "Could not save the uploaded image";
            return false;
        }
    }

    const std::string command = "magick -limit memory 512MiB -limit map 1GiB -limit disk 4GiB " +
                                shell_quote(input) + " -resize '2048x2048>' -strip " +
                                shell_quote(output);
    if (std::system(command.c_str()) != 0) {
        error = "Unsupported or invalid image format";
        return false;
    }

    unsigned char* raw = stbi_load(output.string().c_str(), &width, &height, &channels, 0);
    return normalize_image(raw, width, height, channels, image, error);
}

bool parse_parameter(const httplib::Request& req, const char* name, int default_value,
                     int minimum, int maximum, int& value, std::string& error) {
    if (!req.form.has_field(name)) {
        value = default_value;
        return true;
    }
    const std::string text = req.form.get_field(name);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        error = std::string(name) + " must be an integer from " +
                std::to_string(minimum) + " to " + std::to_string(maximum);
        return false;
    }
    return true;
}

std::string unique_base_name() {
    static std::atomic<uint64_t> sequence{0};
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "sim_" + std::to_string(millis) + "_" +
           std::to_string(sequence.fetch_add(1));
}

bool generate_assets(const std::vector<ThrPoint>& points, bool animation,
                     const fs::path& static_path, const std::string& base_name,
                     std::string& error) {
    const fs::path gif = static_path / (base_name + ".gif");
    const fs::path png = static_path / (base_name + ".png");
    const fs::path thumb = static_path / (base_name + "_thumb.png");
    try {
        bool ok;
        { Utils::Timer timer("png_generation", true, true); ok = ThrPreview::png(points, png.string()); }
        { Utils::Timer timer("thumb_generation", true, true); ok = ThrPreview::png(points, thumb.string(), 128) && ok; }
        if (animation) { Utils::Timer timer("gif_generation", true, true); ok = ThrPreview::gif(points, gif.string()) && ok; }
        if (ok) return true;
        error = "Could not write visualization files";
    } catch (const std::exception& e) { error = e.what(); }
    std::error_code ec;
    fs::remove(gif, ec); fs::remove(png, ec); fs::remove(thumb, ec);
    return false;
}

json path_json(const std::vector<Point>& points) {
    json result = json::array();
    for (const auto& point : points) result.push_back({point.x, point.y});
    return result;
}

json timing_json() {
    json timing;
    const auto& report = Utils::get_timing_report();
    for (const auto& stage : report.stages) timing[stage.first] = stage.second;
    timing["total_ms"] = report.total_ms();
    return timing;
}

fs::path find_static_path() {
    for (const fs::path& candidate : {fs::path("static"), fs::path("../static")}) {
        std::error_code ec;
        if (fs::is_regular_file(candidate / "index.html", ec)) return fs::absolute(candidate);
    }
    return {};
}

} // namespace

int main() {
    const fs::path static_path = find_static_path();
    if (static_path.empty()) {
        std::cerr << "Could not find static/index.html. Run the server from the repository or build directory.\n";
        return 1;
    }

    httplib::Server server;
    server.set_payload_max_length(kMaxUploadBytes + size_t{1024} * 1024);
    server.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                    std::exception_ptr exception) {
        try {
            if (exception) std::rethrow_exception(exception);
        } catch (const std::exception& e) {
            std::cerr << "Request failed: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "Request failed with an unknown exception\n";
        }
        error_response(res, 500, "Internal server error");
    });

    std::cout << "Mounting static files from: " << static_path << '\n';
    if (!server.set_mount_point("/", static_path.string())) {
        std::cerr << "Could not mount the static directory\n";
        return 1;
    }

    server.Get("/", [static_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream stream(static_path / "index.html", std::ios::binary);
        if (!stream) {
            error_response(res, 404, "Index file not found");
            return;
        }
        std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());
        res.set_content(content, "text/html; charset=utf-8");
    });

    server.Post("/process", [static_path](const httplib::Request& req, httplib::Response& res) {
        if (!req.form.has_file("image")) {
            error_response(res, 400, "No image file provided");
            return;
        }

        int low = 50;
        int high = 150;
        int blur = 5;
        int animation = 1;
        std::string error;
        if (!parse_parameter(req, "low_threshold", 50, 0, 255, low, error) ||
            !parse_parameter(req, "high_threshold", 150, 0, 255, high, error) ||
            !parse_parameter(req, "blur", 5, 1, 15, blur, error) ||
            !parse_parameter(req, "animation", 1, 0, 1, animation, error)) {
            error_response(res, 400, error);
            return;
        }
        if (low > high) {
            error_response(res, 400, "low_threshold must not exceed high_threshold");
            return;
        }
        if (blur % 2 == 0) {
            error_response(res, 400, "blur must be an odd integer");
            return;
        }

        ImageData image;
        if (!decode_image(req.form.get_file("image").content, image, error)) {
            error_response(res, 400, error);
            return;
        }

        Utils::clear_timing_report();
        std::vector<Point> edges;
        {
            Utils::Timer timer("edge_detection", true, true);
            edges = EdgeDetector::detect_edges_from_memory(
                image.pixels.data(), image.width, image.height, image.channels,
                low, high, blur);
        }
        if (edges.empty()) {
            error_response(res, 422, "No edges were detected; try lower thresholds or a clearer image");
            return;
        }

        std::vector<Point> path;
        {
            Utils::Timer timer("path_planning", true, true);
            path = PathPlanner::plan_path(edges, image.width, image.height);
        }
        if (path.empty()) {
            error_response(res, 500, "Could not plan a path from the detected edges");
            return;
        }

        std::string thr_content;
        {
            Utils::Timer timer("thr_generation", true, true);
            thr_content = ThrGenerator::to_string(
                ThrGenerator::generate_thr(path, image.width, image.height));
        }

        if (thr_content.size() > 8U*1024*1024) {
            error_response(res, 413, "Generated THR exceeds the table's 8 MiB upload limit; use a simpler image"); return;
        }
        const auto thr_points = ThrGenerator::parse(thr_content);
        const std::string base_name = unique_base_name();
        if (!generate_assets(thr_points, animation != 0, static_path, base_name, error)) {
            error_response(res, 500, error);
            return;
        }

        Utils::get_timing_report().print();
        json response;
        response["thr"] = thr_content;
        if (animation) response["gif_url"] = "/" + base_name + ".gif";
        response["png_url"] = "/" + base_name + ".png";
        response["thumb_url"] = "/" + base_name + "_thumb.png";
        response["edges"] = path_json(edges);
        response["preview"] = json::array();
        response["point_count"] = thr_points.size();
        response["width"] = image.width;
        response["height"] = image.height;
        response["timing"] = timing_json();
        res.set_content(response.dump(), "application/json");
    });

    server.Post("/process_thr", [static_path](const httplib::Request& req, httplib::Response& res) {
        if (!req.form.has_file("thr")) {
            error_response(res, 400, "No THR file provided");
            return;
        }
        const std::string& content = req.form.get_file("thr").content;
        if (content.empty() || content.size() > 8U*1024*1024) {
            error_response(res, 400, "The THR file must be nonempty and no larger than 8 MiB");
            return;
        }

        std::vector<ThrPoint> thr_points;
        try { thr_points = ThrGenerator::parse(content); }
        catch (const std::exception& e) { error_response(res, 400, e.what()); return; }
        if (thr_points.empty() || thr_points.size() > kMaxThrPoints) {
            error_response(res, 400, "The THR file has no valid points or too many points");
            return;
        }

        constexpr int size = 800;
        Utils::clear_timing_report();
        std::string error;
        int animation = 1;
        if (!parse_parameter(req, "animation", 1, 0, 1, animation, error)) {
            error_response(res, 400, error); return;
        }
        const std::string base_name = unique_base_name();
        if (!generate_assets(thr_points, animation != 0, static_path, base_name, error)) {
            error_response(res, 500, error);
            return;
        }

        json response;
        response["thr"] = content;
        if (animation) response["gif_url"] = "/" + base_name + ".gif";
        response["png_url"] = "/" + base_name + ".png";
        response["thumb_url"] = "/" + base_name + "_thumb.png";
        response["preview"] = json::array();
        response["point_count"] = thr_points.size();
        response["edges"] = json::array();
        response["width"] = size;
        response["height"] = size;
        response["timing"] = timing_json();
        res.set_content(response.dump(), "application/json");
    });

    // httplib's defaults include SO_REUSEPORT, which lets a second instance
    // silently share the port; drop it so a conflict falls back to a free port.
    server.set_socket_options([](socket_t sock) {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&yes), sizeof(yes));
    });
    int port = 8080;
    if (!server.bind_to_port("0.0.0.0", port)) {
        server.stop();  // a failed bind decommissions the server; stop() re-arms it
        port = server.bind_to_any_port("0.0.0.0");
        if (port < 0) {
            std::cerr << "Could not bind to any port\n";
            return 1;
        }
        std::cerr << "Port 8080 is in use; picked a free port instead\n";
    }
    std::cout << "Server started at http://localhost:" << port << std::endl;
    if (!server.listen_after_bind()) {
        std::cerr << "Could not listen on port " << port << '\n';
        return 1;
    }
    return 0;
}
