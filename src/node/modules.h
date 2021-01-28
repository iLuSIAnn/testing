// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <msgpack/msgpack.hpp>
#include <optional>
#include <stdint.h>
#include <string>
#include <vector>

namespace ccf
{
  struct Module
  {
    std::string js;

    Module() = default;

    Module(const std::string& js_) : js(js_) {}

    MSGPACK_DEFINE(js);
  };
  DECLARE_JSON_TYPE(Module)
  DECLARE_JSON_REQUIRED_FIELDS(Module, js)

  using Modules = kv::Map<std::string, Module>;
}