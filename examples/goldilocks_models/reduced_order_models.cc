#include "examples/goldilocks_models/reduced_order_models.h"

#include <algorithm>

using drake::MatrixX;
using drake::VectorX;
using drake::multibody::Frame;
using drake::multibody::JacobianWrtVariable;
using drake::multibody::MultibodyPlant;
using drake::trajectories::PiecewisePolynomial;
using Eigen::Matrix3Xd;
using Eigen::MatrixXd;
using Eigen::Vector3d;
using Eigen::VectorXd;
using std::cout;
using std::endl;
using std::map;
using std::multiset;
using std::pair;
using std::set;
using std::shared_ptr;
using std::string;
using std::to_string;
using std::vector;

namespace dairlib {
namespace goldilocks_models {

MonomialFeatures::MonomialFeatures(int n_order, int n_q, vector<int> skip_inds,
                                   const std::string& name)
    : n_q_(n_q), name_(name) {
  // Print
  if (!name.empty()) cout << name << " ";
  cout << "uses monominal features with\n";
  cout << "  n_order = " << n_order << endl;
  cout << "  n_q = " << n_q << endl;
  cout << "  skip_inds = {";
  for (const auto& idx : skip_inds) {
    cout << idx << ", ";
  }
  cout << "}\n";

  for (const auto& idx : skip_inds) {
    DRAKE_DEMAND(idx >= 0);
    DRAKE_DEMAND(idx < n_q);
  }

  // Construct active indices (complement of skip_inds)
  std::vector<int> active_inds;
  for (int i = 0; i < n_q; i++) {
    if (std::find(skip_inds.begin(), skip_inds.end(), i) == skip_inds.end()) {
      active_inds.push_back(i);
    }
  }

  // Construct features_
  set<multiset<int>> previous_subfeatures = {};
  for (int order = 0; order <= n_order; order++) {
    previous_subfeatures =
        ConstructSubfeaturesWithOneMoreOrder(active_inds, previous_subfeatures);
    features_.insert(previous_subfeatures.begin(), previous_subfeatures.end());
  }

  // Construct first time partial derivatives for each term
  int feature_idx = 0;
  for (const auto& feat : features_) {
    for (auto i : active_inds) {
      multiset<int> monomial_copy = feat;
      // Count how many i in the term
      int count = monomial_copy.count(i);
      if (count != 0) {
        // erase the element i from the term
        auto itr = monomial_copy.find(i);
        if (itr != monomial_copy.end()) {
          monomial_copy.erase(itr);
        }
        // Add the resulting term into first_ord_partial_diff_
        first_ord_partial_diff_[{feature_idx, {i}}] = {count, monomial_copy};
      }
    }
    feature_idx++;
  }

  // Construct second time partial derivatives for each term
  for (const auto& ele : first_ord_partial_diff_) {
    for (auto i : active_inds) {
      pair<int, multiset<int>> key_copy = ele.first;
      pair<int, multiset<int>> term_copy = ele.second;
      auto monomial_copy = term_copy.second;
      // Count how many i in the term
      int count = monomial_copy.count(i);
      if (count != 0) {
        // erase the element i from the term
        auto itr = monomial_copy.find(i);
        if (itr != monomial_copy.end()) {
          monomial_copy.erase(itr);
        }
        // Update the "wrt" index set and the coefficient
        key_copy.second.insert(i);
        auto new_coeff = term_copy.first * count;
        // Insert the resulting term into first_ord_partial_diff_ if it doesn't
        // exist. Otherwise, add the new coefficient to the existing coefficient
        auto it = second_ord_partial_diff_.find(key_copy);
        if (it == second_ord_partial_diff_.end()) {
          second_ord_partial_diff_[key_copy] = {new_coeff, monomial_copy};
        } else {
          it->second.first += new_coeff;
        }
      }
    }
  }
};

set<multiset<int>> MonomialFeatures::ConstructSubfeaturesWithOneMoreOrder(
    const vector<int>& active_inds,
    const set<multiset<int>>& terms_of_same_order) {
  set<multiset<int>> ret;
  if (terms_of_same_order.empty()) {
    // if terms_of_same_order is empty, then add {}, i.e. zero order term, to
    // the set
    ret.insert(multiset<int>());
  } else {
    for (const auto& term : terms_of_same_order) {
      for (auto i : active_inds) {
        multiset<int> new_term = term;
        new_term.insert(i);
        ret.insert(new_term);
      }
    }
  }
  return ret;
}

void MonomialFeatures::PrintMultiset(const multiset<int>& set) {
  bool past_first_element = false;
  cout << "(";
  for (const auto& ele : set) {
    if (!past_first_element) {
      past_first_element = true;
    } else {
      cout << ", ";
    }
    cout << ele;
  }
  cout << ")";
}
void MonomialFeatures::PrintSymbolicFeatures() const {
  cout << "Features = \n";
  cout << "  row index : symbolic term\n";
  int row_idx = 0;
  for (const auto& feat_i : features_) {
    cout << "  " << row_idx << ": ";
    PrintMultiset(feat_i);
    cout << "\n";
    row_idx++;
  }
}
void MonomialFeatures::PrintSymbolicPartialDerivatives(int order) const {
  DRAKE_DEMAND((order == 1) || (order == 2));
  (order == 1) ? cout << "First" : cout << "Second";
  cout << " order partial derivatives = \n";
  cout << "  Key ==> Term\n";
  const auto& map =
      (order == 1) ? first_ord_partial_diff_ : second_ord_partial_diff_;
  for (const auto& ele : map) {
    cout << "  " << ele.first.first << ", ";
    PrintMultiset(ele.first.second);
    cout << " ==> " << ele.second.first << ", ";
    PrintMultiset(ele.second.second);
    cout << endl;
  }
}

VectorX<double> MonomialFeatures::Eval(const VectorX<double>& q) const {
  DRAKE_DEMAND(q.size() == n_q_);

  VectorX<double> ret(features_.size());
  int idx = 0;
  for (const auto& term : features_) {
    double value = 1;
    for (const auto& ele : term) {
      value *= q(ele);
    }
    ret(idx) = value;
    idx++;
  }
  return ret;
}
VectorX<double> MonomialFeatures::EvalJV(const VectorX<double>& q,
                                         const VectorX<double>& qdot) const {
  return EvalFeatureTimeDerivatives(q, qdot, first_ord_partial_diff_);
}
VectorX<double> MonomialFeatures::EvalJdotV(const VectorX<double>& q,
                                            const VectorX<double>& qdot) const {
  return EvalFeatureTimeDerivatives(q, qdot, second_ord_partial_diff_);
}

VectorX<double> MonomialFeatures::EvalFeatureTimeDerivatives(
    const VectorX<double>& q, const VectorX<double>& qdot,
    const map<pair<int, multiset<int>>, pair<int, multiset<int>>>&
        partial_diff_map) const {
  DRAKE_DEMAND(q.size() == n_q_);
  DRAKE_DEMAND(qdot.size() == n_q_);

  VectorX<double> ret = VectorX<double>::Zero(features_.size());
  for (const auto& ele : partial_diff_map) {
    double value = ele.second.first;
    for (const auto& q_idx : ele.second.second) {
      value *= q(q_idx);
    }
    for (const auto& qdot_idx : ele.first.second) {
      value *= qdot(qdot_idx);
    }

    ret(ele.first.first) += value;
  }
  return ret;
}

/// Constructors of ReducedOrderModel
ReducedOrderModel::ReducedOrderModel(int n_y, int n_tau,
                                     const Eigen::MatrixXd& B_tau,
                                     int n_feature_y, int n_feature_yddot,
                                     const MonomialFeatures& mapping_basis,
                                     const MonomialFeatures& dynamic_basis,
                                     const std::string& name)
    : name_(name),
      n_y_(n_y),
      n_yddot_(n_y),
      n_tau_(n_tau),
      B_tau_(B_tau),
      n_feature_y_(n_feature_y),
      n_feature_yddot_(n_feature_yddot),
      mapping_basis_(mapping_basis),
      dynamic_basis_(dynamic_basis),
      theta_y_(VectorXd::Zero(n_y * n_feature_y)),
      theta_yddot_(VectorXd::Zero(n_y * n_feature_yddot)){};

/// Methods of ReducedOrderModel
void ReducedOrderModel::CheckModelConsistency() const {
  DRAKE_DEMAND(B_tau_.rows() == n_yddot_);
  DRAKE_DEMAND(B_tau_.cols() == n_tau_);
  DRAKE_DEMAND(theta_y_.size() == n_y_ * n_feature_y_);
  DRAKE_DEMAND(theta_yddot_.size() == n_yddot_ * n_feature_yddot_);
};
Eigen::VectorXd ReducedOrderModel::theta() const {
  Eigen::VectorXd ret(theta_y_.size() + theta_yddot_.size());
  ret << theta_y_, theta_yddot_;
  return ret;
};
void ReducedOrderModel::SetThetaY(const VectorXd& theta_y) {
  DRAKE_DEMAND(theta_y_.size() == theta_y.size());
  theta_y_ = theta_y;
};
void ReducedOrderModel::SetThetaYddot(const VectorXd& theta_yddot) {
  DRAKE_DEMAND(theta_yddot_.size() == theta_yddot.size());
  theta_yddot_ = theta_yddot;
};
void ReducedOrderModel::SetTheta(const VectorXd& theta) {
  DRAKE_DEMAND(theta.size() == theta_y_.size() + theta_yddot_.size());
  theta_y_ = theta.head(theta_y_.size());
  theta_yddot_ = theta.tail(theta_yddot_.size());
};

VectorX<double> ReducedOrderModel::EvalMappingFunc(
    const VectorX<double>& q) const {
  VectorX<double> phi = EvalMappingFeat(q);

  VectorX<double> expression(n_y_);
  for (int i = 0; i < n_y_; i++) {
    expression(i) = theta_y_.segment(i * n_feature_y_, n_feature_y_).dot(phi);
  }
  return expression;
}
VectorX<double> ReducedOrderModel::EvalDynamicFunc(
    const VectorX<double>& y, const VectorX<double>& ydot,
    const VectorX<double>& tau) const {
  VectorX<double> phi = EvalDynamicFeat(y, ydot);

  VectorX<double> expression(n_yddot_);
  for (int i = 0; i < n_yddot_; i++) {
    expression(i) =
        theta_yddot_.segment(i * n_feature_yddot_, n_feature_yddot_).dot(phi);
  }
  expression += B_tau_ * tau;
  return expression;
}
VectorX<double> ReducedOrderModel::EvalMappingFuncJV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  VectorX<double> JV_feat = EvalMappingFeatJV(q, v);

