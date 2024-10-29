//========================================================================================
// AthenaK: astrophysical fluid dynamics & numerical relativity code
// Copyright(C) 2020 James M. Stone <jmstone@ias.edu> and the Athena code team
// Licensed under the 3-clause BSD License (the "LICENSE")
//========================================================================================
//! \file z4c_spectre_bbh.cpp
//  \brief Problem generator for binary black hole initial data generated by
//  the SpECTRE code (https://spectre-code.org).

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "athena.hpp"
#include "coordinates/cell_locations.hpp"
#include "coordinates/adm.hpp"
#include "globals.hpp"
#include "mesh/mesh.hpp"
#include "parameter_input.hpp"
#include "z4c/z4c.hpp"
#include "z4c/z4c_amr.hpp"
#include "z4c/z4c_puncture_tracker.hpp"

#include <spectre/Exporter.hpp>

// Forward declarations
void LoadSpectreInitialData(MeshBlockPack *pmbp, const std::string &filename_glob,
                            const std::string &subfile_name, const int observation_step);
void RefinementCondition(MeshBlockPack *pmbp);

//----------------------------------------------------------------------------------------
//! \fn ProblemGenerator::UserProblem_()
//! \brief Problem Generator for SpECTRE binary black hole initial data
//
// The following options specify the initial data to load:
//
// - problem.id_filename_glob: A glob pattern that selects the SpECTRE initial
//   data files, e.g. "path/to/data/BbhVolume*.h5".
// - problem.id_subfile_name: The name of the subfile within the H5 files that
//   contains the initial data, e.g. "VolumeData".
// - problem.id_observation_step: (Optional) The observation step to load from
//   the H5 files, in case the initial data is stored at multiple refinement
//   steps. Set to -1 to load the last step (this is the default).
void ProblemGenerator::UserProblem(ParameterInput *pin, const bool restart) {
  user_ref_func = RefinementCondition;

  if (restart)
    return;

  MeshBlockPack *pmbp = pmy_mesh_->pmb_pack;
  auto &indcs = pmy_mesh_->mb_indcs;

  // Load initial data specified by the user options
  const std::string options_block = "problem";
  LoadSpectreInitialData(
      pmbp,
      pin->GetOrAddString(options_block, "id_filename_glob", "EMPTY"),
      pin->GetOrAddString(options_block, "id_subfile_name", "EMPTY"),
      pin->GetOrAddInteger(options_block, "id_observation_step", -1));

  // Set lapse from conformal factor
  pmbp->pz4c->GaugePreCollapsedLapse(pmbp, pin);

  // Set Z4c variables from ADM variables
  switch (indcs.ng) {
  case 2:
    pmbp->pz4c->ADMToZ4c<2>(pmbp, pin);
    break;
  case 3:
    pmbp->pz4c->ADMToZ4c<3>(pmbp, pin);
    break;
  case 4:
    pmbp->pz4c->ADMToZ4c<4>(pmbp, pin);
    break;
  }

  // Compute ADM constrains on initial data slice
  switch (indcs.ng) {
  case 2:
    pmbp->pz4c->ADMConstraints<2>(pmbp);
    break;
  case 3:
    pmbp->pz4c->ADMConstraints<3>(pmbp);
    break;
  case 4:
    pmbp->pz4c->ADMConstraints<4>(pmbp);
    break;
  }

  std::cout << "Loading initial data complete." << std::endl;
}

