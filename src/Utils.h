#pragma once

#include <vector>
#include <thread>
#include <future>
#include <algorithm>
#include <functional>
#include <map>
#include <mutex>

#include <iostream>
#include <chrono>

namespace Utils {

    inline std::once_flag thread_count_log_flag;

    // Timing report structure for collecting stage timings
    struct TimingReport {
        std::map<std::string, double> stages;
        void record(const std::string& stage, double ms) {
            stages[stage] = ms;
        }

        double total_ms() const {
            double sum = 0;
            for (const auto& p : stages) sum += p.second;
            return sum;
        }

        void print() const {
            std::cout << "\n[TIMING REPORT]" << std::endl;
            for (const auto& p : stages) {
                std::cout << "  " << p.first << ": " << p.second << " ms" << std::endl;
            }
            std::cout << "  TOTAL: " << total_ms() << " ms\n" << std::endl;
        }
    };

    // Each HTTP request is handled on one worker thread. Keeping the report
    // thread-local prevents concurrent requests from clearing each other's data.
    inline TimingReport& get_timing_report() {
        thread_local TimingReport report;
        return report;
    }

    inline void clear_timing_report() {
        get_timing_report().stages.clear();
    }

    class Timer {
    public:
        Timer(const std::string& name, bool log_to_console = true, bool record_to_report = false)
            : name_(name), log_to_console_(log_to_console), record_to_report_(record_to_report),
              start_(std::chrono::high_resolution_clock::now()) {}

        ~Timer() {
            stop();
        }

        double stop() {
            if (stopped_) return elapsed_ms_;
            stopped_ = true;

            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> diff = end - start_;
            elapsed_ms_ = diff.count();

            if (log_to_console_) {
                std::cout << "[TIMER] " << name_ << ": " << elapsed_ms_ << " ms" << std::endl;
            }

            if (record_to_report_) {
                get_timing_report().record(name_, elapsed_ms_);
            }

            return elapsed_ms_;
        }

        double elapsed_ms() const {
            if (stopped_) return elapsed_ms_;
            auto now = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> diff = now - start_;
            return diff.count();
        }

    private:
        std::string name_;
        bool log_to_console_;
        bool record_to_report_;
        bool stopped_ = false;
        double elapsed_ms_ = 0;
        std::chrono::time_point<std::chrono::high_resolution_clock> start_;
    };

    template <typename Index, typename Func>
    void parallel_for(Index start, Index end, Func&& f) {
        if (end <= start) return;

        unsigned int num_threads = std::thread::hardware_concurrency();

        if (num_threads == 0) {
            num_threads = 2;
            std::call_once(thread_count_log_flag, [] {
                std::cerr << "Warning: std::thread::hardware_concurrency() failed to detect cores. Falling back to 2 threads." << std::endl;
            });
        } else {
            std::call_once(thread_count_log_flag, [num_threads] {
                std::cout << "Parallelizing across " << num_threads << " logical cores." << std::endl;
            });
        }

        Index range = end - start;
        if (range < static_cast<Index>(num_threads)) {
            num_threads = static_cast<unsigned int>(range);
        }

        std::vector<std::future<void>> futures;
        Index chunk_size = range / num_threads;
        Index remainder = range % num_threads;

        Index current_start = start;

        for (unsigned int i = 0; i < num_threads; ++i) {
            Index current_chunk = chunk_size + (static_cast<Index>(i) < remainder ? 1 : 0);
            Index current_end = current_start + current_chunk;

            futures.push_back(std::async(std::launch::async, [current_start, current_end, &f]() {
                for (Index j = current_start; j < current_end; ++j) {
                    f(j);
                }
            }));

            current_start = current_end;
        }

        for (auto& fut : futures) {
            if (fut.valid()) {
                fut.get();
            }
        }
    }

}
