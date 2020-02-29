#pragma once

#include <string>
#include <Eigen/Dense>
#include "solvers/optimization_utils.h"
#include "systems/trajectory_optimization/dircon_position_data.h"
#include "systems/trajectory_optimization/dircon_distance_data.h"
#include "systems/trajectory_optimization/dircon_kinematic_data_set.h"
#include "systems/trajectory_optimization/dircon_opt_constraints.h"
#include "systems/trajectory_optimization/hybrid_dircon.h"

#include "drake/solvers/snopt_solver.h"
#include "drake/solvers/gurobi_solver.h"
#include "drake/solvers/mathematical_program.h"
#include "drake/solvers/constraint.h"
#include "drake/solvers/solve.h"

#include "multibody/multibody_utils.h"

#include "examples/goldilocks_models/find_models/goldilocks_model_traj_opt.h"

using drake::solvers::MathematicalProgram;
using drake::solvers::MathematicalProgramResult;
using drake::solvers::SolutionResult;
using Eigen::VectorXd;
using Eigen::MatrixXd;
using std::vector;

using drake::multibody::MultibodyPlant;
using drake::AutoDiffXd;

using dairlib::systems::trajectory_optimization::DirconAbstractConstraint;

using drake::math::RotationMatrix;
using drake::math::RollPitchYaw;

using drake::multibody::JointActuator;
using drake::multibody::JointActuatorIndex;
using drake::multibody::BodyIndex;
using drake::multibody::ModelInstanceIndex;

