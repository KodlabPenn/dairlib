#include <memory>

#include <gflags/gflags.h>
#include "drake/geometry/geometry_visualization.h"
#include "drake/lcm/drake_lcm.h"
#include "drake/multibody/joints/floating_base_types.h"
#include "drake/multibody/tree/revolute_joint.h"
#include "drake/systems/analysis/implicit_euler_integrator.h"
#include "drake/systems/analysis/radau_integrator.h"
#include "drake/systems/analysis/runge_kutta2_integrator.h"
#include "drake/systems/analysis/simulator.h"
#include "drake/systems/framework/diagram.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/lcm/lcm_interface_system.h"
#include "drake/systems/lcm/lcm_publisher_system.h"
#include "drake/systems/lcm/lcm_subscriber_system.h"
#include "drake/systems/primitives/constant_vector_source.h"

#include "dairlib/lcmt_robot_input.hpp"
#include "dairlib/lcmt_robot_output.hpp"
#include "examples/Cassie/cassie_utils.h"
#include "multibody/multibody_utils.h"
#include "systems/primitives/subvector_pass_through.h"
#include "systems/robot_lcm_systems.h"

namespace dairlib {
using dairlib::systems::SubvectorPassThrough;
using drake::geometry::SceneGraph;
using drake::multibody::MultibodyPlant;
using drake::multibody::RevoluteJoint;
using drake::systems::Context;
using drake::systems::DiagramBuilder;
using drake::systems::Simulator;
using drake::systems::lcm::LcmPublisherSystem;
using drake::systems::lcm::LcmSubscriberSystem;

DEFINE_double(publish_rate, 1000, "Publishing frequency (Hz)");

// Simulation parameters.
DEFINE_bool(floating_base, true, "Fixed or floating base model");
DEFINE_double(end_time, std::numeric_limits<double>::infinity(),
              "Duration (s) to simulate for");
DEFINE_double(target_realtime_rate, 1.0,
              "Desired rate relative to real time.  See documentation for "
              "Simulator::set_target_realtime_rate() for details.");
DEFINE_bool(time_stepping, false,
            "If 'true', the plant is modeled as a "
            "discrete system with periodic updates. "
            "If 'false', the plant is modeled as a continuous system.");
DEFINE_double(dt, 1e-4,
              "The step size to use for compliant, "
              "(ignored for time_stepping)");
DEFINE_double(accuracy, 1e-5,
              "Integrator accuracy (ignored for time_stepping))");

int do_main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  DiagramBuilder<double> builder;

  auto lcm = builder.AddSystem<drake::systems::lcm::LcmInterfaceSystem>();

  SceneGraph<double>& scene_graph = *builder.AddSystem<SceneGraph>();
  scene_graph.set_name("scene_graph");

  const double time_step = FLAGS_time_stepping ? FLAGS_dt : 0.0;

  MultibodyPlant<double>& plant = *builder.AddSystem<MultibodyPlant>(time_step);

  if (FLAGS_floating_base) {
    multibody::addFlatTerrain(&plant, &scene_graph, .8, .8);
  }

  addCassieMultibody(&plant, &scene_graph, FLAGS_floating_base);

  plant.Finalize();
  std::cout << "Before calling ToSymbolic" << std::endl;
  auto tmp = drake::systems::System<double>::ToSymbolic(plant);
  const MultibodyPlant<drake::symbolic::Expression>& symbolic_plant = *tmp;
  std::cout << symbolic_plant.num_joints() << std::endl;

  // Create input receiver.
  auto input_sub =
      builder.AddSystem(LcmSubscriberSystem::Make<dairlib::lcmt_robot_input>(
          "CASSIE_INPUT", lcm));
  auto input_receiver = builder.AddSystem<systems::RobotInputReceiver>(plant);
  builder.Connect(*input_sub, *input_receiver);

  // connect input receiver
  auto passthrough = builder.AddSystem<SubvectorPassThrough>(
      input_receiver->get_output_port(0).size(), 0,
      plant.get_actuation_input_port().size());

  builder.Connect(*input_receiver, *passthrough);
  builder.Connect(passthrough->get_output_port(),
                  plant.get_actuation_input_port());

