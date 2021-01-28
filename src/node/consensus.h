// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "entities.h"
#include "kv/map.h"

#include <msgpack/msgpack.hpp>

namespace ccf
{
  using ConsensusTable = kv::Map<ObjectId, ConsensusType>;
}

DECLARE_JSON_ENUM(
  ConsensusType, {{ConsensusType::CFT, "CFT"}, {ConsensusType::BFT, "BFT"}})

MSGPACK_ADD_ENUM(ConsensusType);
