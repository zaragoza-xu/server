#include <gtest/gtest.h>

#include <numeric>
#include <string>
#include <vector>

#include "collision.h"

namespace
{
  using collision::aabb_aabb_collide;
  using collision::Body;
  using collision::bounce_in_world;
  using collision::detect_collisions_sap;
  using collision::Vec2;

  class AabbAabbCollideTest : public ::testing::Test
  {
  protected:
    Body a;
    Body b;
  };

  class BounceInWorldTest : public ::testing::Test
  {
  protected:
    Body body;

    float world_w = 100.0F;
    float world_h = 60.0F;
  };

  class DetectCollisionsSapTest : public ::testing::Test
  {
  protected:
    float world_w = 100.0F;
    float world_h = 60.0F;
    float dt = 0.1F;
    std::vector<Body> bodies;
    std::vector<int> order;
    std::size_t narrow_checks = 0;

    void SetUp() override
    {
      bodies.clear();
      order.clear();
      narrow_checks = 0;
    }

    void TearDown() override
    {
      bodies.clear();
      order.clear();
      narrow_checks = 0;
    }

    void BuildOrderFromBodies()
    {
      order.resize(static_cast<int>(bodies.size()));
      std::iota(order.begin(), order.end(), 0);
    }
  };

  enum class AxisRelation
  {
    Separate,
    Touching,
    Overlapping,
  };

  struct SapTwoBodyCase
  {
    AxisRelation x_relation;
    AxisRelation y_relation;
    std::size_t expected_collisions;
  };

  const char *AxisRelationToName(AxisRelation relation)
  {
    switch (relation)
    {
    case AxisRelation::Separate:
      return "Separate";
    case AxisRelation::Touching:
      return "Touching";
    case AxisRelation::Overlapping:
      return "Overlapping";
    }
    return "Unknown";
  }

  class DetectCollisionsSapParamTest
      : public DetectCollisionsSapTest,
        public ::testing::WithParamInterface<SapTwoBodyCase>
  {
  protected:
    static float CoordFor(AxisRelation relation)
    {
      switch (relation)
      {
      case AxisRelation::Separate:
        return 3.0F;
      case AxisRelation::Touching:
        return 2.0F;
      case AxisRelation::Overlapping:
        return 1.5F;
      }
      return 3.0F;
    }
  };

  struct BounceCase
  {
    const char *test_name;
    Vec2 initial_position;
    Vec2 initial_velocity;
    Vec2 expected_position;
    Vec2 expected_velocity;
  };

  class BounceInWorldParamTest : public BounceInWorldTest,
                                 public ::testing::WithParamInterface<BounceCase>
  {
  };

  TEST_F(AabbAabbCollideTest, Separate_False)
  {
    a = Body{"A", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};
    b = Body{"B", Vec2{3.1F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};

    EXPECT_FALSE(aabb_aabb_collide(a, b));
    EXPECT_FALSE(aabb_aabb_collide(b, a));
  }

  TEST_F(AabbAabbCollideTest, Touching_True)
  {
    a = Body{"A", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};
    b = Body{"B", Vec2{2.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};

    EXPECT_TRUE(aabb_aabb_collide(a, b));
    EXPECT_TRUE(aabb_aabb_collide(b, a));
  }

  TEST_F(AabbAabbCollideTest, Overlapping_True)
  {
    a = Body{"A", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};
    b = Body{"B", Vec2{1.5F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}};

    EXPECT_TRUE(aabb_aabb_collide(a, b));
    EXPECT_TRUE(aabb_aabb_collide(b, a));
  }

  TEST_P(BounceInWorldParamTest, BoundaryAndCornerCases)
  {
    const auto &param = GetParam();
    body = Body{"A", param.initial_position, param.initial_velocity, Vec2{1.0F, 1.0F}};

    bounce_in_world(body, world_w, world_h);

    EXPECT_FLOAT_EQ(body.position.x, param.expected_position.x);
    EXPECT_FLOAT_EQ(body.position.y, param.expected_position.y);
    EXPECT_FLOAT_EQ(body.velocity.x, param.expected_velocity.x);
    EXPECT_FLOAT_EQ(body.velocity.y, param.expected_velocity.y);
  }

  INSTANTIATE_TEST_SUITE_P(BounceInWorldCases, BounceInWorldParamTest,
                           ::testing::Values(
          BounceCase{"Center", Vec2{50.0F, 30.0F}, Vec2{5.0F, 2.0F},
                     Vec2{50.0F, 30.0F}, Vec2{5.0F, 2.0F}},
          BounceCase{"LeftEdge", Vec2{0.5F, 30.0F}, Vec2{-5.0F, 2.0F},
                     Vec2{1.0F, 30.0F}, Vec2{5.0F, 2.0F}},
          BounceCase{"RightEdge", Vec2{99.5F, 30.0F}, Vec2{7.0F, -1.0F},
                     Vec2{99.0F, 30.0F}, Vec2{-7.0F, -1.0F}},
          BounceCase{"UpEdge", Vec2{40.0F, 0.5F}, Vec2{3.0F, -4.0F},
                     Vec2{40.0F, 1.0F}, Vec2{3.0F, 4.0F}},
          BounceCase{"DownEdge", Vec2{40.0F, 59.5F}, Vec2{-2.0F, 6.0F},
                     Vec2{40.0F, 59.0F}, Vec2{-2.0F, -6.0F}},
          BounceCase{"LeftUpCorner", Vec2{0.5F, 0.5F}, Vec2{-1.0F, -2.0F},
                     Vec2{1.0F, 1.0F}, Vec2{1.0F, 2.0F}},
          BounceCase{"LeftDownCorner", Vec2{0.5F, 59.5F}, Vec2{-1.5F, 2.5F},
                     Vec2{1.0F, 59.0F}, Vec2{1.5F, -2.5F}},
          BounceCase{"RightUpCorner", Vec2{99.5F, 0.5F}, Vec2{4.0F, -3.0F},
                     Vec2{99.0F, 1.0F}, Vec2{-4.0F, 3.0F}},
          BounceCase{"RightDownCorner", Vec2{99.5F, 59.5F}, Vec2{8.0F, 9.0F},
                     Vec2{99.0F, 59.0F}, Vec2{-8.0F, -9.0F}},
          BounceCase{"OutOfWorld", Vec2{100.5F, 30.0F}, Vec2{7.0F, -1.0F},
                     Vec2{99.0F, 30.0F}, Vec2{-7.0F, -1.0F}}),
      [](const ::testing::TestParamInfo<BounceCase> &info)
      { return std::string(info.param.test_name); });

  TEST_F(DetectCollisionsSapTest, Empty_0)
  {
    const std::size_t collisions =
        detect_collisions_sap(bodies, order, narrow_checks);

    EXPECT_EQ(collisions, 0U);
    EXPECT_EQ(narrow_checks, 0U);
    EXPECT_TRUE(order.empty());
  }

  TEST_F(DetectCollisionsSapTest, OneBody_0)
  {
    bodies = {
        Body{"A", Vec2{10.0F, 10.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}},
    };
    BuildOrderFromBodies();

    const std::size_t collisions =
        detect_collisions_sap(bodies, order, narrow_checks);

    EXPECT_EQ(collisions, 0U);
    EXPECT_EQ(narrow_checks, 0U);
    ASSERT_EQ(order.size(), 1U);
    EXPECT_EQ(order[0], 0);
  }

  TEST_P(DetectCollisionsSapParamTest, TwoBodies_Separate_Touching_Overlapping)
  {
    const auto param = GetParam();
    bodies = {
        Body{"A", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}},
        Body{"B", Vec2{CoordFor(param.x_relation), CoordFor(param.y_relation)},
             Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}},
    };
    BuildOrderFromBodies();

    const std::size_t collisions =
        detect_collisions_sap(bodies, order, narrow_checks);
    const std::size_t expected_narrow_checks =
        (param.x_relation == AxisRelation::Separate) ? 0U : 1U;

    EXPECT_EQ(collisions, param.expected_collisions);
    EXPECT_EQ(narrow_checks, expected_narrow_checks);
  }

