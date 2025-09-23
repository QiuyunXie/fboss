// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once
#include "fboss/fsdb/client/FsdbSubManager.h"
#include "fboss/fsdb/oper/instantiations/FsdbAdaptedCowStorage.h"

namespace facebook::fboss {

/*
Creating an instantiation of FsdbSubManager that returns fully adapted
SwitchState types. To do this, first we have to adapt all the way to fsdb root
*/
extern template class fsdb::FsdbSubManager<
    fsdb::CowStorage<fsdb::FsdbOperStateRoot, FsdbOperStateRoot>,
    true /* IsCow */>;

using FsdbAdaptedSubManager = fsdb::FsdbSubManager<
    fsdb::CowStorage<fsdb::FsdbOperStateRoot, FsdbOperStateRoot>,
    true /* IsCow */>;

} // namespace facebook::fboss
