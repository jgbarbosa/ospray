// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //
// Misc helper functions //////////////////////////////////////////////////
#pragma once

#include "mpiCommon/MPIBcastFabric.h"
#include "mpiCommon/MPICommon.h"

namespace ospray {
  namespace mpi {

  void runWorker(work::WorkTypeRegistry&);

    static inline void throwIfNotMpiParallel()
    {
      if (mpicommon::world.size <= 1) {
        throw std::runtime_error(
            "No MPI workers found.\n#osp:mpi: Fatal Error "
            "- OSPRay told to run in MPI mode, but there "
            "seems to be no MPI peers!?\n#osp:mpi: (Did "
            "you forget an 'mpirun' in front of your "
            "application?)");
      }
    }

    static inline void setupMaster()
    {
      MPI_CALL(Comm_split(mpicommon::world.comm,
                          1,
                          mpicommon::world.rank,
                          &mpicommon::app.comm));

      mpicommon::app.makeIntraComm();

      postStatusMsg(OSPRAY_MPI_VERBOSE_LEVEL)
          << "#w: app process " << mpicommon::app.rank << '/'
          << mpicommon::app.size << " (global " << mpicommon::world.rank << '/'
          << mpicommon::world.size;

      MPI_CALL(Intercomm_create(mpicommon::app.comm,
                                0,
                                mpicommon::world.comm,
                                1,
                                1,
                                &mpicommon::worker.comm));

      postStatusMsg(OSPRAY_MPI_VERBOSE_LEVEL)
          << "master: Made 'worker' intercomm (through intercomm_create): "
          << std::hex << std::showbase << mpicommon::worker.comm
          << std::noshowbase << std::dec;

      mpicommon::worker.makeInterComm();

      // -------------------------------------------------------
      // at this point, all processes should be set up and synced. in
      // particular:
      // - app has intracommunicator to all workers (and vica versa)
      // - app process(es) are in one intercomm ("app"); workers all in
      //   another ("worker")
      // - all processes (incl app) have barrier'ed, and thus now in sync.
    }

    static inline void setupWorker()
    {
      MPI_CALL(Comm_split(mpicommon::world.comm,
                          0,
                          mpicommon::world.rank,
                          &mpicommon::worker.comm));

      mpicommon::worker.makeIntraComm();

      postStatusMsg(OSPRAY_MPI_VERBOSE_LEVEL)
          << "master: Made 'worker' intercomm (through split): " << std::hex
          << std::showbase << mpicommon::worker.comm << std::noshowbase << std::dec;

      postStatusMsg(OSPRAY_MPI_VERBOSE_LEVEL)
          << "#w: app process " << mpicommon::app.rank << '/'
          << mpicommon::app.size << " (global " << mpicommon::world.rank << '/'
          << mpicommon::world.size;

      MPI_CALL(Intercomm_create(mpicommon::worker.comm,
                                0,
                                mpicommon::world.comm,
                                0,
                                1,
                                &mpicommon::app.comm));

      mpicommon::app.makeInterComm();

      // -------------------------------------------------------
      // at this point, all processes should be set up and synced. in
      // particular:
      // - app has intracommunicator to all workers (and vica versa)
      // - app process(es) are in one intercomm ("app"); workers all in
      //   another ("worker")
      // - all processes (incl app) have barrier'ed, and thus now in sync.
    }
  }  // namespace mpi
}  // namespace ospray