namespace dairlib {
namespace goldilocks_models  {

void trajOptGivenWeights(
  const MultibodyPlant<double> & plant,
  const MultibodyPlant<AutoDiffXd> & plant_autoDiff,
  int n_s, int n_sDDot, int n_tau, int n_feature_s, int n_feature_sDDot,
  MatrixXd B_tau,
  const VectorXd & theta_s, const VectorXd & theta_sDDot,
  double stride_length, double ground_incline,
  double duration, int n_node, int max_iter,
  double major_optimality_tol, double major_feasibility_tol,
  std::string directory, std::string init_file, std::string prefix,
  /*vector<VectorXd> * w_sol_vec,
  vector<MatrixXd> * A_vec, vector<MatrixXd> * H_vec,
  vector<VectorXd> * y_vec,
  vector<VectorXd> * lb_vec, vector<VectorXd> * ub_vec,
  vector<VectorXd> * b_vec,
  vector<VectorXd> * c_vec,
  vector<MatrixXd> * B_vec,*/
  double Q_double, double R_double,
  double eps_reg,
  bool is_get_nominal,
  bool is_zero_touchdown_impact,
  bool extend_model,
  bool is_add_tau_in_cost,
  int sample_idx, int n_rerun,
  int rom_option, int robot_option);

void addRegularization(bool is_get_nominal, double eps_reg,
                       GoldilocksModelTrajOpt& gm_traj_opt);
void setInitialGuessFromFile(const VectorXd& w_sol,
                             GoldilocksModelTrajOpt& gm_traj_opt);
void augmentConstraintToFixThetaScaling(MatrixXd & B, MatrixXd & A,
                                        VectorXd & y, VectorXd & lb, VectorXd & ub,
                                        int n_s, int n_feature_s,
                                        const VectorXd & theta_s, int sample_idx);
void extractResult(VectorXd& w_sol,
                   GoldilocksModelTrajOpt& gm_traj_opt,
                   const MathematicalProgramResult& result,
                   std::chrono::duration<double> elapsed,
                   std::vector<int> num_time_samples,
                   int& N,
                   const MultibodyPlant<double> & plant,
                   const MultibodyPlant<AutoDiffXd> & plant_autoDiff,
                   int n_s, int n_sDDot, int n_tau,
                   int n_feature_s,
                   int n_feature_sDDot,
                   MatrixXd B_tau,
                   const VectorXd & theta_s, const VectorXd & theta_sDDot,
                   double stride_length, double ground_incline,
                   double duration, int max_iter,
                   string directory,
                   string init_file, string prefix,
                   double Q_double, double R_double,
                   double eps_reg,
                   bool is_get_nominal,
                   bool is_zero_touchdown_impact,
                   bool extend_model,
                   bool is_add_tau_in_cost,
                   int sample_idx, int n_rerun, int rom_option,
                   int robot_option,
                   vector<DirconKinematicDataSet<double>*> dataset_list);
void postProcessing(const VectorXd& w_sol,
                    GoldilocksModelTrajOpt& gm_traj_opt,
                    const MathematicalProgramResult& result,
                    std::chrono::duration<double> elapsed,
                    std::vector<int> num_time_samples,
                    int& N,
                    const MultibodyPlant<double> & plant,
                    const MultibodyPlant<AutoDiffXd> & plant_autoDiff,
                    int n_s, int n_sDDot, int n_tau,
                    int n_feature_s,
                    int n_feature_sDDot,
                    MatrixXd B_tau,
                    const VectorXd & theta_s, const VectorXd & theta_sDDot,
                    double stride_length, double ground_incline,
                    double duration, int max_iter,
                    string directory,
                    string init_file, string prefix,
                    double Q_double, double R_double,
                    double eps_reg,
                    bool is_get_nominal,
                    bool is_zero_touchdown_impact,
                    bool extend_model,
                    bool is_add_tau_in_cost,
                    int sample_idx,
                    int rom_option, int robot_option);
void fiveLinkRobotTrajOpt(const MultibodyPlant<double> & plant,
                          const MultibodyPlant<AutoDiffXd> & plant_autoDiff,
                          int n_s, int n_sDDot, int n_tau,
                          int n_feature_s,
                          int n_feature_sDDot,
                          MatrixXd B_tau,
                          const VectorXd & theta_s, const VectorXd & theta_sDDot,
                          double stride_length, double ground_incline,
                          double duration, int n_node, int max_iter,
                          string directory,
                          string init_file, string prefix,
                          double Q_double, double R_double,
                          double eps_reg,
                          bool is_get_nominal,
                          bool is_zero_touchdown_impact,
                          bool extend_model,
                          bool is_add_tau_in_cost,
                          int sample_idx, int n_rerun,
                          int rom_option, int robot_option);
void cassieTrajOpt(const MultibodyPlant<double> & plant,
                   const MultibodyPlant<AutoDiffXd> & plant_autoDiff,
                   int n_s, int n_sDDot, int n_tau,
                   int n_feature_s,
                   int n_feature_sDDot,
                   MatrixXd B_tau,
                   const VectorXd & theta_s, const VectorXd & theta_sDDot,
                   double stride_length, double ground_incline,
                   double duration, int n_node, int max_iter,
                   double major_optimality_tol,
                   double major_feasibility_tol,
                   string directory,
                   string init_file, string prefix,
                   double Q_double, double R_double,
                   double eps_reg,
                   bool is_get_nominal,
                   bool is_zero_touchdown_impact,
                   bool extend_model,
                   bool is_add_tau_in_cost,
                   int sample_idx, int n_rerun,
                   int rom_option, int robot_option);

// Position constraint of a body origin in one dimension (x, y, or z)
class OneDimBodyPosConstraint : public DirconAbstractConstraint<double> {
 public:
  OneDimBodyPosConstraint(const MultibodyPlant<double>* plant,
                          const string& body_name,
                          const Vector3d& point_wrt_body,
                          const Eigen::Matrix3d& rot_mat, int xyz_idx,
                          double lb, double ub)
      : DirconAbstractConstraint<double>(
            1, plant->num_positions(), VectorXd::Ones(1) * lb,
            VectorXd::Ones(1) * ub,
            body_name + "_constraint_" + std::to_string(xyz_idx)),
        plant_(plant),
        body_(plant->GetBodyByName(body_name)),
        point_wrt_body_(point_wrt_body),
        xyz_idx_(xyz_idx),
        rot_mat_(rot_mat) {}
  ~OneDimBodyPosConstraint() override = default;

  void EvaluateConstraint(const Eigen::Ref<const drake::VectorX<double>>& x,
                          drake::VectorX<double>* y) const override {
    VectorXd q = x;

    std::unique_ptr<drake::systems::Context<double>> context =
        plant_->CreateDefaultContext();
    plant_->SetPositions(context.get(), q);

    VectorX<double> pt(3);
    this->plant_->CalcPointsPositions(*context, body_.body_frame(),
                                      point_wrt_body_, plant_->world_frame(),
                                      &pt);
    *y = (rot_mat_ * pt).segment(xyz_idx_, 1);
  };

