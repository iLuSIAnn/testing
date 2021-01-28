// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "consensus/aft/raft_types.h"
#include "node/view_change.h"

#include <chrono>
#include <map>
#include <set>

namespace aft
{
  class ViewChangeTracker
  {
    struct ViewChange
    {
      ViewChange(kv::Consensus::View view_, kv::Consensus::SeqNo seqno_) :
        view(view_),
        seqno(seqno_),
        new_view_sent(false)
      {}

      kv::Consensus::View view;
      kv::Consensus::SeqNo seqno;
      bool new_view_sent;

      std::map<kv::NodeId, ccf::ViewChangeRequest> received_view_changes;
    };

  public:
    ViewChangeTracker(
      std::shared_ptr<ccf::ProgressTrackerStore> store_,
      std::chrono::milliseconds time_between_attempts_) :
      store(store_),
      last_view_change_sent(0),
      time_between_attempts(time_between_attempts_)
    {}

    bool should_send_view_change(std::chrono::milliseconds time)
    {
      if (time > time_between_attempts + time_previous_view_change_increment)
      {
        ++last_view_change_sent;
        time_previous_view_change_increment = time;
        return true;
      }
      return false;
    }

    bool is_view_change_in_progress(std::chrono::milliseconds time)
    {
      return time <=
        (time_between_attempts + time_previous_view_change_increment);
    }

    kv::Consensus::View get_target_view() const
    {
      return last_view_change_sent;
    }

    void set_current_view_change(kv::Consensus::View view)
    {
      view_changes.clear();
      last_view_change_sent = view;
    }

    enum class ResultAddView
    {
      OK,
      APPEND_NEW_VIEW_MESSAGE
    };

    ResultAddView add_request_view_change(
      ccf::ViewChangeRequest& v,
      kv::NodeId from,
      kv::Consensus::View view,
      kv::Consensus::SeqNo seqno,
      uint32_t node_count)
    {
      auto it = view_changes.find(view);
      if (it == view_changes.end())
      {
        ViewChange view_change(view, seqno);
        std::tie(it, std::ignore) =
          view_changes.emplace(view, std::move(view_change));
      }
      it->second.received_view_changes.emplace(from, v);

      if (
        should_send_new_view(
          it->second.received_view_changes.size(), node_count) &&
        it->second.new_view_sent == false)
      {
        it->second.new_view_sent = true;
        last_valid_view = view;
        return ResultAddView::APPEND_NEW_VIEW_MESSAGE;
      }

      return ResultAddView::OK;
    }

    kv::Consensus::SeqNo write_view_change_confirmation_append_entry(
      kv::Consensus::View view)
    {
      ccf::ViewChangeConfirmation nv =
        create_view_change_confirmation_msg(view);
      return store->write_view_change_confirmation(nv);
    }

    std::vector<uint8_t> get_serialized_view_change_confirmation(
      kv::Consensus::View view)
    {
      ccf::ViewChangeConfirmation nv =
        create_view_change_confirmation_msg(view);
      nlohmann::json j;
      to_json(j, nv);
      std::string s = j.dump();
      return {s.begin(), s.end() + 1};
    }

    bool add_unknown_primary_evidence(
      CBuffer data, kv::Consensus::View view, uint32_t node_count)
    {
      nlohmann::json j = nlohmann::json::parse(data.p);
      auto vc = j.get<ccf::ViewChangeConfirmation>();

      if (view != vc.view)
      {
        return false;
      }

      if (
        vc.view_change_messages.size() < ccf::get_message_threshold(node_count))
      {
        return false;
      }

      for (auto it : vc.view_change_messages)
      {
        if (!store->verify_view_change_request(
              it.second, it.first, vc.view, vc.seqno))
        {
          return false;
        }
      }

      last_valid_view = view;
      return true;
    }

    bool check_evidence(kv::Consensus::View view) const
    {
      return last_valid_view == view;
    }

    void clear(bool is_primary, kv::Consensus::View view)
    {
      for (auto it = view_changes.begin(); it != view_changes.end();)
      {
        if (is_primary && it->first != view)
        {
          it = view_changes.erase(it);
        }
        else
        {
          ++it;
        }
      }
      view_changes.clear();
      last_valid_view = view;
    }

  private:
    std::shared_ptr<ccf::ProgressTrackerStore> store;
    std::map<kv::Consensus::View, ViewChange> view_changes;
    std::chrono::milliseconds time_previous_view_change_increment =
      std::chrono::milliseconds(0);
    kv::Consensus::View last_view_change_sent = 0;
    kv::Consensus::View last_valid_view = aft::starting_view_change;
    const std::chrono::milliseconds time_between_attempts;

    ccf::ViewChangeConfirmation create_view_change_confirmation_msg(
      kv::Consensus::View view)
    {
      auto it = view_changes.find(view);
      if (it == view_changes.end())
      {
        throw std::logic_error(fmt::format(
          "Cannot write unknown view-change to ledger, view:{}", view));
      }

      auto& vc = it->second;
      ccf::ViewChangeConfirmation nv(vc.view, vc.seqno);

      for (auto it : vc.received_view_changes)
      {
        nv.view_change_messages.emplace(it.first, it.second);
      }

      return nv;
    }

    bool should_send_new_view(size_t received_requests, size_t node_count) const
    {
      return received_requests == ccf::get_message_threshold(node_count);
    }
  };
}