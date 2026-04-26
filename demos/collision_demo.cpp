#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }

Vec2 operator*(const Vec2 &v, float s) { return {v.x * s, v.y * s}; }

struct Body {
  std::string name;
  Vec2 position;
  Vec2 velocity;
  Vec2 half_extents;
};

bool aabb_aabb_collide(const Body &a, const Body &b) {
  const bool overlap_x = std::abs(a.position.x - b.position.x) <=
                         (a.half_extents.x + b.half_extents.x);
  const bool overlap_y = std::abs(a.position.y - b.position.y) <=
                         (a.half_extents.y + b.half_extents.y);
  return overlap_x && overlap_y;
}

void bounce_in_world(Body &body, float world_w, float world_h) {
  float min_x = 0.0F;
  float max_x = world_w;
  float min_y = 0.0F;
  float max_y = world_h;

  min_x += body.half_extents.x;
  max_x -= body.half_extents.x;
  min_y += body.half_extents.y;
  max_y -= body.half_extents.y;

  if (body.position.x < min_x) {
    body.position.x = min_x;
    body.velocity.x *= -1.0F;
  } else if (body.position.x > max_x) {
    body.position.x = max_x;
    body.velocity.x *= -1.0F;
  }

  if (body.position.y < min_y) {
    body.position.y = min_y;
    body.velocity.y *= -1.0F;
  } else if (body.position.y > max_y) {
    body.position.y = max_y;
    body.velocity.y *= -1.0F;
  }
}

float left_edge(const Body &body) {
  return body.position.x - body.half_extents.x;
}

float right_edge(const Body &body) {
  return body.position.x + body.half_extents.x;
}

std::size_t detect_collisions_sap(const std::vector<Body> &bodies,
                                  std::vector<int> &order,
                                  std::size_t &narrow_checks) {
  // Bodies move linearly with small per-frame displacement; insertion sort
  // exploits this temporal coherence and is close to O(n) on average.
  for (std::size_t i = 1; i < order.size(); ++i) {
    std::size_t j = i;
    while (j > 0 &&
           left_edge(bodies[order[j]]) < left_edge(bodies[order[j - 1]])) {
      std::swap(order[j], order[j - 1]);
      --j;
    }
  }

  std::vector<int> active;
  active.reserve(order.size());
  std::size_t collisions = 0;

  for (int idx : order) {
    const float current_left = left_edge(bodies[idx]);

    std::size_t write = 0;
    for (std::size_t read = 0; read < active.size(); ++read) {
      if (right_edge(bodies[active[read]]) >= current_left) {
        active[write++] = active[read];
      }
    }
    active.resize(write);

    for (int other : active) {
      ++narrow_checks;
      if (aabb_aabb_collide(bodies[idx], bodies[other])) {
        ++collisions;
      }
    }

    active.push_back(idx);
  }

  return collisions;
}

} // namespace

int main() {
  constexpr float kWorldWidth = 100.0F;
  constexpr float kWorldHeight = 60.0F;
  constexpr float kDt = 0.1F;
  constexpr int kFrameCount = 800;
  constexpr int kBodyCount = 200;

  std::mt19937 rng(20260411U);
  std::uniform_real_distribution<float> half_x_dist(1.0F, 2.8F);
  std::uniform_real_distribution<float> half_y_dist(1.0F, 2.2F);
  std::uniform_real_distribution<float> vel_dist(-14.0F, 14.0F);

  std::vector<Body> bodies;
  bodies.reserve(kBodyCount);
  for (int i = 0; i < kBodyCount; ++i) {
    const float hx = half_x_dist(rng);
    const float hy = half_y_dist(rng);

    std::uniform_real_distribution<float> pos_x_dist(hx, kWorldWidth - hx);
    std::uniform_real_distribution<float> pos_y_dist(hy, kWorldHeight - hy);

    float vx = vel_dist(rng);
    float vy = vel_dist(rng);
    if (std::abs(vx) < 2.0F) {
      vx = (vx >= 0.0F) ? 2.0F : -2.0F;
    }
    if (std::abs(vy) < 2.0F) {
      vy = (vy >= 0.0F) ? 2.0F : -2.0F;
    }

    bodies.push_back(Body{
        "Rect_" + std::to_string(i),
        {pos_x_dist(rng), pos_y_dist(rng)},
        {vx, vy},
        {hx, hy},
    });
  }

  std::vector<int> sweep_order(static_cast<std::size_t>(kBodyCount));
  std::iota(sweep_order.begin(), sweep_order.end(), 0);

  std::size_t total_narrow_checks = 0;
  std::size_t total_collisions = 0;

  std::cout << "Collision Detection Demo\n";
  std::cout << "World: " << kWorldWidth << " x " << kWorldHeight << '\n';
  std::cout << "Bodies: " << bodies.size()
            << " (axis-aligned rectangles, AABB-AABB)\n\n";

  for (int frame = 0; frame < kFrameCount; ++frame) {
    for (auto &body : bodies) {
      body.position = body.position + body.velocity * kDt;
      bounce_in_world(body, kWorldWidth, kWorldHeight);
    }

    total_collisions +=
        detect_collisions_sap(bodies, sweep_order, total_narrow_checks);
  }

  const std::size_t brute_force_per_frame =
      static_cast<std::size_t>(kBodyCount) * (kBodyCount - 1) / 2;
  const std::size_t brute_force_total =
      brute_force_per_frame * static_cast<std::size_t>(kFrameCount);
  const double kept_ratio = (brute_force_total == 0)
                                ? 0.0
                                : static_cast<double>(total_narrow_checks) /
                                      static_cast<double>(brute_force_total);

  std::cout << "Frames: " << kFrameCount << '\n';
  std::cout << "Total collisions found: " << total_collisions << '\n';
  std::cout << "Brute-force checks: " << brute_force_total << '\n';
  std::cout << "SAP narrow-phase checks: " << total_narrow_checks << '\n';
  std::cout << std::fixed << std::setprecision(2)
            << "Checks kept: " << (kept_ratio * 100.0) << "% (reduced "
            << ((1.0 - kept_ratio) * 100.0) << "%)\n";

  return 0;
}