  VectorX<double> JV(n_y_);
  for (int i = 0; i < n_y_; i++) {
    JV(i) = theta_y_.segment(i * n_feature_y_, n_feature_y_).dot(JV_feat);
  }
  return JV;
}
VectorX<double> ReducedOrderModel::EvalDynamicFuncJdotV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  VectorX<double> JdotV_feat = EvalDynamicFeatJdotV(q, v);

  VectorX<double> JdotV(n_y_);
  for (int i = 0; i < n_y_; i++) {
    JdotV(i) = theta_y_.segment(i * n_feature_y_, n_feature_y_).dot(JdotV_feat);
  }
  return JdotV;
}

/// LIPM
Lipm::Lipm(const MultibodyPlant<double>& plant,
           const BodyPoint& stance_contact_point,
           const MonomialFeatures& mapping_basis,
           const MonomialFeatures& dynamic_basis, int world_dim)
    : ReducedOrderModel(world_dim, 0, MatrixX<double>::Zero(world_dim, 0),
                        world_dim + mapping_basis.length(),
                        (world_dim - 1) + dynamic_basis.length(), mapping_basis,
                        dynamic_basis, to_string(world_dim) + "D lipm"),
      plant_(plant),
      context_(plant_.CreateDefaultContext()),
      world_(plant_.world_frame()),
      stance_contact_point_(stance_contact_point),
      world_dim_(world_dim) {
  DRAKE_DEMAND((world_dim == 2) || (world_dim == 3));

  // Initialize model parameters (dependant on the feature vectors)
  VectorXd theta_y = VectorXd::Zero(n_y() * n_feature_y());
  theta_y(0) = 1;
  theta_y(1 + n_feature_y()) = 1;
  if (world_dim == 3) {
    theta_y(2 + 2 * n_feature_y()) = 1;
  }
  SetThetaY(theta_y);

  VectorXd theta_yddot = VectorXd::Zero(n_yddot() * n_feature_yddot());
  theta_yddot(0) = 1;
  if (world_dim == 3) {
    theta_yddot(1 + n_feature_yddot()) = 1;
  }
  SetThetaYddot(theta_yddot);

  // Always check dimension after model construction
  CheckModelConsistency();
};
// Copy constructor
Lipm::Lipm(const Lipm& old_obj)
    : ReducedOrderModel(old_obj),
      plant_(old_obj.plant()),
      context_(old_obj.plant().CreateDefaultContext()),
      world_(old_obj.world()),
      stance_contact_point_(old_obj.stance_foot()),
      world_dim_(old_obj.world_dim()) {}

