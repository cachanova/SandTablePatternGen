#include "EdgeDetector.h"
#include "ThrPreview.h"
#include "PathPlanner.h"
#include "ThrGenerator.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kMaxInputDimension = 2048;
constexpr int kMaxOutputDimension = 8192;

struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> pixels;
};

std::string shell_quote(const fs::path& path) {
    std::string quoted = "'";
    for (char c : path.string()) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    return quoted + "'";
}

fs::path temporary_png_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("thrgen_cli_" + std::to_string(stamp) + ".png");
}

bool load_image(const fs::path& input, ImageData& image, std::string& error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* raw = stbi_load(input.string().c_str(), &width, &height, &channels, 0);
    fs::path converted;

    if (!raw) {
        converted = temporary_png_path();
        const std::string command = "magick " + shell_quote(input) +
                                    " -resize '2048x2048>' -strip " + shell_quote(converted);
        if (std::system(command.c_str()) != 0) {
            error = "Could not load image: " + input.string();
            return false;
        }
        raw = stbi_load(converted.string().c_str(), &width, &height, &channels, 0);
    }

    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> owned(raw, stbi_image_free);
    std::error_code cleanup_error;
    if (!converted.empty()) fs::remove(converted, cleanup_error);

    if (!raw || width <= 0 || height <= 0 || channels <= 0 || channels > 4) {
        error = "Could not decode image: " + input.string();
        return false;
    }

    image.width = width;
    image.height = height;
    image.channels = channels;
    if (width > kMaxInputDimension || height > kMaxInputDimension) {
        const double ratio = std::min(static_cast<double>(kMaxInputDimension) / width,
                                      static_cast<double>(kMaxInputDimension) / height);
        image.width = std::max(1, static_cast<int>(width * ratio));
        image.height = std::max(1, static_cast<int>(height * ratio));
        image.pixels = EdgeDetector::resize(raw, width, height, channels,
                                            image.width, image.height);
    } else {
        const size_t byte_count = static_cast<size_t>(width) * height * channels;
        image.pixels.assign(raw, raw + byte_count);
    }

    if (image.pixels.empty()) {
        error = "Could not resize image: " + input.string();
        return false;
    }
    return true;
}

bool write_points_png(const std::vector<Point>& points, int width, int height,
                      const std::string& output) {
    if (width <= 0 || height <= 0 || output.empty()) return false;
    std::vector<uint8_t> image(static_cast<size_t>(width) * height * 4, 0);
    for (const auto& point : points) {
        if (point.x < 0 || point.x >= width || point.y < 0 || point.y >= height) continue;
        const size_t index = (static_cast<size_t>(point.y) * width + point.x) * 4;
        image[index] = 255;
        image[index + 1] = 255;
        image[index + 2] = 255;
        image[index + 3] = 255;
    }
    return stbi_write_png(output.c_str(), width, height, 4, image.data(), width * 4) != 0;
}

bool read_file(const fs::path& path, std::string& content) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    content.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

bool write_file(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary);
    return stream && stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool parse_int(const std::string& text, int minimum, int maximum, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
           value >= minimum && value <= maximum;
}

bool option_value(int argc, char** argv, int& index, const std::string& option,
                  std::string& value) {
    if (index + 1 >= argc) {
        std::cerr << "Error: " << option << " requires a value\n";
        return false;
    }
    value = argv[++index];
    return true;
}

void print_usage(const char* name) {
    std::cout
        << "Usage:\n"
        << "  " << name << " gen <input_image> <output_thr> [options]\n"
        << "    --low <0-255>       Low edge threshold (default 50)\n"
        << "    --high <0-255>      High edge threshold (default 150)\n"
        << "    --blur <odd 1-15>   Blur kernel size (default 5)\n"
        << "    --gif <file>        Write an animated visualization\n"
        << "    --png <file>        Write an 800px transparent path preview\n"
        << "    --thumb <file>      Write a transparent thumbnail\n"
        << "    --thumb-size <n>    Thumbnail size, 2-8192 (default 150)\n"
        << "    --edges <file>      Write detected edge points\n\n"
        << "  " << name << " vis <input_thr> [options]\n"
        << "    --gif <file>        Write an animated visualization\n"
        << "    --png <file>        Write an 800px transparent path preview\n"
        << "    --thumb <file>      Write a transparent thumbnail\n"
        << "    --thumb-size <n>    Thumbnail size, 2-8192 (default 150)\n"
        << "    --edges <file>      Write path points at --size resolution\n"
        << "    --size <n>          Visualization resolution, 2-8192 (default 1024)\n\n"
        << "  " << name << " bench\n";
}

int run_benchmark() {
    fs::path test_directory = "tests/images";
    if (!fs::exists(test_directory)) test_directory = "../tests/images";
    if (!fs::is_directory(test_directory)) {
        std::cerr << "Error: Could not find tests/images\n";
        return 1;
    }

    std::cout << std::left << std::setw(25) << "Image"
              << std::setw(15) << "Resolution"
              << std::setw(15) << "Total Time" << '\n'
              << std::string(55, '-') << '\n';

    for (const auto& entry : fs::directory_iterator(test_directory)) {
        if (!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".jpg" && extension != ".jpeg" && extension != ".png" &&
            extension != ".webp") continue;

        ImageData image;
        std::string error;
        if (!load_image(entry.path(), image, error)) {
            std::cerr << "Skipping " << entry.path().filename() << ": " << error << '\n';
            continue;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto edges = EdgeDetector::detect_edges_from_memory(
            image.pixels.data(), image.width, image.height, image.channels, 50, 150, 5);
        const auto path = PathPlanner::plan_path(edges, image.width, image.height);
        const auto thr = ThrGenerator::parse(ThrGenerator::to_string(
            ThrGenerator::generate_thr(path, image.width, image.height)));
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
        (void)thr;

        const std::string resolution = std::to_string(image.width) + "x" + std::to_string(image.height);
        std::cout << std::left << std::setw(25) << entry.path().filename().string()
                  << std::setw(15) << resolution << std::fixed << std::setprecision(3)
                  << std::setw(15) << elapsed.count() << "s\n";
    }
    return 0;
}

} // namespace

