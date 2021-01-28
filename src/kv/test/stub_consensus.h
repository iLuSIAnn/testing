// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "consensus/aft/impl/state.h"
#include "crypto/symmetric_key.h"
#include "kv/kv_types.h"

#include <algorithm>
#include <iostream>

namespace kv
{
  class StubConsensus : public Consensus
  {
  private:
    std::vector<kv::BatchVector::value_type> replica;
    ConsensusType consensus_type;

  public:
    aft::ViewHistory view_history;

    StubConsensus(ConsensusType consensus_type_ = ConsensusType::CFT) :
      Consensus(0),
      replica(),
      consensus_type(consensus_type_)
    {}

    bool replicate(const BatchVector& entries, View view) override
    {
      for (const auto& entry : entries)
      {
        replica.push_back(entry);

        // Simplification: all entries are replicated in the same term
        view_history.update(std::get<0>(entry), 2);
      }
      return true;
    }

    std::optional<std::vector<uint8_t>> get_latest_data()
    {
      if (!replica.empty())
      {
        return *std::get<1>(replica.back());
      }
      else
      {
        return std::nullopt;
      }
    }

    std::optional<std::vector<uint8_t>> pop_oldest_data()
    {
      if (!replica.empty())
      {
        auto data = *std::get<1>(replica.front());
        replica.erase(replica.begin());
        return data;
      }
      else
      {
        return std::nullopt;
      }
    }

    std::optional<kv::BatchVector::value_type> pop_oldest_entry()
    {
      if (!replica.empty())
      {
        auto entry = replica.front();
        replica.erase(replica.begin());
        return entry;
      }
      else
      {
        return std::nullopt;
      }
    }

    size_t number_of_replicas()
    {
      return replica.size();
    }

    void flush()
    {
      replica.clear();
    }

    std::pair<View, SeqNo> get_committed_txid() override
    {
      return {2, 0};
    }

    std::optional<std::pair<View, SeqNo>> get_signable_txid() override
    {
      return get_committed_txid();
    }

    SeqNo get_committed_seqno() override
    {
      return 0;
    }

    NodeId primary() override
    {
      return 1;
    }

    std::set<NodeId> active_nodes() override
    {
      return {};
    }

    NodeId id() override
    {
      return 0;
    }

    View get_view(SeqNo seqno) override
    {
      return 2;
    }

    View get_view() override
    {
      return 2;
    }

    std::vector<SeqNo> get_view_history(SeqNo seqno) override
    {
      return view_history.get_history_until(seqno);
    }

    void initialise_view_history(
      const std::vector<SeqNo>& view_history_) override
    {
      view_history.initialise(view_history_);
    }

    void recv_message(OArray&& oa) override {}

    void add_configuration(
      SeqNo seqno, const Configuration::Nodes& conf) override
    {}

    Configuration::Nodes get_latest_configuration() const override
    {
      return {};
    }

    uint32_t node_count() override
    {
      return 0;
    }

    void emit_signature() override
    {
      return;
    }

    ConsensusType type() override
    {
      return consensus_type;
    }
  };

  class BackupStubConsensus : public StubConsensus
  {
  public:
    BackupStubConsensus(ConsensusType consensus_type = ConsensusType::CFT) :
      StubConsensus(consensus_type)
    {}

    bool is_primary() override
    {
      return false;
    }

    bool replicate(const BatchVector& entries, View view) override
    {
      return false;
    }
  };

  class PrimaryStubConsensus : public StubConsensus
  {
  public:
    PrimaryStubConsensus(ConsensusType consensus_type = ConsensusType::CFT) :
      StubConsensus(consensus_type)
    {}

    bool is_primary() override
    {
      return true;
    }
  };
}