VectorX<double> Lipm::EvalMappingFeat(const VectorX<double>& q) const {
  // Get CoM position
  plant_.SetPositions(context_.get(), q);
  VectorX<double> CoM = plant_.CalcCenterOfMassPosition(*context_);
  // Stance foot position
  VectorX<double> stance_foot_pos(3);
  plant_.CalcPointsPositions(*context_, stance_contact_point_.second,
                             stance_contact_point_.first, plant_.world_frame(),
                             &stance_foot_pos);
  VectorX<double> st_to_CoM = CoM - stance_foot_pos;
  // cout << "CoM = " << CoM.transpose() << endl;
  // cout << "stance_foot_pos = " << stance_foot_pos.transpose() << endl;
  // cout << "CoM from MBP = " << CoM(0) << " " << CoM(2) << endl;
  // cout << "st_to_CoM from MBP = " << st_to_CoM(0) << " " << st_to_CoM(2) <<
  // endl;

  VectorX<double> feature(n_feature_y());
  if (world_dim_ == 2) {
    feature << st_to_CoM(0), st_to_CoM(2), mapping_basis().Eval(q);
  } else {
    feature << st_to_CoM, mapping_basis().Eval(q);
  }
  return feature;
}
VectorX<double> Lipm::EvalDynamicFeat(const VectorX<double>& y,
                                      const VectorX<double>& ydot) const {
  VectorX<double> feature_extension = y.head(world_dim_ - 1);
  double z = y(world_dim_ - 1);
  if (z == 0) {
    cout << "avoid singularity in dynamics_expression\n";
    feature_extension *= 9.80665 / (1e-8);
  } else {
    feature_extension *= 9.80665 / z;
  }

  VectorX<double> y_and_ydot(2 * n_y());
  y_and_ydot << y, ydot;

  VectorX<double> feature(n_feature_yddot());
  feature << feature_extension, dynamic_basis().Eval(y_and_ydot);
  return feature;
}
VectorX<double> Lipm::EvalMappingFeatJV(const VectorX<double>& q,
                                        const VectorX<double>& v) const {
  plant_.SetPositions(context_.get(), q);
  // Get CoM velocity
  MatrixX<double> J_com(3, plant_.num_velocities());
  plant_.CalcJacobianCenterOfMassTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, world_, world_, &J_com);
  // Stance foot velocity
  MatrixX<double> J_sf(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_, &J_sf);
  VectorX<double> JV_st_to_CoM = (J_com - J_sf) * v;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  if (world_dim_ == 2) {
    ret << JV_st_to_CoM(0), JV_st_to_CoM(2), mapping_basis().EvalJV(q, qdot);
  } else {
    ret << JV_st_to_CoM, mapping_basis().EvalJV(q, qdot);
  }
  return ret;
}
VectorX<double> Lipm::EvalDynamicFeatJdotV(const VectorX<double>& q,
                                           const VectorX<double>& v) const {
  VectorX<double> x(plant_.num_positions() + plant_.num_positions());
  x << q, v;
  plant_.SetPositionsAndVelocities(context_.get(), x);

  // Get CoM JdotV
  VectorX<double> JdotV_com =
      plant_.CalcBiasCenterOfMassTranslationalAcceleration(
          *context_, JacobianWrtVariable::kV, world_, world_);
  // Stance foot JdotV
  VectorX<double> JdotV_st = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_);
  VectorX<double> JdotV_st_to_com = JdotV_com - JdotV_st;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  if (world_dim_ == 2) {
    ret << JdotV_st_to_com(0), JdotV_st_to_com(2),
        mapping_basis().EvalJdotV(q, qdot);
  } else {
    ret << JdotV_st_to_com, mapping_basis().EvalJdotV(q, qdot);
  }
  return ret;
}

