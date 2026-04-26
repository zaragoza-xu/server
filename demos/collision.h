#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace collision {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

Vec2 operator+(const Vec2 &a, const Vec2 &b);
Vec2 operator*(const Vec2 &v, float s);

struct Body {
  std::string name;
  Vec2 position;
  Vec2 velocity;
  Vec2 half_extents;
};

bool aabb_aabb_collide(const Body &a, const Body &b);

void bounce_in_world(Body &body, float world_w, float world_h);

std::size_t detect_collisions_sap(const std::vector<Body> &bodies,
                                  std::vector<int> &order,
                                  std::size_t &narrow_checks);

} // namespace collision
