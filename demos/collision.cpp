#include "collision.h"

#include <algorithm>
#include <cmath>

namespace {

float left_edge(const collision::Body &body) {
  return body.position.x - body.half_extents.x;
}

float right_edge(const collision::Body &body) {
  return body.position.x + body.half_extents.x;
}

} // namespace

namespace collision {

Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }

Vec2 operator*(const Vec2 &v, float s) { return {v.x * s, v.y * s}; }

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

} // namespace collision
