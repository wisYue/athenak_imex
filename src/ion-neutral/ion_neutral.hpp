#ifndef ION_NEUTRAL_ION_NEUTRAL_HPP_
#define ION_NEUTRAL_ION_NEUTRAL_HPP_
//========================================================================================
// AthenaXXX astrophysical plasma code
// Copyright(C) 2020 James M. Stone <jmstone@ias.edu> and the Athena code team
// Licensed under the 3-clause BSD License (the "LICENSE")
//========================================================================================
//! \file ion-neutral.hpp
//  \brief definitions for IonNeutral class

#include "athena.hpp"
#include "parameter_input.hpp"
#include "tasklist/task_list.hpp"
#include "driver/driver.hpp"

//----------------------------------------------------------------------------------------
//! \struct IonNeutralTaskIDs
//  \brief container to hold TaskIDs of all ion-neutral tasks
  
struct IonNeutralTaskIDs
{   
  TaskID i_init_recv;
  TaskID n_init_recv;
  TaskID first2_imp_update;
  TaskID i_calc_flux;
  TaskID n_calc_flux;
  TaskID i_exp_update;
  TaskID n_exp_update;
  TaskID imp_update;
  TaskID i_sendu;
  TaskID n_sendu;
  TaskID i_recvu;
  TaskID n_recvu;
  TaskID corner_e;
  TaskID ct;
  TaskID sendb;
  TaskID recvb;
  TaskID i_phys_bcs;
  TaskID n_phys_bcs;
  TaskID i_cons2prim;
  TaskID n_cons2prim;
  TaskID i_newdt;
  TaskID n_newdt;
  TaskID i_clear_send;
  TaskID n_clear_send;
};

//----------------------------------------------------------------------------------------
//! \class IonNeutral

class IonNeutral
{
 public:
  IonNeutral(MeshBlockPack *ppack, ParameterInput *pin, Driver *pdrive);
  ~IonNeutral();

  Real drag_coeff;       // ion-neutral coupling coefficient
  DvceArray6D<Real> ru;  // drag term in each dirn evaluated at different time levels

  // container to hold names of TaskIDs
  IonNeutralTaskIDs id;

  // functions
  void AssembleIonNeutralTasks(TaskList &start, TaskList &run, TaskList &end);
  TaskStatus FirstTwoImpRK(Driver* pdrive, int stage);
  TaskStatus ImpRKUpdate(Driver* pdrive, int stage);

 private:
  MeshBlockPack* pmy_pack;  // ptr to MeshBlockPack containing this Hydro
};

#endif // ION_NEUTRAL_ION_NEUTRAL_HPP_