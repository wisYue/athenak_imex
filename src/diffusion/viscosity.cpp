//========================================================================================
// AthenaXXX astrophysical plasma code
// Copyright(C) 2020 James M. Stone <jmstone@ias.edu> and the Athena code team
// Licensed under the 3-clause BSD License (the "LICENSE")
//========================================================================================
//! \file viscosity.cpp
//  \brief Implements functions for Viscosity class. This includes isotropic shear
//  viscosity in a Newtonian fluid (in which stress is proportional to shear).
//  Viscosity may be added to Hydro and/or MHD independently.

#include <limits>
#include <iostream>

// Athena++ headers
#include "athena.hpp"
#include "parameter_input.hpp"
#include "mesh/mesh.hpp"
#include "viscosity.hpp"

//----------------------------------------------------------------------------------------
// ctor:
// Note first argument passes string ("hydro" or "mhd") denoting in wihch class this
// object is being constructed, and therefore which <block> in the input file from which
// the parameters are read.

Viscosity::Viscosity(std::string block, MeshBlockPack *pp, ParameterInput *pin)
  : pmy_pack(pp)
{
  // Read coefficient of isotropic kinematic shear viscosity (must be present)
  nu = pin->GetReal(block,"viscosity");

  // viscous timestep on MeshBlock(s) in this pack
  dtnew = std::numeric_limits<float>::max();
  auto size = pmy_pack->pmb->mbsize;
  Real fac;
  if (pp->pmesh->three_d) {
    fac = 1.0/6.0;
  } else if (pp->pmesh->two_d) {
    fac = 0.25;
  } else {
    fac = 0.5;
  }
  for (int m=0; m<(pp->nmb_thispack); ++m) {
    dtnew = std::min(dtnew, fac*SQR(size.dx1.h_view(m))/nu);
    if (pp->pmesh->multi_d) {dtnew = std::min(dtnew, fac*SQR(size.dx2.h_view(m))/nu);}
    if (pp->pmesh->three_d) {dtnew = std::min(dtnew, fac*SQR(size.dx3.h_view(m))/nu);}
  }

}

//----------------------------------------------------------------------------------------
// Viscosity destructor

Viscosity::~Viscosity()
{
}

//----------------------------------------------------------------------------------------
//! \fn void AddIsoViscousFlux
//  \brief Adds viscous fluxes to face-centered fluxes of conserved variables

