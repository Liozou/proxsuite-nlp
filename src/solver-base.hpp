/** Copyright (c) 2022 LAAS-CNRS, INRIA
 * 
 */
#pragma once

#include <Eigen/Core>
#include <Eigen/Cholesky>
#include <cassert>

#include <fmt/core.h>
#include <fmt/ostream.h>

#include "lienlp/macros.hpp"
#include "lienlp/problem-base.hpp"
#include "lienlp/meritfuncs/pdal.hpp"
#include "lienlp/workspace.hpp"
#include "lienlp/results.hpp"


namespace lienlp {

  template<typename M>
  class Solver
  {
  public:
    using Scalar = typename M::Scalar;
    LIENLP_DEFINE_DYNAMIC_TYPES(Scalar)
    using Prob_t = Problem<Scalar>;
    using Merit_t = PDALFunction<Scalar>;

    using Workspace = SWorkspace<Scalar>;
    using Results = SResults<Scalar>;

    shared_ptr<Prob_t> problem;
    Merit_t meritFun;
    M& manifold;

    //// Other settings

    bool verbose = true;

    //// Algo params which evolve

    Scalar dualTol;
    Scalar primTol;
    Scalar rho;
    Scalar muEq;
    Scalar muEqInv;
    Scalar muFactor;
    Scalar rhoFactor = muFactor;

    const Scalar MU_MIN = 1e-9;

    //// Algo hyperparams

    Scalar targetTol;
    const Scalar alphaDual;
    const Scalar betaDual;
    const Scalar alphaPrim;
    const Scalar betaPrim;

    Solver(M& man,
           shared_ptr<Prob_t>& prob,
           const Scalar tol=1e-6,
           const Scalar muEq=1e-2,
           const Scalar rho=0.,
           const Scalar muFactor=0.1,
           const Scalar muMin=1e-9,
           const Scalar alphaPrim=0.1,
           const Scalar betaPrim=0.9,
           const Scalar alphaDual=1.,
           const Scalar betaDual=1.)
      : manifold(man),
        problem(prob),
        meritFun(prob),
        targetTol(tol),
        muEq(muEq),
        rho(rho),
        muFactor(muFactor),
        MU_MIN(muMin),
        alphaPrim(alphaPrim),
        betaPrim(betaPrim),
        alphaDual(alphaDual),
        betaDual(betaDual)
    {
      meritFun.setPenalty(muEq);
    }

    ConvergedFlag
    solve(Workspace& workspace,
          Results& results,
          const VectorXs& x0,
          const VectorOfVectors& lams0)
    {
      // init variables
      results.xOpt = x0;
      results.lamsOpt = lams0;


      dualTol = 1.;
      primTol = 1.;
      updateToleranceFailure();


      std::size_t i = 0;
      while (results.numIters < MAX_ITERS)
      {
        fmt::print("[Iter {:d}] omega={}, eta={}\n", i, dualTol, primTol);
        solveInner(workspace, results);

        // accept new primal iterate
        workspace.xPrev = results.xOpt;

        if (workspace.primalInfeas < primTol)
        {
          if ((workspace.primalInfeas < targetTol) && (workspace.dualInfeas < targetTol))
          {
            // terminate algorithm
            results.converged = ConvergedFlag::SUCCESS;
            break;
          }
          // accept dual iterate
          // TODO fix to avoid recomputing 1st order multipliers
          acceptMultipliers(workspace, results.lamsOpt);
          updateToleranceSuccess();
        } else {
          updatePenalty();
          updateToleranceFailure();
        }
        results.mu = muEq;
        // safeguard tolerances
        dualTol = std::max(dualTol, targetTol);

        i++;
      }

      return results.converged;
    }

    // Set solver convergence threshold
    void setTolerance(const Scalar tol) { targetTol = tol; }
    // Set solver maximum iteration number
    void setMaxIters(const std::size_t val) { MAX_ITERS = val; }

    /// Update penalty parameter and propagate side-effects.
    inline void updatePenalty()
    {
      muEq = std::max(muEq * muFactor, MU_MIN);
      muEqInv = 1. / muEq;
      meritFun.setPenalty(muEq);
    }

  protected:
    const std::size_t MAX_ITERS = 200;