int run(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string mode = argv[1];
    if (mode == "bench") return run_benchmark();
    if (mode != "gen" && mode != "vis") {
        std::cerr << "Error: Unknown command: " << mode << '\n';
        print_usage(argv[0]);
        return 1;
    }
    if ((mode == "gen" && argc < 4) || (mode == "vis" && argc < 3)) {
        print_usage(argv[0]);
        return 1;
    }

    int low = 50;
    int high = 150;
    int blur = 5;
    int size = 1024;
    int thumb_size = 128;
    std::string gif_output;
    std::string png_output;
    std::string thumb_output;
    std::string edges_output;

    const int first_option = mode == "gen" ? 4 : 3;
    for (int i = first_option; i < argc; ++i) {
        const std::string option = argv[i];
        if ((mode == "gen" && option == "--size") ||
            (mode == "vis" && (option == "--low" || option == "--high" || option == "--blur"))) {
            std::cerr << "Error: " << option << " is not valid for the " << mode << " command\n";
            return 1;
        }
        std::string value;
        if (option == "--gif" || option == "--png" || option == "--thumb" ||
            option == "--edges") {
            if (!option_value(argc, argv, i, option, value)) return 1;
            if (option == "--gif") gif_output = value;
            else if (option == "--png") png_output = value;
            else if (option == "--thumb") thumb_output = value;
            else edges_output = value;
        } else if (option == "--low" || option == "--high" || option == "--blur" ||
                   option == "--size" || option == "--thumb-size") {
            if (!option_value(argc, argv, i, option, value)) return 1;
            int* destination = nullptr;
            int minimum = 0;
            int maximum = 255;
            if (option == "--low") destination = &low;
            else if (option == "--high") destination = &high;
            else if (option == "--blur") {
                destination = &blur;
                minimum = 1;
                maximum = 15;
            } else if (option == "--size") {
                destination = &size;
                minimum = 2;
                maximum = kMaxOutputDimension;
            } else {
                destination = &thumb_size;
                minimum = 2;
                maximum = kMaxOutputDimension;
            }
            if (!parse_int(value, minimum, maximum, *destination)) {
                std::cerr << "Error: Invalid value for " << option << ": " << value << '\n';
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << option << '\n';
            return 1;
        }
    }

    if (mode == "gen") {
        if (low > high) {
            std::cerr << "Error: --low must not exceed --high\n";
            return 1;
        }
        if (blur % 2 == 0) {
            std::cerr << "Error: --blur must be odd\n";
            return 1;
        }

        ImageData image;
        std::string error;
        if (!load_image(argv[2], image, error)) {
            std::cerr << "Error: " << error << '\n';
            return 1;
        }
        const auto edges = EdgeDetector::detect_edges_from_memory(
            image.pixels.data(), image.width, image.height, image.channels,
            low, high, blur);
        if (edges.empty()) {
            std::cerr << "Error: No edges detected; try lower thresholds or a clearer image\n";
            return 2;
        }
        const auto path = PathPlanner::plan_path(edges, image.width, image.height);
        const auto thr = ThrGenerator::parse(ThrGenerator::to_string(
            ThrGenerator::generate_thr(path, image.width, image.height)));
        if (path.empty() || !write_file(argv[3], ThrGenerator::to_string(thr))) {
            std::cerr << "Error: Could not write THR file: " << argv[3] << '\n';
            return 1;
        }

        bool ok = true;
        if (!edges_output.empty()) ok &= write_points_png(edges, image.width, image.height, edges_output);
        if (!gif_output.empty()) ok &= ThrPreview::gif(thr, gif_output);
        if (!png_output.empty()) ok &= ThrPreview::png(thr, png_output);
        if (!thumb_output.empty()) {
            ok &= ThrPreview::png(thr, thumb_output, thumb_size);
        }
        if (!ok) {
            std::cerr << "Error: One or more visualization files could not be written\n";
            return 1;
        }
        std::cout << "Wrote " << argv[3] << " with " << thr.size() << " points\n";
        return 0;
    }

    if (gif_output.empty() && png_output.empty() && thumb_output.empty() && edges_output.empty()) {
        std::cerr << "Error: Specify at least one output format\n";
        return 1;
    }

    std::string content;
    if (!read_file(argv[2], content)) {
        std::cerr << "Error: Could not read THR file: " << argv[2] << '\n';
        return 1;
    }
    const auto thr = ThrGenerator::parse(content);
    if (thr.empty()) {
        std::cerr << "Error: THR file contains no valid points\n";
        return 1;
    }

    bool ok = true;
    if (!gif_output.empty()) ok &= ThrPreview::gif(thr, gif_output, std::min(size,1024));
    if (!png_output.empty()) ok &= ThrPreview::png(thr, png_output);
    if (!thumb_output.empty()) ok &= ThrPreview::png(thr, thumb_output, thumb_size);
    if (!edges_output.empty()) ok &= ThrPreview::png(thr, edges_output, size);
    if (!ok) {
        std::cerr << "Error: One or more visualization files could not be written\n";
        return 1;
    }
    std::cout << "Rendered " << thr.size() << " THR points\n";
    return 0;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
