// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "kv/kv_types.h"
#include "raft.h"
#include "request.h"

#include <memory>

namespace aft
{
  // This class acts as an adapter between the generic Consensus API and
  // the AFT API, allowing for a mapping between the generic consensus
  // terminology and the terminology that is specific to AFT

  template <class... T>
  class Consensus : public kv::Consensus
  {
  private:
    std::unique_ptr<Aft<T...>> aft;
    ConsensusType consensus_type;

  public:
    Consensus(std::unique_ptr<Aft<T...>> raft_, ConsensusType consensus_type_) :
      kv::Consensus(raft_->id()),
      aft(std::move(raft_)),
      consensus_type(consensus_type_)
    {}

    bool is_primary() override
    {
      return aft->is_primary();
    }

    bool is_backup() override
    {
      return aft->is_follower();
    }

    void force_become_primary() override
    {
      aft->force_become_leader();
    }

    void force_become_primary(
      SeqNo seqno,
      View view,
      const std::vector<kv::Version>& terms,
      SeqNo commit_seqno) override
    {
      aft->force_become_leader(seqno, view, terms, commit_seqno);
    }

    void init_as_backup(
      SeqNo seqno,
      View view,
      const std::vector<kv::Version>& view_history) override
    {
      aft->init_as_follower(seqno, view, view_history);
    }

    bool replicate(const kv::BatchVector& entries, View view) override
    {
      return aft->replicate(entries, view);
    }

    std::pair<View, SeqNo> get_committed_txid() override
    {
      return aft->get_commit_term_and_idx();
    }

    std::optional<std::pair<View, SeqNo>> get_signable_txid() override
    {
      return aft->get_signable_commit_term_and_idx();
    }

    View get_view(SeqNo seqno) override
    {
      return aft->get_term(seqno);
    }

    View get_view() override
    {
      return aft->get_term();
    }

    std::vector<SeqNo> get_view_history(SeqNo seqno) override
    {
      return aft->get_term_history(seqno);
    }

    void initialise_view_history(
      const std::vector<SeqNo>& view_history) override
    {
      aft->initialise_term_history(view_history);
    }

    SeqNo get_committed_seqno() override
    {
      return aft->get_commit_idx();
    }

    NodeId primary() override
    {
      return aft->leader();
    }

    std::set<NodeId> active_nodes() override
    {
      return aft->active_nodes();
    }

    void recv_message(OArray&& data) override
    {
      return aft->recv_message(std::move(data));
    }

    void add_configuration(
      SeqNo seqno, const Configuration::Nodes& conf) override
    {
      aft->add_configuration(seqno, conf);
    }

    Configuration::Nodes get_latest_configuration() const override
    {
      return aft->get_latest_configuration();
    }

    void periodic(std::chrono::milliseconds elapsed) override
    {
      aft->periodic(elapsed);
    }

    void enable_all_domains() override
    {
      aft->enable_all_domains();
    }

    uint32_t node_count() override
    {
      return aft->node_count();
    }

    void emit_signature() override {}

    bool on_request(const kv::TxHistory::RequestCallbackArgs& args) override
    {
      return aft->on_request(args);
    }

    ConsensusType type() override
    {
      return consensus_type;
    }
  };
}