 private:
  const MultibodyPlant<double>* plant_;
  const drake::multibody::Body<double>& body_;
  const Vector3d point_wrt_body_;
  // xyz_idx_ takes value of 0, 1 or 2.
  // 0 is x, 1 is y and 2 is z component of the position vector.
  const int xyz_idx_;
  Eigen::Matrix3d rot_mat_;
};

class ComHeightVelConstraint : public DirconAbstractConstraint<double> {
 public:
  ComHeightVelConstraint(const MultibodyPlant<double>* plant) :
    DirconAbstractConstraint<double>(
      1, 2 * (plant->num_positions() + plant->num_velocities()),
      VectorXd::Zero(1), VectorXd::Zero(1),
      "com_height_vel_constraint"),
    plant_(plant),
    n_q_(plant->num_positions()),
    n_v_(plant->num_velocities()) {

    DRAKE_DEMAND(plant->num_bodies() > 1);
    DRAKE_DEMAND(plant->num_model_instances() > 1);

    // Get all body indices
    std::vector<ModelInstanceIndex> model_instances;
    for (ModelInstanceIndex model_instance_index(1);
         model_instance_index < plant->num_model_instances();
         ++model_instance_index)
      model_instances.push_back(model_instance_index);
    for (auto model_instance : model_instances) {
      const std::vector<BodyIndex> body_index_in_instance =
        plant->GetBodyIndices(model_instance);
      for (BodyIndex body_index : body_index_in_instance)
        body_indexes_.push_back(body_index);
    }
    // Get total mass
    std::unique_ptr<drake::systems::Context<double>> context =
          plant->CreateDefaultContext();
    for (BodyIndex body_index : body_indexes_) {
      if (body_index == 0) continue;
      const Body<double>& body = plant_->get_body(body_index);

      // Calculate composite_mass_.
      const double& body_mass = body.get_mass(*context);
      // composite_mass_ = ∑ mᵢ
      composite_mass_ += body_mass;
    }
    if (!(composite_mass_ > 0)) {
      throw std::runtime_error(
        "The total mass must larger than zero.");
    }
  }
  ~ComHeightVelConstraint() override = default;

  void EvaluateConstraint(const Eigen::Ref<const drake::VectorX<double>>& x,
                          drake::VectorX<double>* y) const override {
    VectorXd q1 = x.head(n_q_);
    VectorXd v1 = x.segment(n_q_, n_v_);
    VectorXd q2 = x.segment(n_q_ + n_v_, n_q_);
    VectorXd v2 = x.segment(2 * n_q_ + n_v_, n_v_);

    std::unique_ptr<drake::systems::Context<double>> context =
          plant_->CreateDefaultContext();
    plant_->SetPositions(context.get(), q1);
    plant_->SetVelocities(context.get(), v1);

    const drake::multibody::Frame<double>& world = plant_->world_frame();

    // Get com jacobian for x1
    MatrixXd Jcom1 = MatrixXd::Zero(3, n_v_);
    for (BodyIndex body_index : body_indexes_) {
      if (body_index == 0) continue;

      const Body<double>& body = plant_->get_body(body_index);
      const Vector3d pi_BoBcm = body.CalcCenterOfMassInBodyFrame(*context);

      // Calculate M * J in world frame.
      const double& body_mass = body.get_mass(*context);
      // Jcom = ∑ mᵢ * Ji
      MatrixXd Jcom_i(3, n_v_);
      plant_->CalcJacobianTranslationalVelocity(
        *context, drake::multibody::JacobianWrtVariable::kV,
        body.body_frame(), pi_BoBcm, world, world, &Jcom_i);
      Jcom1 += body_mass * Jcom_i;
      // cout << "body_mass = " << body_mass << endl;
      // cout << "Jcom_i = " << Jcom_i << endl;
    }
    Jcom1 /= composite_mass_;

    // Get com jacobian for x2
    plant_->SetPositions(context.get(), q2);
    plant_->SetVelocities(context.get(), v2);
    MatrixXd Jcom2 = MatrixXd::Zero(3, n_v_);
    for (BodyIndex body_index : body_indexes_) {
      if (body_index == 0) continue;

      const Body<double>& body = plant_->get_body(body_index);
      const Vector3d pi_BoBcm = body.CalcCenterOfMassInBodyFrame(*context);

      // Calculate M * J in world frame.
      const double& body_mass = body.get_mass(*context);
      // Jcom = ∑ mᵢ * Ji
      MatrixXd Jcom_i(3, n_v_);
      plant_->CalcJacobianTranslationalVelocity(
        *context, drake::multibody::JacobianWrtVariable::kV,
        body.body_frame(), pi_BoBcm, world, world, &Jcom_i);
      Jcom2 += body_mass * Jcom_i;
      // cout << "body_mass = " << body_mass << endl;
      // cout << "Jcom_i = " << Jcom_i << endl;
    }
    Jcom2 /= composite_mass_;


    *y = Jcom1.row(2) * v1 - Jcom2.row(2) * v2;
  };
 private:
  const MultibodyPlant<double>* plant_;
  int n_q_;
  int n_v_;

  std::vector<BodyIndex> body_indexes_;
  double composite_mass_;
};

}  // namespace goldilocks_models
}  // namespace dairlib