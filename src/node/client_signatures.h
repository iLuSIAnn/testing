// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "ds/hash.h"
#include "ds/json.h"
#include "entities.h"
#include "kv/map.h"

#include <mbedtls/md.h>
#include <msgpack/msgpack.hpp>
#include <vector>

MSGPACK_ADD_ENUM(mbedtls_md_type_t);

DECLARE_JSON_ENUM(
  mbedtls_md_type_t,
  {{MBEDTLS_MD_NONE, "MBEDTLS_MD_NONE"},
   {MBEDTLS_MD_SHA1, "MBEDTLS_MD_SHA1"},
   {MBEDTLS_MD_SHA256, "MBEDTLS_MD_SHA256"},
   {MBEDTLS_MD_SHA384, "MBEDTLS_MD_SHA384"},
   {MBEDTLS_MD_SHA512, "MBEDTLS_MD_SHA512"}});

namespace ccf
{
  struct SignedReq
  {
    // signature
    std::vector<uint8_t> sig = {};
    // signed content
    std::vector<uint8_t> req = {};

    // request body
    std::vector<uint8_t> request_body = {};

    // signature hashing algorithm used
    mbedtls_md_type_t md = MBEDTLS_MD_NONE;

    // The key id, if declared in the request
    std::string key_id = {};

    bool operator==(const SignedReq& other) const
    {
      return (sig == other.sig) && (req == other.req) && (md == other.md) &&
        (request_body == other.request_body) && (key_id == other.key_id);
    }

    MSGPACK_DEFINE(sig, req, request_body, md);
  };
  DECLARE_JSON_TYPE(SignedReq)
  DECLARE_JSON_REQUIRED_FIELDS(SignedReq, sig, req, request_body, md, key_id)
  // this maps client-id to latest SignedReq
  using ClientSignatures = kv::Map<CallerId, SignedReq>;
}