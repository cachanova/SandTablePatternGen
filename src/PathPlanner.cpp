#include "PathPlanner.h"
#include "Utils.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <queue>
#include <map>
#include <tuple>

struct Component {
    int id;
    std::vector<Point> points;
    int min_x, min_y, max_x, max_y;
};

static inline double dist_sq(Point p1, Point p2) {
    double dx = (double)p1.x - p2.x;
    double dy = (double)p1.y - p2.y;
    return dx * dx + dy * dy;
}

std::vector<Point> PathPlanner::plan_path(const std::vector<Point>& input_points, int width, int height) {
    if (input_points.empty() || width <= 0 || height <= 0) return {};

    const size_t cell_count = static_cast<size_t>(width) * height;
    std::vector<int> point_idx_map(cell_count, -1);
    std::vector<Point> points;
    points.reserve(input_points.size());
    for (const auto& p : input_points) {
        if (p.x < 0 || p.x >= width || p.y < 0 || p.y >= height) continue;
        const size_t idx = static_cast<size_t>(p.y) * width + p.x;
        if (point_idx_map[idx] == -1) {
            point_idx_map[idx] = static_cast<int>(points.size());
            points.push_back(p);
        }
    }
    if (points.empty()) return {};

    // Identify connected components. This pass is linear in the point count;
    // keeping it sequential avoids data races in union-find parent traversal.
    std::vector<int> dsu_parent(points.size());
    for (int i = 0; i < static_cast<int>(points.size()); ++i) dsu_parent[i] = i;

    auto dsu_find = [&](int i) {
        while (dsu_parent[i] != i) i = dsu_parent[i];
        return i;
    };

    auto dsu_unite = [&](int i, int j) {
        int root_i = dsu_find(i);
        int root_j = dsu_find(j);
        if (root_i != root_j) dsu_parent[root_i] = root_j;
    };

    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const auto& p = points[i];
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = p.x + dx, ny = p.y + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    int neighbor_idx = point_idx_map[ny * width + nx];
                    if (neighbor_idx != -1 && neighbor_idx > i) {
                        dsu_unite(i, neighbor_idx);
                    }
                }
            }
        }
    }

    std::map<int, std::vector<Point>> component_map;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        int root = dsu_find(i);
        component_map[root].push_back(points[i]);
    }

    std::vector<Component> components;
    int next_comp_id = 0;
    for (auto& pair : component_map) {
        Component comp;
        comp.id = next_comp_id++;
        comp.points = std::move(pair.second);
        comp.min_x = comp.max_x = comp.points[0].x;
        comp.min_y = comp.max_y = comp.points[0].y;
        for (const auto& p : comp.points) {
            comp.min_x = std::min(comp.min_x, p.x);
            comp.max_x = std::max(comp.max_x, p.x);
            comp.min_y = std::min(comp.min_y, p.y);
            comp.max_y = std::max(comp.max_y, p.y);
        }
        components.push_back(comp);
    }

    if (components.empty()) return {};

    // 2. Setup for traversal
    std::vector<uint8_t> has_point(cell_count, 0);
    std::vector<uint8_t> visited(cell_count, 0);
    std::vector<int> point_to_comp(cell_count, -1);
    std::vector<int> comp_remaining_counts(components.size(), 0);

    for (const auto& comp : components) {
        for (const auto& p : comp.points) {
            int idx = p.y * width + p.x;
            if (!has_point[idx]) {
                has_point[idx] = 1;
                point_to_comp[idx] = comp.id;
                comp_remaining_counts[comp.id]++;
            }
        }
    }

    std::vector<int> active_comp_indices;
    for(size_t i=0; i<components.size(); ++i) {
        if (comp_remaining_counts[i] > 0) active_comp_indices.push_back(static_cast<int>(i));
    }

    int cx = width / 2;
    int cy = height / 2;

    size_t remaining = points.size();
    Point curr_p = { cx, cy };
    std::vector<Point> path;
    path.push_back(curr_p);
    std::vector<uint8_t> is_in_path(cell_count, 0);
    is_in_path[curr_p.y * width + curr_p.x] = 1;

    const int GRID_SIZE = 16;
    int grid_cols = (width + GRID_SIZE - 1) / GRID_SIZE;
    int grid_rows = (height + GRID_SIZE - 1) / GRID_SIZE;
    std::vector<std::vector<Point>> spatial_grid(static_cast<size_t>(grid_cols) * grid_rows);
    spatial_grid[(curr_p.y / GRID_SIZE) * grid_cols + (curr_p.x / GRID_SIZE)].push_back(curr_p);

    auto mark_visited = [&](int x, int y) {
        int idx = y * width + x;
        if (has_point[idx] && !visited[idx]) {
            visited[idx] = 1;
            remaining--;
            int cid = point_to_comp[idx];
            if (cid != -1) comp_remaining_counts[cid]--;
        }
        if (!is_in_path[idx]) {
            is_in_path[idx] = 1;
            spatial_grid[(y / GRID_SIZE) * grid_cols + (x / GRID_SIZE)].push_back({x, y});
        }
    };

    auto add_straight = [&](Point start, Point end) {
        if (start.x == end.x && start.y == end.y) return;
        int x0 = start.x, y0 = start.y, x1 = end.x, y1 = end.y;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        while (true) {
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            if (x0 == x1 && y0 == y1) break;
            path.push_back({x0, y0});
            mark_visited(x0, y0);
        }
    };

    // Strategy 1: Start in the center, go to the nearest point on an edge
    {
        double min_d = std::numeric_limits<double>::max();
        int best_idx = -1;
        for(size_t i=0; i<points.size(); ++i) {
            double d = dist_sq(curr_p, points[i]);
            if (d < min_d) { min_d = d; best_idx = static_cast<int>(i); }
        }
        if (best_idx != -1) {
            Point target = points[best_idx];
            add_straight(curr_p, target);
            curr_p = target;
            path.push_back(curr_p);
            mark_visited(curr_p.x, curr_p.y);
        }
    }

    int s3_count = 0;
    std::vector<int> parent(cell_count, -1);
    while (remaining > 0) {
        Point next_p = {-1, -1};
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = curr_p.x + dx, ny = curr_p.y + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    int idx = ny * width + nx;
                    if (has_point[idx] && !visited[idx]) {
                        next_p = {nx, ny};
                        goto found_point;
                    }
                }
            }
        }

        // Strategy 3: Global search
        {
            s3_count++;
            Point best_v = {-1, -1};
            Point best_u = {-1, -1};

            struct Candidate {
                Point unvisited{-1, -1};
                Point visited{-1, -1};
                double distance_sq = std::numeric_limits<double>::max();
            };
            std::vector<Candidate> candidates(active_comp_indices.size());

            auto is_better = [](double distance, Point unvisited, Point visited,
                                const Candidate& current) {
                if (distance != current.distance_sq) return distance < current.distance_sq;
                return std::tie(unvisited.y, unvisited.x, visited.y, visited.x) <
                       std::tie(current.unvisited.y, current.unvisited.x,
                                current.visited.y, current.visited.x);
            };

            Utils::parallel_for(0, (int)active_comp_indices.size(), [&](int i) {
                int c_idx = active_comp_indices[i];
                if (comp_remaining_counts[c_idx] == 0) return;
                const auto& comp = components[c_idx];
                Candidate& component_best = candidates[i];
                
                size_t step = 1;
                if (comp.points.size() > 100) step = comp.points.size() / 10;
                else if (comp.points.size() > 20) step = 4;

                auto consider = [&](const Point& u) {
                    if (visited[u.y * width + u.x]) return;

                    int ux = u.x / GRID_SIZE;
                    int uy = u.y / GRID_SIZE;
                    double local_min_dist_sq = std::numeric_limits<double>::max();
                    Point local_best_v = {-1, -1};

                    bool found_in_radius = false;
                    for (int r = 0; r < std::max(grid_cols, grid_rows); ++r) {
                        if (found_in_radius && r > (int)(std::sqrt(local_min_dist_sq)/GRID_SIZE) + 1) break;

                        for (int dy = -r; dy <= r; ++dy) {
                            for (int dx = -r; dx <= r; ++dx) {
                                if (std::abs(dx) < r && std::abs(dy) < r) continue;
                                int gx = ux + dx, gy = uy + dy;
                                if (gx >= 0 && gx < grid_cols && gy >= 0 && gy < grid_rows) {
                                    for (const auto& v : spatial_grid[gy * grid_cols + gx]) {
                                        double d2 = (double)(u.x-v.x)*(u.x-v.x) + (double)(u.y-v.y)*(u.y-v.y);
                                        if (d2 < local_min_dist_sq ||
                                            (d2 == local_min_dist_sq &&
                                             std::tie(v.y, v.x) < std::tie(local_best_v.y, local_best_v.x))) {
                                            local_min_dist_sq = d2;
                                            local_best_v = v;
                                            found_in_radius = true;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (local_best_v.x != -1 &&
                        is_better(local_min_dist_sq, u, local_best_v, component_best)) {
                        component_best = {u, local_best_v, local_min_dist_sq};
                    }
                };

                bool considered_unvisited = false;
                for (size_t point_index = 0; point_index < comp.points.size(); point_index += step) {
                    const auto& u = comp.points[point_index];
                    if (visited[u.y * width + u.x]) continue;
                    considered_unvisited = true;
                    consider(u);
                }
                // Sampling must not make the last unsampled points unreachable.
                if (!considered_unvisited) {
                    for (const auto& u : comp.points) {
                        if (!visited[u.y * width + u.x]) {
                            consider(u);
                            break;
                        }
                    }
                }
            });

            Candidate best;
            for (const auto& candidate : candidates) {
                if (candidate.unvisited.x != -1 &&
                    is_better(candidate.distance_sq, candidate.unvisited,
                              candidate.visited, best)) {
                    best = candidate;
                }
            }
            best_u = best.unvisited;
            best_v = best.visited;

            if (best_u.x != -1) {
                if (s3_count % 10 == 0) {
                    active_comp_indices.erase(
                        std::remove_if(active_comp_indices.begin(), active_comp_indices.end(),
                            [&](int cid) { return comp_remaining_counts[cid] == 0; }),
                        active_comp_indices.end()
                    );
                }

                if (curr_p.x != best_v.x || curr_p.y != best_v.y) {
                    std::queue<Point> q;
                    q.push(curr_p);
                    
                    std::fill(parent.begin(), parent.end(), -1);
                    
                    parent[curr_p.y * width + curr_p.x] = -2;
                    bool found = false;
                    while(!q.empty()) {
                        Point c = q.front(); q.pop();
                        if (c.x == best_v.x && c.y == best_v.y) { found = true; break; }
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                int nx = c.x + dx, ny = c.y + dy;
                                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                                    int nidx = ny * width + nx;
                                    if (is_in_path[nidx] && parent[nidx] == -1) {
                                        parent[nidx] = c.y * width + c.x;
                                        q.push({nx, ny});
                                    }
                                }
                            }
                        }
                    }
                    if (found) {
                        std::vector<Point> trace;
                        int cur = best_v.y * width + best_v.x;
                        while(cur != -2) {
                            trace.push_back({cur % width, cur / width});
                            cur = parent[cur];
                        }
                        std::reverse(trace.begin(), trace.end());
                        for(size_t i=1; i<trace.size(); ++i) {
                            path.push_back(trace[i]);
                            mark_visited(trace[i].x, trace[i].y);
                        }
                        curr_p = best_v;
                    }
                }

                add_straight(curr_p, best_u);
                next_p = best_u;
                goto found_point;
            }
        }
        break;

    found_point:
        mark_visited(next_p.x, next_p.y);
        curr_p = next_p;
        path.push_back(curr_p);
    }

    return path;
}