/// 2D LIPM with a 2D swing foot
const int TwoDimLipmWithSwingFoot::kDimension = 4;

TwoDimLipmWithSwingFoot::TwoDimLipmWithSwingFoot(
    const MultibodyPlant<double>& plant, const BodyPoint& stance_contact_point,
    const BodyPoint& swing_contact_point, const MonomialFeatures& mapping_basis,
    const MonomialFeatures& dynamic_basis)
    : ReducedOrderModel(
          kDimension, 2,
          (MatrixX<double>(kDimension, 2) << 0, 0, 0, 0, 1, 0, 0, 1).finished(),
          4 + mapping_basis.length(), 1 + dynamic_basis.length(), mapping_basis,
          dynamic_basis, "2D lipm with 2D swing foot"),
      plant_(plant),
      context_(plant_.CreateDefaultContext()),
      world_(plant_.world_frame()),
      stance_contact_point_(stance_contact_point),
      swing_contact_point_(swing_contact_point) {
  // Initialize model parameters (dependant on the feature vectors)
  VectorXd theta_y = VectorXd::Zero(n_y() * n_feature_y());
  VectorXd theta_yddot = VectorXd::Zero(n_yddot() * n_feature_yddot());
  theta_y(0) = 1;
  theta_y(1 + n_feature_y()) = 1;
  theta_y(2 + 2 * n_feature_y()) = 1;
  theta_y(3 + 3 * n_feature_y()) = 1;
  theta_yddot(0) = 1;
  SetThetaY(theta_y);
  SetThetaYddot(theta_yddot);

  // Always check dimension after model construction
  CheckModelConsistency();
};
// Copy constructor
TwoDimLipmWithSwingFoot::TwoDimLipmWithSwingFoot(
    const TwoDimLipmWithSwingFoot& old_obj)
    : ReducedOrderModel(old_obj),
      plant_(old_obj.plant()),
      context_(old_obj.plant().CreateDefaultContext()),
      world_(old_obj.world()),
      stance_contact_point_(old_obj.stance_foot()),
      swing_contact_point_(old_obj.swing_foot()) {}

