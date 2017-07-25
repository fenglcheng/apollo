/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/dreamview/backend/simulation_world/simulation_world_service.h"

#include <iostream>
#include "gtest/gtest.h"

#include "modules/common/adapters/adapter_manager.h"
#include "modules/common/configs/vehicle_config_helper.h"
#include "modules/common/math/quaternion.h"

using apollo::canbus::Chassis;
using apollo::common::monitor::MonitorMessage;
using apollo::localization::LocalizationEstimate;
using apollo::planning::ADCTrajectory;
using apollo::common::TrajectoryPoint;
using apollo::perception::PerceptionObstacle;
using apollo::perception::PerceptionObstacles;

namespace apollo {
namespace dreamview {

const float kEpsilon = 0.0001;

class SimulationWorldServiceTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    apollo::common::config::VehicleConfigHelper::Init();
    sim_world_service_.reset(new SimulationWorldService(&map_service_));
  }
 protected:
  SimulationWorldServiceTest()
      : map_service_("modules/dreamview/backend/testdata/garage.bin") {
  }

  MapService map_service_;
  std::unique_ptr<SimulationWorldService> sim_world_service_;
};

TEST_F(SimulationWorldServiceTest, UpdateMonitorSuccess) {
  MonitorMessage monitor;
  monitor.add_item()->set_msg("I am the latest message.");
  monitor.mutable_header()->set_timestamp_sec(2000);

  sim_world_service_->world_.mutable_monitor()->mutable_header()
      ->set_timestamp_sec(1990);
  sim_world_service_->world_.mutable_monitor()->add_item()->set_msg(
      "I am the previous message.");

  sim_world_service_->UpdateSimulationWorld(monitor);
  EXPECT_EQ(2, sim_world_service_->world_.monitor().item_size());
  EXPECT_EQ("I am the latest message.",
            sim_world_service_->world_.monitor().item(0).msg());
  EXPECT_EQ("I am the previous message.",
            sim_world_service_->world_.monitor().item(1).msg());
}

TEST_F(SimulationWorldServiceTest, UpdateMonitorRemove) {
  MonitorMessage monitor;
  monitor.add_item()->set_msg("I am message -2");
  monitor.add_item()->set_msg("I am message -1");
  monitor.mutable_header()->set_timestamp_sec(2000);

  sim_world_service_->world_.mutable_monitor()->mutable_header()
      ->set_timestamp_sec(1990);
  for (int i = 0; i < SimulationWorldService::kMaxMonitorItems; ++i) {
    sim_world_service_->world_.mutable_monitor()->add_item()->set_msg(
        "I am message " + std::to_string(i));
  }
  int last = SimulationWorldService::kMaxMonitorItems - 1;
  EXPECT_EQ("I am message " + std::to_string(last),
            sim_world_service_->world_.monitor().item(last).msg());

  sim_world_service_->UpdateSimulationWorld(monitor);
  EXPECT_EQ(SimulationWorldService::kMaxMonitorItems,
            sim_world_service_->world_.monitor().item_size());
  EXPECT_EQ("I am message -2",
            sim_world_service_->world_.monitor().item(0).msg());
  EXPECT_EQ("I am message -1",
            sim_world_service_->world_.monitor().item(1).msg());
  EXPECT_EQ("I am message " + std::to_string(last - monitor.item_size()),
            sim_world_service_->world_.monitor().item(last).msg());
}

TEST_F(SimulationWorldServiceTest, UpdateMonitorTruncate) {
  MonitorMessage monitor;
  int large_size = SimulationWorldService::kMaxMonitorItems + 10;
  for (int i = 0; i < large_size; ++i) {
    monitor.add_item()->set_msg("I am message " + std::to_string(i));
  }
  monitor.mutable_header()->set_timestamp_sec(2000);
  EXPECT_EQ(large_size, monitor.item_size());
  EXPECT_EQ("I am message " + std::to_string(large_size - 1),
            monitor.item(large_size - 1).msg());
  sim_world_service_->world_.mutable_monitor()->mutable_header()
      ->set_timestamp_sec(1990);

  sim_world_service_->UpdateSimulationWorld(monitor);
  int last = SimulationWorldService::kMaxMonitorItems - 1;
  EXPECT_EQ(SimulationWorldService::kMaxMonitorItems,
            sim_world_service_->world_.monitor().item_size());
  EXPECT_EQ("I am message 0",
            sim_world_service_->world_.monitor().item(0).msg());
  EXPECT_EQ("I am message " + std::to_string(last),
            sim_world_service_->world_.monitor().item(last).msg());
}

TEST_F(SimulationWorldServiceTest, UpdateChassisInfo) {
  // Prepare the chassis message that will be used to update the
  // SimulationWorld object.
  ::apollo::canbus::Chassis chassis;
  chassis.set_speed_mps(25);
  chassis.set_throttle_percentage(50);
  chassis.set_brake_percentage(10);
  chassis.set_steering_percentage(25);
  chassis.mutable_signal()->set_turn_signal(
      apollo::common::VehicleSignal::TURN_RIGHT);

  // Commit the update.
  sim_world_service_->UpdateSimulationWorld(chassis);

  // Check the update reuslt.
  const Object& car = sim_world_service_->world_.auto_driving_car();
  EXPECT_DOUBLE_EQ(4.933, car.length());
  EXPECT_DOUBLE_EQ(2.11, car.width());
  EXPECT_DOUBLE_EQ(1.48, car.height());
  EXPECT_DOUBLE_EQ(25.0, car.speed());
  EXPECT_DOUBLE_EQ(50.0, car.throttle_percentage());
  EXPECT_DOUBLE_EQ(10.0, car.brake_percentage());
  EXPECT_DOUBLE_EQ(25.0, car.steering_angle());
  EXPECT_EQ("RIGHT", car.current_signal());
}

