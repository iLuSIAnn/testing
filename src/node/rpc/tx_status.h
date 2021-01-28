// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/json.h"

namespace ccf
{
  enum class TxStatus
  {
    Unknown,
    Pending,
    Committed,
    Invalid,
  };

  constexpr char const* tx_status_to_str(TxStatus status)
  {
    switch (status)
    {
      case TxStatus::Unknown:
      {
        return "UNKNOWN";
      }
      case TxStatus::Pending:
      {
        return "PENDING";
      }
      case TxStatus::Committed:
      {
        return "COMMITTED";
      }
      case TxStatus::Invalid:
      {
        return "INVALID";
      }
      default:
      {
        return "Unhandled value";
      }
    }
  }

  DECLARE_JSON_ENUM(
    TxStatus,
    {{TxStatus::Unknown, tx_status_to_str(TxStatus::Unknown)},
     {TxStatus::Pending, tx_status_to_str(TxStatus::Pending)},
     {TxStatus::Committed, tx_status_to_str(TxStatus::Committed)},
     {TxStatus::Invalid, tx_status_to_str(TxStatus::Invalid)}});

  constexpr int64_t VIEW_UNKNOWN = std::numeric_limits<int64_t>::min();

  static TxStatus get_tx_status(
    int64_t target_view,
    int64_t target_seqno,
    int64_t local_view,
    int64_t committed_view,
    int64_t committed_seqno)
  {
    const bool is_committed = committed_seqno >= target_seqno;
    const bool views_match = local_view == target_view;
    const bool view_known = local_view != VIEW_UNKNOWN;

    if (is_committed && !view_known)
    {
      throw std::logic_error(fmt::format(
        "Should know local view for seqnos up to {}, but have no view for {}",
        committed_seqno,
        target_seqno));
    }

    if (is_committed)
    {
      // The requested seqno has been committed, so we know for certain whether
      // the requested tx id is committed or not
      if (views_match)
      {
        return TxStatus::Committed;
      }
      else
      {
        return TxStatus::Invalid;
      }
    }
    else if (views_match)
    {
      // This node knows about the requested tx id, but it is not globally
      // committed
      return TxStatus::Pending;
    }
    else if (committed_view > target_view)
    {
      // This node has seen the seqno in a different view, and committed
      // further, so the requested tx id is impossible
      return TxStatus::Invalid;
    }
    else
    {
      // Otherwise, we cannot state anything about this tx id. The most common
      // reason is that the local_view is unknown (this transaction has never
      // existed, or has not reached this node yet). It is also possible that
      // this node believes locally that this tx id is impossible, but does not
      // have a global commit to back this up - it will eventually receive
      // either a global commit confirming this belief, or an election and
      // global commit making this tx id invalid
      return TxStatus::Unknown;
    }
  }
}