VectorX<double> TwoDimLipmWithSwingFoot::EvalMappingFeat(
    const VectorX<double>& q) const {
  // Get CoM position
  plant_.SetPositions(context_.get(), q);
  VectorX<double> CoM = plant_.CalcCenterOfMassPosition(*context_);
  // Stance foot position
  VectorX<double> left_foot_pos(3);
  plant_.CalcPointsPositions(*context_, stance_contact_point_.second,
                             stance_contact_point_.first, plant_.world_frame(),
                             &left_foot_pos);
  VectorX<double> st_to_CoM = CoM - left_foot_pos;
  // Swing foot position
  VectorX<double> right_foot_pos(3);
  plant_.CalcPointsPositions(*context_, swing_contact_point_.second,
                             swing_contact_point_.first, plant_.world_frame(),
                             &right_foot_pos);
  VectorX<double> CoM_to_sw = right_foot_pos - CoM;
  // cout << "CoM = " << CoM.transpose() << endl;
  // cout << "left_foot_pos = " << left_foot_pos.transpose() << endl;
  // cout << "right_foot_pos = " << right_foot_pos.transpose() << endl;
  // cout << "CoM from MBP = " << CoM(0) << " " << CoM(2) << endl;
  // cout << "st_to_CoM from MBP = " << st_to_CoM(0) << " " << st_to_CoM(2) <<
  // endl; cout << "CoM_to_sw from MBP = " << CoM_to_sw(0) << " " <<
  // CoM_to_sw(2) << endl;

  VectorX<double> feature(n_feature_y());
  feature << st_to_CoM(0), st_to_CoM(2), CoM_to_sw(0), CoM_to_sw(2),
      mapping_basis().Eval(q);

  return feature;
}
VectorX<double> TwoDimLipmWithSwingFoot::EvalDynamicFeat(
    const VectorX<double>& y, const VectorX<double>& ydot) const {
  VectorX<double> feature_extension(1);
  if (y(1) == 0) {
    cout << "avoid singularity in dynamics_expression\n";
    feature_extension << (9.80665 / (y(1) + 1e-8)) * y(0);  // avoid singularity
  } else {
    feature_extension << (9.80665 / y(1)) * y(0);
  }

  VectorX<double> y_and_ydot(2 * kDimension);
  y_and_ydot << y, ydot;

  VectorX<double> feature(n_feature_yddot());
  feature << feature_extension(0), dynamic_basis().Eval(y_and_ydot);
  return feature;
}
VectorX<double> TwoDimLipmWithSwingFoot::EvalMappingFeatJV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  plant_.SetPositions(context_.get(), q);
  // Get CoM velocity
  MatrixX<double> J_com(3, plant_.num_velocities());
  plant_.CalcJacobianCenterOfMassTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, world_, world_, &J_com);
  // Stance foot velocity
  MatrixX<double> J_sf(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_, &J_sf);
  VectorX<double> JV_st_to_CoM = (J_com - J_sf) * v;
  // Swing foot velocity
  MatrixX<double> J_sw(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, swing_contact_point_.second,
      swing_contact_point_.first, world_, world_, &J_sw);
  VectorX<double> JV_CoM_to_sw = (J_sw - J_com) * v;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JV_st_to_CoM(0), JV_st_to_CoM(2), JV_CoM_to_sw(0), JV_CoM_to_sw(2),
      mapping_basis().EvalJV(q, qdot);
  return ret;
}
VectorX<double> TwoDimLipmWithSwingFoot::EvalDynamicFeatJdotV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  VectorX<double> x(plant_.num_positions() + plant_.num_positions());
  x << q, v;
  plant_.SetPositionsAndVelocities(context_.get(), x);

  // Get CoM JdotV
  VectorX<double> JdotV_com =
      plant_.CalcBiasCenterOfMassTranslationalAcceleration(
          *context_, JacobianWrtVariable::kV, world_, world_);
  // Stance foot JdotV
  VectorX<double> JdotV_st = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_);
  VectorX<double> JdotV_st_to_com = JdotV_com - JdotV_st;
  // Swing foot JdotV
  VectorX<double> JdotV_sw = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, swing_contact_point_.second,
      swing_contact_point_.first, world_, world_);
  VectorX<double> JdotV_com_to_sw = JdotV_sw - JdotV_com;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JdotV_st_to_com(0), JdotV_st_to_com(2), JdotV_com_to_sw(0),
      JdotV_com_to_sw(2), mapping_basis().EvalJdotV(q, qdot);
  return ret;
}