TEST_F(SimulationWorldServiceTest, UpdateLocalization) {
  // Prepare the localization message that will be used to update the
  // SimulationWorld object.
  ::apollo::localization::LocalizationEstimate localization;
  localization.mutable_pose()->mutable_position()->set_x(1.0);
  localization.mutable_pose()->mutable_position()->set_y(1.5);
  localization.mutable_pose()->mutable_orientation()->set_qx(0.0);
  localization.mutable_pose()->mutable_orientation()->set_qy(0.0);
  localization.mutable_pose()->mutable_orientation()->set_qz(0.0);
  localization.mutable_pose()->mutable_orientation()->set_qw(0.0);

  auto pose = localization.pose();
  auto heading = apollo::common::math::QuaternionToHeading(
      pose.orientation().qw(), pose.orientation().qx(), pose.orientation().qy(),
      pose.orientation().qz());
  localization.mutable_pose()->set_heading(heading);

  // Commit the update.
  sim_world_service_->UpdateSimulationWorld(localization);

  // Check the update result.
  const Object& car = sim_world_service_->world_.auto_driving_car();
  EXPECT_DOUBLE_EQ(1.0, car.position_x());
  EXPECT_DOUBLE_EQ(1.5, car.position_y());
  EXPECT_DOUBLE_EQ(
      apollo::common::math::QuaternionToHeading(0.0, 0.0, 0.0, 0.0),
      car.heading());
}

TEST_F(SimulationWorldServiceTest, UpdatePlanningTrajectory) {
  // Prepare the trajectory message that will be used to update the
  // SimulationWorld object.
  ADCTrajectory planning_trajectory;
  for (int i = 0; i < 30; ++i) {
    TrajectoryPoint* point = planning_trajectory.add_trajectory_point();
    point->mutable_path_point()->set_x(i * 10);
    point->mutable_path_point()->set_y(i * 10 + 10);
  }

  // Commit the update.
  sim_world_service_->UpdateSimulationWorld(planning_trajectory);

  // Check the update result.
  EXPECT_EQ(sim_world_service_->world_.planning_trajectory_size(), 4);

  // Check first point.
  {
    const Object point = sim_world_service_->world_.planning_trajectory(0);
    EXPECT_DOUBLE_EQ(0.0, point.position_x());
    EXPECT_DOUBLE_EQ(10.0, point.position_y());
    EXPECT_DOUBLE_EQ(atan2(100.0, 100.0), point.heading());
    EXPECT_EQ(point.polygon_point_size(), 4);
  }

  // Check last point.
  {
    const Object point = sim_world_service_->world_.planning_trajectory(3);
    EXPECT_DOUBLE_EQ(280.0, point.position_x());
    EXPECT_DOUBLE_EQ(290.0, point.position_y());
    EXPECT_DOUBLE_EQ(atan2(100.0, 100.0), point.heading());
    EXPECT_EQ(point.polygon_point_size(), 4);
  }
}

TEST_F(SimulationWorldServiceTest, UpdatePerceptionObstacles) {
  PerceptionObstacles obstacles;
  PerceptionObstacle* obstacle1 = obstacles.add_perception_obstacle();
  obstacle1->set_id(1);
  apollo::perception::Point* point1 = obstacle1->add_polygon_point();
  point1->set_x(0.0);
  point1->set_y(0.0);
  apollo::perception::Point* point2 = obstacle1->add_polygon_point();
  point2->set_x(0.0);
  point2->set_y(1.0);
  apollo::perception::Point* point3 = obstacle1->add_polygon_point();
  point3->set_x(-1.0);
  point3->set_y(0.0);
  obstacle1->set_timestamp(1489794020.123);
  obstacle1->set_type(apollo::perception::PerceptionObstacle_Type_UNKNOWN);
  apollo::perception::PerceptionObstacle* obstacle2 = obstacles
      .add_perception_obstacle();
  obstacle2->set_id(2);
  apollo::perception::Point* point = obstacle2->mutable_position();
  point->set_x(1.0);
  point->set_y(2.0);
  obstacle2->set_theta(3.0);
  obstacle2->set_length(4.0);
  obstacle2->set_width(5.0);
  obstacle2->set_height(6.0);
  obstacle2->set_type(apollo::perception::PerceptionObstacle_Type_VEHICLE);

  sim_world_service_->UpdateSimulationWorld(obstacles);
  EXPECT_EQ(2, sim_world_service_->world_.object_size());

  for (const auto& object : sim_world_service_->world_.object()) {
    if (object.id() == "1") {
      EXPECT_NEAR(1489794020.123, object.timestamp_sec(), kEpsilon);
      EXPECT_EQ(3, object.polygon_point_size());
      EXPECT_EQ(Object_Type_UNKNOWN, object.type());
    } else if (object.id() == "2") {
      EXPECT_NEAR(1.0, object.position_x(), kEpsilon);
      EXPECT_NEAR(2.0, object.position_y(), kEpsilon);
      EXPECT_NEAR(3.0, object.heading(), kEpsilon);
      EXPECT_NEAR(4.0, object.length(), kEpsilon);
      EXPECT_NEAR(5.0, object.width(), kEpsilon);
      EXPECT_NEAR(6.0, object.height(), kEpsilon);
      EXPECT_EQ(0, object.polygon_point_size());
      EXPECT_EQ(Object_Type_VEHICLE, object.type());
    } else {
      EXPECT_TRUE(false) << "Unexpected object id " << object.id();
    }
  }
}

}  // namespace dreamview
}  // namespace apollo