//! \brief Interpolate SpECTRE initial data to AthenaK mesh
void LoadSpectreInitialData(MeshBlockPack *pmbp, const std::string &filename_glob,
                            const std::string &subfile_name, const int observation_step) {
  auto &u_adm = pmbp->padm->u_adm;
  HostArray5D<Real>::HostMirror host_u_adm = create_mirror(u_adm);
  z4c::Z4c::ADMhost_vars host_adm;
  host_adm.psi4.InitWithShallowSlice(host_u_adm, adm::ADM::I_ADM_PSI4);
  host_adm.g_dd.InitWithShallowSlice(
      host_u_adm, adm::ADM::I_ADM_GXX, adm::ADM::I_ADM_GZZ);
  host_adm.vK_dd.InitWithShallowSlice(
      host_u_adm, adm::ADM::I_ADM_KXX, adm::ADM::I_ADM_KZZ);
  auto &indcs = pmbp->pmesh->mb_indcs;
  auto &size = pmbp->pmb->mb_size;
  int &is = indcs.is;
  int &ie = indcs.ie;
  int &js = indcs.js;
  int &je = indcs.je;
  int &ks = indcs.ks;
  int &ke = indcs.ke;
  // For GLOOPS
  int isg = is - indcs.ng;
  int ieg = ie + indcs.ng;
  int jsg = js - indcs.ng;
  int jeg = je + indcs.ng;
  int ksg = ks - indcs.ng;
  int keg = ke + indcs.ng;

  int nx1 = indcs.nx1;
  int nx2 = indcs.nx2;
  int nx3 = indcs.nx3;
  int ncells1 = nx1 + 2 * (indcs.ng);
  int ncells2 = nx2 + 2 * (indcs.ng);
  int ncells3 = nx3 + 2 * (indcs.ng);
  int n[3] = {ncells1, ncells2, ncells3};
  int sz = n[0] * n[1] * n[2];

  // Allocate memory for the coordinates
  std::array<std::vector<double>, 3> x{};
  x[0].resize(sz);
  x[1].resize(sz);
  x[2].resize(sz);

  int nmb = pmbp->nmb_thispack;
  for (int m = 0; m < nmb; ++m) {
    // Get the coordinates for this meshblock
    Real &x1min = size.h_view(m).x1min;
    Real &x1max = size.h_view(m).x1max;
    Real &x2min = size.h_view(m).x2min;
    Real &x2max = size.h_view(m).x2max;
    Real &x3min = size.h_view(m).x3min;
    Real &x3max = size.h_view(m).x3max;
    for (int ix_I = isg; ix_I < ieg + 1; ix_I++) {
      for (int ix_J = jsg; ix_J < jeg + 1; ix_J++) {
        for (int ix_K = ksg; ix_K < keg + 1; ix_K++) {
          int flat_ix = ix_I + n[0] * (ix_J + n[1] * ix_K);
          x[0][flat_ix] = CellCenterX(ix_I - is, nx1, x1min, x1max);
          x[1][flat_ix] = CellCenterX(ix_J - js, nx2, x2min, x2max);
          x[2][flat_ix] = CellCenterX(ix_K - ks, nx3, x3min, x3max);
        }
      }
    }

    // Interpolate data to the coordinates
    std::cout << "Interpolating initial data for meshblock " << m << "/"
              << nmb - 1 << " with " << sz << " points " << std::endl;
    const auto data = spectre::Exporter::interpolate_to_points<3>(
        filename_glob, subfile_name, spectre::Exporter::ObservationStep{observation_step},
        {"SpatialMetric_xx", "SpatialMetric_yx", "SpatialMetric_yy",
         "SpatialMetric_zx", "SpatialMetric_zy", "SpatialMetric_zz",
         "ExtrinsicCurvature_xx", "ExtrinsicCurvature_yx", "ExtrinsicCurvature_yy",
         "ExtrinsicCurvature_zx", "ExtrinsicCurvature_zy", "ExtrinsicCurvature_zz"},
        /* target_points */ x,
        /* extrapolate_into_excisions */ true);
    const auto &gxx = data[0];
    const auto &gyx = data[1];
    const auto &gyy = data[2];
    const auto &gzx = data[3];
    const auto &gzy = data[4];
    const auto &gzz = data[5];
    const auto &Kxx = data[6];
    const auto &Kyx = data[7];
    const auto &Kyy = data[8];
    const auto &Kzx = data[9];
    const auto &Kzy = data[10];
    const auto &Kzz = data[11];

    // Move the interpolated data into the meshblock
    for (int k = ksg; k <= keg; k++)
      for (int j = jsg; j <= jeg; j++)
        for (int i = isg; i <= ieg; i++) {
          int flat_ix = i + n[0] * (j + n[1] * k);

          host_adm.g_dd(m, 0, 0, k, j, i) = gxx[flat_ix];
          host_adm.g_dd(m, 1, 1, k, j, i) = gyy[flat_ix];
          host_adm.g_dd(m, 2, 2, k, j, i) = gzz[flat_ix];
          host_adm.g_dd(m, 0, 1, k, j, i) = gyx[flat_ix];
          host_adm.g_dd(m, 0, 2, k, j, i) = gzx[flat_ix];
          host_adm.g_dd(m, 1, 2, k, j, i) = gzy[flat_ix];

          host_adm.vK_dd(m, 0, 0, k, j, i) = Kxx[flat_ix];
          host_adm.vK_dd(m, 1, 1, k, j, i) = Kyy[flat_ix];
          host_adm.vK_dd(m, 2, 2, k, j, i) = Kzz[flat_ix];
          host_adm.vK_dd(m, 0, 1, k, j, i) = Kyx[flat_ix];
          host_adm.vK_dd(m, 0, 2, k, j, i) = Kzx[flat_ix];
          host_adm.vK_dd(m, 1, 2, k, j, i) = Kzy[flat_ix];

          // Compute conformal factor such that the conformal metric has unit
          // determinant.
          // The conformal decomposition of the spatial metric is:
          //   g_ij = psi^4 \bar{g}_{ij}
          // So, to impose unit determinant on the conformal metric, we set:
          //   psi^4 = det(g)^{1/3}
          Real detg = adm::SpatialDet(host_adm.g_dd(m, 0, 0, k, j, i),
                                      host_adm.g_dd(m, 0, 1, k, j, i),
                                      host_adm.g_dd(m, 0, 2, k, j, i),
                                      host_adm.g_dd(m, 1, 1, k, j, i),
                                      host_adm.g_dd(m, 1, 2, k, j, i),
                                      host_adm.g_dd(m, 2, 2, k, j, i));
          host_adm.psi4(m, k, j, i) = std::pow(detg, 1. / 3.);
        }
  }
  Kokkos::deep_copy(u_adm, host_u_adm);
}

void RefinementCondition(MeshBlockPack *pmbp) {
  pmbp->pz4c->pz4c_amr->Refine(pmbp);
}