/// Fixed vertial COM acceleration
const int FixHeightAccel::kDimension = 1;

FixHeightAccel::FixHeightAccel(const MultibodyPlant<double>& plant,
                               const BodyPoint& stance_contact_point,
                               const MonomialFeatures& mapping_basis,
                               const MonomialFeatures& dynamic_basis)
    : ReducedOrderModel(kDimension, 0, MatrixX<double>::Zero(kDimension, 0),
                        1 + mapping_basis.length(), 0 + dynamic_basis.length(),
                        mapping_basis, dynamic_basis,
                        "Fixed COM vertical acceleration"),
      plant_(plant),
      context_(plant_.CreateDefaultContext()),
      world_(plant_.world_frame()),
      stance_contact_point_(stance_contact_point) {
  // Initialize model parameters (dependant on the feature vectors)
  VectorXd theta_y = VectorXd::Zero(n_y() * n_feature_y());
  VectorXd theta_yddot = VectorXd::Zero(n_yddot() * n_feature_yddot());
  theta_y(0) = 1;
  SetThetaY(theta_y);
  SetThetaYddot(theta_yddot);

  // Always check dimension after model construction
  CheckModelConsistency();
};
// Copy constructor
FixHeightAccel::FixHeightAccel(const FixHeightAccel& old_obj)
    : ReducedOrderModel(old_obj),
      plant_(old_obj.plant()),
      context_(old_obj.plant().CreateDefaultContext()),
      world_(old_obj.world()),
      stance_contact_point_(old_obj.stance_foot()) {}

