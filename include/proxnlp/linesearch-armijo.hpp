/// @file linesearch-armijo.hpp
/// @copyright Copyright (C) 2022 LAAS-CNRS, INRIA
/// @brief  Implements a basic Armijo back-tracking strategy.
#pragma once

#include "proxnlp/linesearch-base.hpp"
#include "proxnlp/exceptions.hpp"

#include <Eigen/QR>

namespace proxnlp {

/// @brief Polynomials represented by their coefficients in decreasing order of
/// degree.
template <typename T> struct PolynomialTpl {
  using VectorXs = typename math_types<T>::VectorXs;
  VectorXs coeffs;
  PolynomialTpl() {}
  PolynomialTpl(const Eigen::Ref<const VectorXs> &c) : coeffs(c) {}
  /// @brief Polynomial degree (number of coefficients minus one).
  std::size_t degree() const { return coeffs.size() - 1; }
  inline T evaluate(T a) const {
    T r = 0.0;
    for (int i = 0; i < coeffs.size(); i++) {
      r = r * a + coeffs(i);
    }
    return r;
  }
  PolynomialTpl derivative() const {
    if (degree() == 0) {
      return PolynomialTpl({0.});
    }
    VectorXs out(degree());
    for (int i = 0; i < coeffs.size() - 1; i++) {
      out(i) = coeffs(i) * (degree() - i);
    }
    return PolynomialTpl(out);
  }
};

/// @brief  Basic backtracking Armijo line-search strategy.
template <typename Scalar> class ArmijoLinesearch : public Linesearch<Scalar> {
public:
  using Base = Linesearch<Scalar>;
  using FunctionSample = typename Base::FunctionSample;
  using Polynomial = PolynomialTpl<Scalar>;
  using VectorXs = typename math_types<Scalar>::VectorXs;
  using Base::options;

  Polynomial interpol;

  ArmijoLinesearch(const typename Base::Options &options)
      : Base(options), lu(alph_mat) {}

  template <typename Fn>
  Scalar run(Fn phi, const Scalar phi0, const Scalar dphi0, Scalar &alpha_try) {
    const FunctionSample lower_bound(0., phi0, dphi0);

    alpha_try = 1.;
    FunctionSample latest;
    FunctionSample previous;

    // try the full step; if failure encountered, aggressively
    // backtrack until no exception is raised
    while (true) {
      try {
        latest = FunctionSample(alpha_try, phi(alpha_try));
        break;
      } catch (const std::runtime_error &e) {
        alpha_try *= 0.5;
        if (alpha_try <= options().alpha_min) {
          alpha_try = options().alpha_min;
        }
      }
    }

    if (std::abs(dphi0) < options().dphi_thresh) {
      return alpha_try;
    }

    for (std::size_t i = 0; i < options().max_num_steps; i++) {

      const Scalar dM = latest.phi - phi0;
      if (dM <= options().armijo_c1 * alpha_try * dphi0) {
        break;
      }

      // compute next alpha try
      LSInterpolation strat = options().interp_type;
      if (strat == LSInterpolation::BISECTION) {
        alpha_try *= 0.5;
      } else {
        std::vector<FunctionSample> samples = {lower_bound};

        // interpolation routines
        switch (strat) {
        case LSInterpolation::QUADRATIC: {
          // 2-point interp: value, derivative at 0 and latest value
          samples.push_back(latest);
          break;
        }
        case LSInterpolation::CUBIC: {
          // 3-point interp: phi(0), phi'(0) and last two values
          samples.push_back(latest);
          if (previous.valid) {
            samples.push_back(previous);
          }
          break;
        }
        default:
          proxnlp_runtime_error(
              "Unrecognized interpolation type in this context.\n");
          break;
        }

        alpha_try = this->minimize_interpolant(
            strat, samples, options().contraction_min * alpha_try,
            options().contraction_max * alpha_try);
      }

      if (std::isnan(alpha_try)) {
        // handle NaN case
        alpha_try = options().contraction_min * previous.alpha;
      } else {
        alpha_try = std::max(alpha_try, options().alpha_min);
      }

      try {
        previous = latest;
        latest = FunctionSample(alpha_try, phi(alpha_try));
      } catch (const std::runtime_error &e) {
        continue;
      }

      if (alpha_try <= options().alpha_min) {
        break;
      }
    }
    return latest.phi;
  }

  using Matrix2s = Eigen::Matrix<Scalar, 2, 2>;
  using Vector2s = Eigen::Matrix<Scalar, 2, 1>;
  Matrix2s alph_mat;
  Vector2s alph_rhs;
  Vector2s coeffs_cubic_interpolant;
  Eigen::HouseholderQR<Eigen::Ref<Matrix2s>> lu;

  /// Propose a new candidate step size through safeguarded interpolation
  Scalar minimize_interpolant(LSInterpolation strat,
                              const std::vector<FunctionSample> &samples,
                              Scalar min_step_size, Scalar max_step_size) {
    Scalar anext = 0.0;
    VectorXs &coeffs = interpol.coeffs;

    assert(samples.size() >= 2);
    const FunctionSample &lower_bound = samples[0];
    const Scalar &phi0 = lower_bound.phi;
    const Scalar &dphi0 = lower_bound.dphi;

    if (samples.size() == 2) {
      strat = LSInterpolation::QUADRATIC;
    }

    switch (strat) {
    case LSInterpolation::QUADRATIC: {

      const FunctionSample &cand0 = samples[1];
      Scalar a = (cand0.phi - phi0 - cand0.alpha * dphi0) /
                 (cand0.alpha * cand0.alpha);
      coeffs.conservativeResize(3);
      coeffs << a, dphi0, phi0;
      assert(interpol.degree() == 2);
      anext = -dphi0 / (2. * a);
      break;
    }
    case LSInterpolation::CUBIC: {

      const FunctionSample &cand0 = samples[1];
      const FunctionSample &cand1 = samples[2];
      const Scalar &a0 = cand0.alpha;
      const Scalar &a1 = cand1.alpha;
      alph_mat(0, 0) = a0 * a0 * a0;
      alph_mat(0, 1) = a0 * a0;
      alph_mat(1, 0) = a1 * a1 * a1;
      alph_mat(1, 1) = a1 * a1;

      alph_rhs(0) = cand1.phi - phi0 - dphi0 * a1;
      alph_rhs(1) = cand0.phi - phi0 - dphi0 * a0;

      lu.compute(alph_mat);
      coeffs_cubic_interpolant = lu.solve(alph_rhs);

      const Scalar c3 = coeffs_cubic_interpolant(0);
      const Scalar c2 = coeffs_cubic_interpolant(1);
      coeffs.conservativeResize(4);
      coeffs << c3, c2, dphi0, phi0;
      assert(interpol.degree() == 3);

      // minimizer of cubic interpolant -> solve dinterp/da = 0
      anext = (-c2 + std::sqrt(c2 * c2 - 3.0 * c3 * dphi0)) / (3.0 * c3);
      break;
    }
    default:
      break;
    }

    if ((anext > max_step_size) || (anext < min_step_size)) {
      // if min outside of (amin; amax), look at the edges
      Scalar pleft = interpol.evaluate(min_step_size);
      Scalar pright = interpol.evaluate(max_step_size);
      if (pleft < pright) {
        anext = min_step_size;
      } else {
        anext = max_step_size;
      }
    }

    return anext;
  }
};

} // namespace proxnlp
