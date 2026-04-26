#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "collision.h"

int main() {
  using collision::Body;

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
      collision::bounce_in_world(body, kWorldWidth, kWorldHeight);
    }

    total_collisions +=
        collision::detect_collisions_sap(bodies, sweep_order, total_narrow_checks);
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