void Viscosity::IsotropicViscousFlux(
     const DvceArray5D<Real> &w0, DvceFaceFld5D<Real> &flx, const Real nu)
{
  int is = pmy_pack->mb_cells.is; int ie = pmy_pack->mb_cells.ie;
  int js = pmy_pack->mb_cells.js; int je = pmy_pack->mb_cells.je;
  int ks = pmy_pack->mb_cells.ks; int ke = pmy_pack->mb_cells.ke;
  int ncells1 = pmy_pack->mb_cells.nx1 + 2*(pmy_pack->mb_cells.ng);
  int nmb1 = pmy_pack->nmb_thispack - 1;
  auto size = pmy_pack->pmb->mbsize;
  bool &multi_d = pmy_pack->pmesh->multi_d;
  bool &three_d = pmy_pack->pmesh->three_d;

  //--------------------------------------------------------------------------------------
  // fluxes in x1-direction

  int scr_level = 0;
  size_t scr_size = (ScrArray1D<Real>::shmem_size(ncells1)) * 3;
  auto flx1 = flx.x1f;

  par_for_outer("visc1",DevExeSpace(), scr_size, scr_level, 0, nmb1, ks, ke, js, je,
    KOKKOS_LAMBDA(TeamMember_t member, const int m, const int k, const int j)
    {
      ScrArray1D<Real> fvx(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvy(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvz(member.team_scratch(scr_level), ncells1);

      // Add [2(dVx/dx)-(2/3)dVx/dx, dVy/dx, dVz/dx]
      par_for_inner(member, is, ie+1, [&](const int i)
      {
        fvx(i) = 4.0*(w0(m,IVX,k,j,i) - w0(m,IVX,k,j,i-1))/(3.0*size.dx1.d_view(m));
        fvy(i) =     (w0(m,IVY,k,j,i) - w0(m,IVY,k,j,i-1))/size.dx1.d_view(m);
        fvz(i) =     (w0(m,IVZ,k,j,i) - w0(m,IVZ,k,j,i-1))/size.dx1.d_view(m);
      });

      // In 2D/3D Add [(-2/3)dVy/dy, dVx/dy, 0]
      if (multi_d) {
        par_for_inner(member, is, ie+1, [&](const int i)
        {
          fvx(i) -= ((w0(m,IVY,k,j+1,i) + w0(m,IVY,k,j+1,i-1)) -
                     (w0(m,IVY,k,j-1,i) + w0(m,IVY,k,j-1,i-1)))/(6.0*size.dx2.d_view(m));
          fvy(i) += ((w0(m,IVX,k,j+1,i) + w0(m,IVX,k,j+1,i-1)) -
                     (w0(m,IVX,k,j-1,i) + w0(m,IVX,k,j-1,i-1)))/(4.0*size.dx2.d_view(m));
        });
      }

      // In 3D Add [(-2/3)dVz/dz, 0,  dVx/dz]
      if (three_d) {
        par_for_inner(member, is, ie+1, [&](const int i)
        {
          fvx(i) -= ((w0(m,IVZ,k+1,j,i) + w0(m,IVZ,k+1,j,i-1)) -
                     (w0(m,IVZ,k-1,j,i) + w0(m,IVZ,k-1,j,i-1)))/(6.0*size.dx3.d_view(m));
          fvz(i) += ((w0(m,IVX,k+1,j,i) + w0(m,IVX,k+1,j,i-1)) -
                     (w0(m,IVX,k-1,j,i) + w0(m,IVX,k-1,j,i-1)))/(4.0*size.dx3.d_view(m));
        });
     }

      // Sum viscous fluxes into fluxes of conserved variables; including energy fluxes
      par_for_inner(member, is, ie+1, [&](const int i)
      {
        Real nud = 0.5*nu*(w0(m,IDN,k,j,i) + w0(m,IDN,k,j,i-1));
        flx1(m,IVX,k,j,i) -= nud*fvx(i);
        flx1(m,IVY,k,j,i) -= nud*fvy(i);
        flx1(m,IVZ,k,j,i) -= nud*fvz(i);
        if (flx1.extent_int(1) == static_cast<int>(IEN)) {   // proxy for eos.is_adiabatic
          flx1(m,IEN,k,j,i) -= 0.5*nud*((w0(m,IVX,k,j,i-1) + w0(m,IVX,k,j,i))*fvx(i) +
                                        (w0(m,IVY,k,j,i-1) + w0(m,IVY,k,j,i))*fvy(i) +
                                        (w0(m,IVZ,k,j,i-1) + w0(m,IVZ,k,j,i))*fvz(i));
        }
      });
    }
  );
  if (pmy_pack->pmesh->one_d) {return;}

  //--------------------------------------------------------------------------------------
  // fluxes in x2-direction

  auto flx2 = flx.x2f;

  par_for_outer("visc2",DevExeSpace(), scr_size, scr_level, 0, nmb1, ks, ke, js, je+1,
    KOKKOS_LAMBDA(TeamMember_t member, const int m, const int k, const int j)
    {
      ScrArray1D<Real> fvx(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvy(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvz(member.team_scratch(scr_level), ncells1);

      // Add [(dVx/dy+dVy/dx), 2(dVy/dy)-(2/3)(dVx/dx+dVy/dy), dVz/dy]
      par_for_inner(member, is, ie, [&](const int i)
      {
        fvx(i) = (w0(m,IVX,k,j,i  ) - w0(m,IVX,k,j-1,i  ))/size.dx2.d_view(m) +
                ((w0(m,IVY,k,j,i+1) + w0(m,IVY,k,j-1,i+1)) -
                 (w0(m,IVY,k,j,i-1) + w0(m,IVY,k,j-1,i-1)))/(4.0*size.dx1.d_view(m));
        fvy(i) = (w0(m,IVY,k,j,i) - w0(m,IVY,k,j-1,i))*4.0/(3.0*size.dx2.d_view(m)) -
                ((w0(m,IVX,k,j,i+1) + w0(m,IVX,k,j-1,i+1)) -
                 (w0(m,IVX,k,j,i-1) + w0(m,IVX,k,j-1,i-1)))/(6.0*size.dx1.d_view(m));
        fvz(i) = (w0(m,IVZ,k,j,i  ) - w0(m,IVZ,k,j-1,i  ))/size.dx2.d_view(m);
      });

      // In 3D Add [0, (-2/3)dVz/dz, dVy/dz]
      if (three_d) {
        par_for_inner(member, is, ie, [&](const int i)
        {
          fvy(i) -= ((w0(m,IVZ,k+1,j,i) + w0(m,IVZ,k+1,j-1,i)) -
                     (w0(m,IVZ,k-1,j,i) + w0(m,IVZ,k-1,j-1,i)))/(6.0*size.dx3.d_view(m));
          fvz(i) += ((w0(m,IVY,k+1,j,i) + w0(m,IVY,k+1,j-1,i)) -
                     (w0(m,IVY,k-1,j,i) + w0(m,IVY,k-1,j-1,i)))/(4.0*size.dx3.d_view(m));
        });
     }

      // Sum viscous fluxes into fluxes of conserved variables; including energy fluxes
      par_for_inner(member, is, ie, [&](const int i)
      {
        Real nud = 0.5*nu*(w0(m,IDN,k,j,i) + w0(m,IDN,k,j-1,i));
        flx2(m,IVX,k,j,i) -= nud*fvx(i);
        flx2(m,IVY,k,j,i) -= nud*fvy(i);
        flx2(m,IVZ,k,j,i) -= nud*fvz(i);
        if (flx2.extent_int(1) == static_cast<int>(IEN)) {   // proxy for eos.is_adiabatic
          flx2(m,IEN,k,j,i) -= 0.5*nud*((w0(m,IVX,k,j-1,i) + w0(m,IVX,k,j,i))*fvx(i) +
                                        (w0(m,IVY,k,j-1,i) + w0(m,IVY,k,j,i))*fvy(i) +
                                        (w0(m,IVZ,k,j-1,i) + w0(m,IVZ,k,j,i))*fvz(i));
        }
      });
    }
  );
  if (pmy_pack->pmesh->two_d) {return;}

  //--------------------------------------------------------------------------------------
  // fluxes in x3-direction

  auto flx3 = flx.x3f;

  par_for_outer("visc3",DevExeSpace(), scr_size, scr_level, 0, nmb1, ks, ke+1, js, je,
    KOKKOS_LAMBDA(TeamMember_t member, const int m, const int k, const int j)
    {
      ScrArray1D<Real> fvx(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvy(member.team_scratch(scr_level), ncells1);
      ScrArray1D<Real> fvz(member.team_scratch(scr_level), ncells1);

      // Add [(dVx/dz+dVz/dx), (dVy/dz+dVz/dy), 2(dVz/dz)-(2/3)(dVx/dx+dVy/dy+dVz/dz)]
      par_for_inner(member, is, ie, [&](const int i)
      {
        fvx(i) = (w0(m,IVX,k,j,i  ) - w0(m,IVX,k-1,j,i  ))/size.dx3.d_view(m) +
                ((w0(m,IVZ,k,j,i+1) + w0(m,IVZ,k-1,j,i+1)) -
                 (w0(m,IVZ,k,j,i-1) + w0(m,IVZ,k-1,j,i-1)))/(4.0*size.dx1.d_view(m));
        fvy(i) = (w0(m,IVY,k,j,i  ) - w0(m,IVY,k-1,j,i  ))/size.dx3.d_view(m) +
                ((w0(m,IVZ,k,j+1,i) + w0(m,IVZ,k-1,j+1,i)) -
                 (w0(m,IVZ,k,j-1,i) + w0(m,IVZ,k-1,j-1,i)))/(4.0*size.dx2.d_view(m));
        fvz(i) = (w0(m,IVZ,k,j,i) - w0(m,IVZ,k-1,j,i))*4.0/(3.0*size.dx3.d_view(m)) -
                ((w0(m,IVX,k,j,i+1) + w0(m,IVX,k-1,j,i+1)) -
                 (w0(m,IVX,k,j,i-1) + w0(m,IVX,k-1,j,i-1)))/(6.0*size.dx1.d_view(m)) -
                ((w0(m,IVY,k,j+1,i) + w0(m,IVY,k-1,j+1,i)) -
                 (w0(m,IVY,k,j-1,i) + w0(m,IVY,k-1,j-1,i)))/(6.0*size.dx2.d_view(m));
      });

      // Sum viscous fluxes into fluxes of conserved variables; including energy fluxes
      par_for_inner(member, is, ie, [&](const int i)
      {
        Real nud = 0.5*nu*(w0(m,IDN,k,j,i) + w0(m,IDN,k-1,j,i));
        flx3(m,IVX,k,j,i) -= nud*fvx(i);
        flx3(m,IVY,k,j,i) -= nud*fvy(i);
        flx3(m,IVZ,k,j,i) -= nud*fvz(i);
        if (flx3.extent_int(1) == static_cast<int>(IEN)) {   // proxy for eos.is_adiabatic
          flx3(m,IEN,k,j,i) -= 0.5*nud*((w0(m,IVX,k-1,j,i) + w0(m,IVX,k,j,i))*fvx(i) +
                                        (w0(m,IVY,k-1,j,i) + w0(m,IVY,k,j,i))*fvy(i) +
                                        (w0(m,IVZ,k-1,j,i) + w0(m,IVZ,k,j,i))*fvz(i));
        }
      });
    }
  );

  return;
}
