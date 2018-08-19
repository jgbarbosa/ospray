//
// Created by jbarbosa on 7/31/18.
//

#ifndef OSPRAY_MPOFFLOADWORKER_H
#define OSPRAY_MPOFFLOADWORKER_H

namespace ospray {
    namespace mpi {
        std::unique_ptr<work::Work> readWork(work::WorkTypeRegistry &registry,
                                             networking::ReadStream &readStream);
        void runWorker(work::WorkTypeRegistry &registry);
    }
}  // namespace ospray

#endif //OSPRAY_MPOFFLOADWORKER_H