VectorX<double> FixHeightAccel::EvalMappingFeat(
    const VectorX<double>& q) const {
  // Get CoM position
  plant_.SetPositions(context_.get(), q);
  VectorX<double> CoM = plant_.CalcCenterOfMassPosition(*context_);
  // Stance foot position
  VectorX<double> left_foot_pos(3);
  plant_.CalcPointsPositions(*context_, stance_contact_point_.second,
                             stance_contact_point_.first, plant_.world_frame(),
                             &left_foot_pos);
  VectorX<double> st_to_CoM = CoM - left_foot_pos;

  VectorX<double> feature(n_feature_y());
  feature << st_to_CoM(2), mapping_basis().Eval(q);

  return feature;
}
VectorX<double> FixHeightAccel::EvalDynamicFeat(
    const VectorX<double>& y, const VectorX<double>& ydot) const {
  VectorX<double> y_and_ydot(2 * kDimension);
  y_and_ydot << y, ydot;

  VectorX<double> feature(n_feature_yddot());
  feature << dynamic_basis().Eval(y_and_ydot);
  return feature;
}
VectorX<double> FixHeightAccel::EvalMappingFeatJV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  plant_.SetPositions(context_.get(), q);
  // Get CoM velocity
  MatrixX<double> J_com(3, plant_.num_velocities());
  plant_.CalcJacobianCenterOfMassTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, world_, world_, &J_com);
  // Stance foot velocity
  MatrixX<double> J_sf(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_, &J_sf);
  VectorX<double> JV_st_to_CoM = (J_com - J_sf) * v;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JV_st_to_CoM(2), mapping_basis().EvalJV(q, qdot);
  return ret;
}
VectorX<double> FixHeightAccel::EvalDynamicFeatJdotV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  VectorX<double> x(plant_.num_positions() + plant_.num_positions());
  x << q, v;
  plant_.SetPositionsAndVelocities(context_.get(), x);

  // Get CoM JdotV
  VectorX<double> JdotV_com =
      plant_.CalcBiasCenterOfMassTranslationalAcceleration(
          *context_, JacobianWrtVariable::kV, world_, world_);
  // Stance foot JdotV
  VectorX<double> JdotV_st = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_);
  VectorX<double> JdotV_st_to_com = JdotV_com - JdotV_st;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JdotV_st_to_com(2), mapping_basis().EvalJdotV(q, qdot);
  return ret;
}

/// Fixed vertial COM acceleration + 2D swing foot
const int FixHeightAccelWithSwingFoot::kDimension = 3;

FixHeightAccelWithSwingFoot::FixHeightAccelWithSwingFoot(
    const MultibodyPlant<double>& plant, const BodyPoint& stance_contact_point,
    const BodyPoint& swing_contact_point, const MonomialFeatures& mapping_basis,
    const MonomialFeatures& dynamic_basis)
    : ReducedOrderModel(
          kDimension, 2,
          (MatrixX<double>(kDimension, 2) << 0, 0, 1, 0, 0, 1).finished(),
          3 + mapping_basis.length(), 0 + dynamic_basis.length(), mapping_basis,
          dynamic_basis, "Fixed COM vertical acceleration + 2D swing foot"),
      plant_(plant),
      context_(plant_.CreateDefaultContext()),
      world_(plant_.world_frame()),
      stance_contact_point_(stance_contact_point),
      swing_contact_point_(swing_contact_point) {
  // Initialize model parameters (dependant on the feature vectors)
  VectorXd theta_y = VectorXd::Zero(n_y() * n_feature_y());
  VectorXd theta_yddot = VectorXd::Zero(n_yddot() * n_feature_yddot());
  theta_y(0) = 1;
  theta_y(1 + n_feature_y()) = 1;
  theta_y(2 + 2 * n_feature_y()) = 1;
  SetThetaY(theta_y);
  SetThetaYddot(theta_yddot);

  // Always check dimension after model construction
  CheckModelConsistency();
};
// Copy constructor
FixHeightAccelWithSwingFoot::FixHeightAccelWithSwingFoot(
    const FixHeightAccelWithSwingFoot& old_obj)
    : ReducedOrderModel(old_obj),
      plant_(old_obj.plant()),
      context_(old_obj.plant().CreateDefaultContext()),
      world_(old_obj.world()),
      stance_contact_point_(old_obj.stance_foot()),
      swing_contact_point_(old_obj.swing_foot()) {}

