// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "crypto/hash.h"
#include "ds/json.h"
#include "entities.h"

#include <vector>

namespace ccf
{
  using Nonce = crypto::Sha256Hash;

  struct NodeSignature
  {
    std::vector<uint8_t> sig;
    ccf::NodeId node;
    Nonce hashed_nonce;

    NodeSignature(const NodeSignature& ns) :
      sig(ns.sig),
      node(ns.node),
      hashed_nonce(ns.hashed_nonce)
    {}
    NodeSignature(
      const std::vector<uint8_t>& sig_, NodeId node_, Nonce hashed_nonce_) :
      sig(sig_),
      node(node_),
      hashed_nonce(hashed_nonce_)
    {}
    NodeSignature(ccf::NodeId node_, Nonce hashed_nonce_) :
      node(node_),
      hashed_nonce(hashed_nonce_)
    {}
    NodeSignature(ccf::NodeId node_) : node(node_) {}
    NodeSignature() = default;

    bool operator==(const NodeSignature& o) const
    {
      return sig == o.sig && hashed_nonce == o.hashed_nonce;
    }

    size_t get_serialized_size() const
    {
      return sizeof(node) + sizeof(hashed_nonce) + sizeof(size_t) + sig.size();
    }

    void serialize(uint8_t*& data, size_t& size) const
    {
      size_t sig_size = sig.size();
      serialized::write(
        data, size, reinterpret_cast<uint8_t*>(&sig_size), sizeof(sig_size));
      serialized::write(data, size, sig.data(), sig_size);

      serialized::write(
        data, size, reinterpret_cast<const uint8_t*>(&node), sizeof(node));
      serialized::write(
        data,
        size,
        reinterpret_cast<const uint8_t*>(&hashed_nonce),
        sizeof(hashed_nonce));
    }

    static NodeSignature deserialize(const uint8_t*& data, size_t& size)
    {
      NodeSignature n;

      size_t sig_size = serialized::read<size_t>(data, size);
      n.sig = serialized::read(data, size, sig_size);
      n.node = serialized::read<ccf::NodeId>(data, size);
      n.hashed_nonce = serialized::read<Nonce>(data, size);

      return n;
    }

    MSGPACK_DEFINE(sig, node, hashed_nonce);
  };
  DECLARE_JSON_TYPE(NodeSignature);
  DECLARE_JSON_REQUIRED_FIELDS(NodeSignature, sig, node, hashed_nonce);
}