/// @file
/// @author Sarah El-Kazdadi
/// @author Wilson Jallet
/// @copyright Copyright (C) 2022-2023 LAAS-CNRS, INRIA
#pragma once

#include "./blocks.hpp"

namespace proxnlp {
namespace linalg {

template <typename Scalar>
void BlockLDLT<Scalar>::setPermutation(isize const *new_perm) {
  auto in = m_structure.copy();
  const isize n = m_structure.nsegments();
  if (new_perm != nullptr)
    std::copy_n(new_perm, n, m_perm);
  m_structure.performed_llt = false;
  symbolic_deep_copy(in, m_structure, m_perm);
  analyzePattern();
}

template <typename Scalar>
void BlockLDLT<Scalar>::updateBlockPermutationMatrix(
    const SymbolicBlockMatrix &in) {
  const isize *row_segs = in.segment_lens;
  const isize nblocks = in.nsegments();
  using IndicesType = PermutationType::IndicesType;
  IndicesType &indices = m_permutation.indices();
  isize idx = 0;
  for (isize i = 0; i < nblocks; ++i) {
    m_idx[i] = idx;
    idx += row_segs[i];
  }

  idx = 0;
  for (isize i = 0; i < nblocks; ++i) {
    auto len = row_segs[m_perm[i]];
    auto s = indices.segment(idx, len);
    isize i0 = m_idx[m_perm[i]];
    s.setLinSpaced(i0, i0 + len - 1);
    idx += len;
  }
  m_permutation = m_permutation.transpose();
}

template <typename Scalar>
typename BlockLDLT<Scalar>::MatrixXs
BlockLDLT<Scalar>::reconstructedMatrix() const {
  MatrixXs res(m_matrix.rows(), m_matrix.cols());
  res.setIdentity();
  backend::dense_ldlt_reconstruct<Scalar>(m_matrix, res);
  res.noalias() = res * permutationP();
  res.noalias() = permutationP().transpose() * res;
  return res;
}

template <typename Scalar>
bool BlockLDLT<Scalar>::solveInPlace(MatrixRef b) const {

  b.noalias() = permutationP() * b;
  PROXNLP_NOMALLOC_BEGIN;
  bool flag = backend::dense_ldlt_solve_in_place(m_matrix, b.derived());
  PROXNLP_NOMALLOC_END;
  b.noalias() = permutationP().transpose() * b;
  return flag;
}

} // namespace linalg
} // namespace proxnlp