    void solveInner(Workspace& workspace, Results& results)
    {
      const auto ndx = manifold.ndx();
      VectorXs& x = results.xOpt; // shorthand
      const std::size_t num_c = problem->getNumConstraints();

      bool inner_conv;

      std::size_t k;
      for (k = 0; k < MAX_ITERS; k++)
      {

        //// precompute temp data

        results.value = problem->m_cost(x);
        problem->m_cost.computeGradient(x, workspace.objectiveGradient);
        problem->m_cost.computeHessian(x, workspace.objectiveHessian);

        computeResidualsAndMultipliers(x, workspace, results.lamsOpt);
        computeResidualDerivatives(x, workspace);

        int nc = 0;   // constraint size
        int cursor = ndx;  // starts after ndx (primal grad size)

        //// fill in LHS/RHS
        //// TODO create an Eigen::Map to map submatrices to the active sets of each constraint

        auto idx_prim = Eigen::seq(0, ndx - 1);
        for (std::size_t i = 0; i < num_c; i++)
        {

          // fill in the dual part of the KKT
          nc = problem->getCstr(i)->nr();
          auto block_slice = Eigen::seq(cursor, cursor + nc - 1);
          fmt::print("[{}] cursor at {:d} (cstr size = {:d})\n", __func__, cursor, nc);
          workspace.kktRhs(block_slice) = workspace.auxProxDualErr[i];
          // jacobian block and transpose
          workspace.kktMatrix(block_slice, idx_prim) = workspace.cstrJacobians[i];
          workspace.kktMatrix(idx_prim, block_slice) = workspace.cstrJacobians[i].transpose();
          // reg block
          workspace.kktMatrix(block_slice, block_slice).setIdentity();
          workspace.kktMatrix(block_slice, block_slice).array() *= -muEq;

          cursor += nc;
        }
        // now fill in the 0-ndx prefixes
        auto& lagrangian = meritFun.m_lagr;
        lagrangian.computeGradient(x, workspace.lamsPDAL, workspace.kktRhs(idx_prim));
        lagrangian.computeHessian(x, workspace.lamsPDAL, workspace.kktMatrix(idx_prim, idx_prim));

        fmt::print("[{}]\n{} << kkt RHS\n", __func__, workspace.kktRhs.transpose());
        fmt::print("[{}]\n{} << kkt LHS\n", __func__, workspace.kktMatrix);

        // now check if we can stop
        workspace.dualResidual = workspace.kktRhs(idx_prim);
        workspace.dualInfeas = infNorm(workspace.dualResidual);
        inner_conv = infNorm(workspace.kktRhs) < dualTol;

        fmt::print("[{}] dual Infeas: {}\n", __func__, workspace.dualInfeas);

        if (inner_conv) return;

        // factorization
        workspace.ldlt_.compute(workspace.kktMatrix);
        workspace.pdStep = workspace.ldlt_.solve(-workspace.kktRhs);

        workspace.signature.array() = (workspace.ldlt_.vectorD().array() > 0);
        fmt::print("[{}] KKT signature:\n{}\n", __func__, workspace.signature.transpose());

        assert(workspace.ldlt_.info() == Eigen::ComputationInfo::Success);

        //// Take the step
        // TODO implement linesearch
        manifold.integrate(x, workspace.pdStep(idx_prim), x);
        cursor = ndx;
        for (std::size_t i = 0; i < num_c; i++)
        {
          nc = problem->getCstr(i)->nr();

          auto block_slice = Eigen::seq(cursor, cursor + nc - 1);
          results.lamsOpt[i].noalias() += workspace.pdStep(block_slice);

          cursor += nc;
        }

        fmt::print("[{}] current x:\n{}\n", __func__, x);

        results.numIters++;
        if (results.numIters >= MAX_ITERS)
        {
          results.converged = ConvergedFlag::TOO_MANY_ITERS;
          break;
        }
      }

      if (k == MAX_ITERS)
        results.converged = ConvergedFlag::TOO_MANY_ITERS;
    }

    void updateToleranceFailure()
    {
      primTol = primTol * std::pow(muEq, alphaPrim);
      dualTol = dualTol * std::pow(muEq, alphaDual);
    }

    void updateToleranceSuccess()
    {
      primTol = primTol * std::pow(muEq, betaPrim);
      dualTol = dualTol * std::pow(muEq, betaDual);
    }

    void acceptMultipliers(Workspace& workspace, VectorOfVectors& lams) const
    {
      const auto nc = problem->getNumConstraints();
      for (std::size_t i = 0; i < nc; i++)
      {
        // copy the (cached) estimates from the algo
        workspace.lamsPrev[i] = workspace.lamsPDAL[i];
      }
    }

    /// Evaluate the primal residuals, etc
    void computeResidualsAndMultipliers(
      const ConstVectorRef& x,
      Workspace& workspace,
      VectorOfVectors& lams) const
    {
      for (std::size_t i = 0; i < problem->getNumConstraints(); i++)
      {
        auto cstr = problem->getCstr(i);
        workspace.primalResiduals[i] = cstr->m_func(x);

        // multiplier
        workspace.lamsPlus[i] = workspace.lamsPrev[i] + workspace.primalResiduals[i] * muEqInv;
        workspace.lamsPlus[i].noalias() = cstr->dualProjection(workspace.lamsPlus[i]);
        workspace.auxProxDualErr[i] = muEq * (workspace.lamsPlus[i] - lams[i]);
        workspace.lamsPDAL[i] = 2 * workspace.lamsPlus[i] - lams[i];
      } 

      // update primal infeas measure
      workspace.primalInfeas = 0.;
      for (std::size_t i = 0; i < problem->getNumConstraints(); i++)
      {
        workspace.primalInfeas = std::max(
          workspace.primalInfeas,
          infNorm(workspace.primalResiduals[i]));
      }
    }

    /// Evaluate the constraint jacobians, vhp
    void computeResidualDerivatives(
      const ConstVectorRef& x,
      Workspace& workspace) const
    {
      for (std::size_t i = 0; i < problem->getNumConstraints(); i++)
      {
        auto cstr = problem->getCstr(i);

        MatrixXs& J_ = workspace.cstrJacobians[i];
        cstr->m_func.computeJacobian(x, J_);
        MatrixXs projJac = cstr->JdualProjection(workspace.lamsPlus[i]);
        J_.noalias() = projJac * J_;
        cstr->m_func.vhp(x, workspace.lamsPDAL[i], workspace.cstrVectorHessProd[i]);
      }
    } 

  };

} // namespace lienlp