  INSTANTIATE_TEST_SUITE_P(
      AxisRelations, DetectCollisionsSapParamTest,
      ::testing::Values(
          SapTwoBodyCase{AxisRelation::Separate, AxisRelation::Separate, 0U},
          SapTwoBodyCase{AxisRelation::Separate, AxisRelation::Touching, 0U},
          SapTwoBodyCase{AxisRelation::Separate, AxisRelation::Overlapping, 0U},
          SapTwoBodyCase{AxisRelation::Touching, AxisRelation::Separate, 0U},
          SapTwoBodyCase{AxisRelation::Touching, AxisRelation::Touching, 1U},
          SapTwoBodyCase{AxisRelation::Touching, AxisRelation::Overlapping, 1U},
          SapTwoBodyCase{AxisRelation::Overlapping, AxisRelation::Separate, 0U},
          SapTwoBodyCase{AxisRelation::Overlapping, AxisRelation::Touching, 1U},
          SapTwoBodyCase{AxisRelation::Overlapping, AxisRelation::Overlapping,
                         1U}),
      [](const ::testing::TestParamInfo<SapTwoBodyCase> &info)
      {
        return std::string("X") + AxisRelationToName(info.param.x_relation) +
               "_Y" + AxisRelationToName(info.param.y_relation);
      });

  TEST_F(DetectCollisionsSapTest, MultipleBodies_Unordered)
  {
    bodies = {
        Body{"A", Vec2{5.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}}, // left=4
        Body{"B", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F},
             Vec2{1.0F, 1.0F}}, // left=-1
        Body{"C", Vec2{1.5F, 0.0F}, Vec2{0.0F, 0.0F},
             Vec2{1.0F, 1.0F}}, // left=0.5
        Body{"D", Vec2{10.0F, 10.0F}, Vec2{0.0F, 0.0F},
             Vec2{1.0F, 1.0F}}, // left=9
    };
    order = {0, 1, 3, 2};

    const std::size_t collisions =
        detect_collisions_sap(bodies, order, narrow_checks);

    // Only B-C overlap.
    EXPECT_EQ(collisions, 1U);
    EXPECT_EQ(narrow_checks, 1U);
    ASSERT_EQ(order.size(), 4U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 0);
    EXPECT_EQ(order[3], 3);
  }

  TEST_F(DetectCollisionsSapTest, NarrowChecks_AccumulatingFromInitialValue)
  {
    bodies = {
        Body{"A", Vec2{0.0F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}},
        Body{"B", Vec2{1.5F, 0.0F}, Vec2{0.0F, 0.0F}, Vec2{1.0F, 1.0F}},
    };
    BuildOrderFromBodies();
    narrow_checks = 5U;

    const std::size_t collisions =
        detect_collisions_sap(bodies, order, narrow_checks);

    EXPECT_EQ(collisions, 1U);
    EXPECT_EQ(narrow_checks, 6U);
  }

} // namespace
