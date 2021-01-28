// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "crypto/hash.h"
#include "kv/map.h"
#include "node_signature.h"

#include <msgpack/msgpack.hpp>
#include <string>
#include <vector>

namespace ccf
{
  struct BackupSignatures
  {
    kv::Consensus::View view = 0;
    kv::Consensus::SeqNo seqno = 0;
    crypto::Sha256Hash root;
    std::vector<NodeSignature> signatures;

    MSGPACK_DEFINE(view, seqno, root, signatures);

    BackupSignatures() = default;

    BackupSignatures(
      kv::Consensus::View view_,
      kv::Consensus::SeqNo seqno_,
      const crypto::Sha256Hash root_) :
      view(view_),
      seqno(seqno_),
      root(root_)
    {}
  };
  DECLARE_JSON_TYPE(BackupSignatures);
  DECLARE_JSON_REQUIRED_FIELDS(BackupSignatures, view, seqno, root, signatures)
  using BackupSignaturesMap = kv::Map<ObjectId, BackupSignatures>;
}