  // Create state publisher.
  auto state_pub =
      builder.AddSystem(LcmPublisherSystem::Make<dairlib::lcmt_robot_output>(
          "CASSIE_STATE", lcm, 1.0 / FLAGS_publish_rate));
  auto state_sender = builder.AddSystem<systems::RobotOutputSender>(plant);

  // connect state publisher
  builder.Connect(plant.get_state_output_port(),
                  state_sender->get_input_port_state());

  builder.Connect(*state_sender, *state_pub);

  builder.Connect(
      plant.get_geometry_poses_output_port(),
      scene_graph.get_source_pose_port(plant.get_source_id().value()));

  builder.Connect(scene_graph.get_query_output_port(),
                  plant.get_geometry_query_input_port());

  auto diagram = builder.Build();

  std::cout << "a" << std::endl;
  auto diagram_sym = diagram->ToSymbolic();
  const auto& plant_sym =
      dynamic_cast<const MultibodyPlant<drake::symbolic::Expression>&>(
          diagram->GetSubsystemByName(plant.get_name()));
  std::cout << plant_sym.num_joints() << std::endl;

  // Create a context for this system:
  std::unique_ptr<Context<double>> diagram_context =
      diagram->CreateDefaultContext();
  diagram_context->EnableCaching();
  diagram->SetDefaultContext(diagram_context.get());
  Context<double>& plant_context =
      diagram->GetMutableSubsystemContext(plant, diagram_context.get());

  plant.GetJointByName<RevoluteJoint>("hip_pitch_left")
      .set_angle(&plant_context, .269);
  plant.GetJointByName<RevoluteJoint>("knee_left")
      .set_angle(&plant_context, -.644);
  plant.GetJointByName<RevoluteJoint>("ankle_joint_left")
      .set_angle(&plant_context, .792);
  plant.GetJointByName<RevoluteJoint>("toe_left")
      .set_angle(&plant_context, -M_PI / 3);

  plant.GetJointByName<RevoluteJoint>("hip_pitch_right")
      .set_angle(&plant_context, .269);
  plant.GetJointByName<RevoluteJoint>("knee_right")
      .set_angle(&plant_context, -.644);
  plant.GetJointByName<RevoluteJoint>("ankle_joint_right")
      .set_angle(&plant_context, .792);
  plant.GetJointByName<RevoluteJoint>("toe_right")
      .set_angle(&plant_context, -M_PI / 3);

  if (FLAGS_floating_base) {
    const drake::math::RigidTransformd transform(
        drake::math::RotationMatrix<double>(), Eigen::Vector3d(0, 0, 1.2));
    plant.SetFreeBodyPose(&plant_context, plant.GetBodyByName("pelvis"),
                          transform);
  }

  Simulator<double> simulator(*diagram, std::move(diagram_context));

  // Different continuous time integrator choices
  if (!FLAGS_time_stepping) {
    simulator.reset_integrator<drake::systems::RungeKutta2Integrator<double>>(
        *diagram, FLAGS_dt, &simulator.get_mutable_context());
    // simulator.reset_integrator<drake::systems::ImplicitEulerIntegrator<double>>
    //     (
    //       *diagram, &simulator.get_mutable_context());
    // simulator.reset_integrator<drake::systems::RadauIntegrator<double, 2>>(
    //   *diagram, &simulator.get_mutable_context());
    simulator.get_mutable_integrator().set_maximum_step_size(FLAGS_dt);
    simulator.get_mutable_integrator().set_target_accuracy(FLAGS_accuracy);
      FLAGS_dt);
  }

  simulator.set_publish_every_time_step(false);
  simulator.set_publish_at_initialization(false);
  simulator.set_target_realtime_rate(FLAGS_target_realtime_rate);
  simulator.Initialize();
  // simulator.AdvanceTo(std::numeric_limits<double>::infinity());
  auto start = std::chrono::steady_clock::now();
  simulator.AdvanceTo(FLAGS_end_time);
  std::cout << "0.5 second execution time: "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - start)
                   .count()
            << std::endl;
  std::cout << "Average realtime factor: "
            << 0.5 * 1e9 /
                   std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count()
            << std::endl;

  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) { return dairlib::do_main(argc, argv); }