VectorX<double> FixHeightAccelWithSwingFoot::EvalMappingFeat(
    const VectorX<double>& q) const {
  // Get CoM position
  plant_.SetPositions(context_.get(), q);
  VectorX<double> CoM = plant_.CalcCenterOfMassPosition(*context_);
  // Stance foot position
  VectorX<double> left_foot_pos(3);
  plant_.CalcPointsPositions(*context_, stance_contact_point_.second,
                             stance_contact_point_.first, plant_.world_frame(),
                             &left_foot_pos);
  VectorX<double> st_to_CoM = CoM - left_foot_pos;
  // Swing foot position
  VectorX<double> right_foot_pos(3);
  plant_.CalcPointsPositions(*context_, swing_contact_point_.second,
                             swing_contact_point_.first, plant_.world_frame(),
                             &right_foot_pos);
  VectorX<double> CoM_to_sw = right_foot_pos - CoM;

  VectorX<double> feature(n_feature_y());
  feature << st_to_CoM(2), CoM_to_sw(0), CoM_to_sw(2), mapping_basis().Eval(q);

  return feature;
}
VectorX<double> FixHeightAccelWithSwingFoot::EvalDynamicFeat(
    const VectorX<double>& y, const VectorX<double>& ydot) const {
  VectorX<double> y_and_ydot(2 * kDimension);
  y_and_ydot << y, ydot;

  VectorX<double> feature(n_feature_yddot());
  feature << dynamic_basis().Eval(y_and_ydot);
  return feature;
}
VectorX<double> FixHeightAccelWithSwingFoot::EvalMappingFeatJV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  plant_.SetPositions(context_.get(), q);
  // Get CoM velocity
  MatrixX<double> J_com(3, plant_.num_velocities());
  plant_.CalcJacobianCenterOfMassTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, world_, world_, &J_com);
  // Stance foot velocity
  MatrixX<double> J_sf(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_, &J_sf);
  VectorX<double> JV_st_to_CoM = (J_com - J_sf) * v;
  // Swing foot velocity
  MatrixX<double> J_sw(3, plant_.num_velocities());
  plant_.CalcJacobianTranslationalVelocity(
      *context_, JacobianWrtVariable::kV, swing_contact_point_.second,
      swing_contact_point_.first, world_, world_, &J_sw);
  VectorX<double> JV_CoM_to_sw = (J_sw - J_com) * v;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JV_st_to_CoM(2), JV_CoM_to_sw(0), JV_CoM_to_sw(2),
      mapping_basis().EvalJV(q, qdot);
  return ret;
}
VectorX<double> FixHeightAccelWithSwingFoot::EvalDynamicFeatJdotV(
    const VectorX<double>& q, const VectorX<double>& v) const {
  VectorX<double> x(plant_.num_positions() + plant_.num_positions());
  x << q, v;
  plant_.SetPositionsAndVelocities(context_.get(), x);

  // Get CoM JdotV
  VectorX<double> JdotV_com =
      plant_.CalcBiasCenterOfMassTranslationalAcceleration(
          *context_, JacobianWrtVariable::kV, world_, world_);
  // Stance foot JdotV
  VectorX<double> JdotV_st = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, stance_contact_point_.second,
      stance_contact_point_.first, world_, world_);
  VectorX<double> JdotV_st_to_com = JdotV_com - JdotV_st;
  // Swing foot JdotV
  VectorX<double> JdotV_sw = plant_.CalcBiasTranslationalAcceleration(
      *context_, JacobianWrtVariable::kV, swing_contact_point_.second,
      swing_contact_point_.first, world_, world_);
  VectorX<double> JdotV_com_to_sw = JdotV_sw - JdotV_com;

  // Convert v to qdot
  VectorX<double> qdot(plant_.num_positions());
  plant_.MapVelocityToQDot(*context_, v, &qdot);

  VectorX<double> ret(n_feature_y());
  ret << JdotV_st_to_com(2), JdotV_com_to_sw(0), JdotV_com_to_sw(2),
      mapping_basis().EvalJdotV(q, qdot);
  return ret;
}

}  // namespace goldilocks_models
}  // namespace dairlib