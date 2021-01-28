// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "kv/kv_serialiser.h"
#include "kv/store.h"
#include "kv/test/null_encryptor.h"
#include "kv/tx.h"

#include <doctest/doctest.h>

struct MapTypes
{
  using StringString = kv::Map<std::string, std::string>;
  using NumNum = kv::Map<size_t, size_t>;
};

TEST_CASE("Simple snapshot" * doctest::test_suite("snapshot"))
{
  kv::Store store;
  MapTypes::StringString string_map("public:string_map");
  MapTypes::NumNum num_map("public:num_map");

  kv::Version first_snapshot_version = kv::NoVersion;
  kv::Version second_snapshot_version = kv::NoVersion;

  INFO("Apply transactions to original store");
  {
    auto tx1 = store.create_tx();
    auto view_1 = tx1.get_view<MapTypes::StringString>("public:string_map");
    view_1->put("foo", "bar");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);
    first_snapshot_version = tx1.commit_version();

    auto tx2 = store.create_tx();
    auto view_2 = tx2.get_view(num_map);
    view_2->put(42, 123);
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);
    second_snapshot_version = tx2.commit_version();

    auto tx3 = store.create_tx();
    auto view_3 = tx1.get_view<MapTypes::StringString>("public:string_map");
    view_3->put("uncommitted", "not committed");
    // Do not commit tx3
  }

  auto first_snapshot = store.snapshot(first_snapshot_version);
  auto first_serialised_snapshot =
    store.serialise_snapshot(std::move(first_snapshot));

  INFO("Apply snapshot at 1 to new store");
  {
    kv::Store new_store;

    REQUIRE_EQ(
      new_store.deserialise_snapshot(first_serialised_snapshot),
      kv::DeserialiseSuccess::PASS);
    REQUIRE_EQ(new_store.current_version(), 1);

    auto tx1 = new_store.create_tx();
    auto view = tx1.get_view<MapTypes::StringString>("public:string_map");
    auto v = view->get("foo");
    REQUIRE(v.has_value());
    REQUIRE_EQ(v.value(), "bar");

    auto num_view = tx1.get_view<MapTypes::NumNum>("public:num_map");
    REQUIRE(!num_view->has(42));

    REQUIRE(!view->has("uncommitted"));
  }

  auto second_snapshot = store.snapshot(second_snapshot_version);
  auto second_serialised_snapshot =
    store.serialise_snapshot(std::move(second_snapshot));

  INFO("Apply snapshot at 2 to new store");
  {
    kv::Store new_store;

    new_store.deserialise_snapshot(second_serialised_snapshot);
    REQUIRE_EQ(new_store.current_version(), 2);

    auto tx1 = new_store.create_tx();
    auto view = tx1.get_view<MapTypes::StringString>("public:string_map");

    auto v = view->get("foo");
    REQUIRE(v.has_value());
    REQUIRE_EQ(v.value(), "bar");

    auto num_view = tx1.get_view<MapTypes::NumNum>("public:num_map");
    auto num_v = num_view->get(42);
    REQUIRE(num_v.has_value());
    REQUIRE_EQ(num_v.value(), 123);

    REQUIRE(!view->has("uncommitted"));
  }
}

TEST_CASE(
  "Commit transaction while applying snapshot" *
  doctest::test_suite("snapshot"))
{
  kv::Store store;
  MapTypes::StringString string_map("public:string_map");

  kv::Version snapshot_version = kv::NoVersion;
  INFO("Apply transactions to original store");
  {
    auto tx1 = store.create_tx();
    auto view_1 = tx1.get_view<MapTypes::StringString>("public:string_map");
    view_1->put("foo", "foo");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK); // Committed at 1

    auto tx2 = store.create_tx();
    auto view_2 = tx2.get_view<MapTypes::StringString>("public:string_map");
    view_2->put("bar", "bar");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK); // Committed at 2
    snapshot_version = tx2.commit_version();
  }

  auto snapshot = store.snapshot(snapshot_version);
  auto serialised_snapshot = store.serialise_snapshot(std::move(snapshot));

  INFO("Apply snapshot while committing a transaction");
  {
    kv::Store new_store;

    auto tx = new_store.create_tx();
    auto view = tx.get_view<MapTypes::StringString>("public:string_map");
    view->put("in", "flight");
    // tx is not committed until the snapshot is deserialised

    new_store.deserialise_snapshot(serialised_snapshot);

    // Transaction conflicts as snapshot was applied while transaction was in
    // flight
    REQUIRE(tx.commit() == kv::CommitSuccess::CONFLICT);

    // Try again
    auto tx2 = new_store.create_tx();
    auto view2 = tx2.get_view<MapTypes::StringString>("public:string_map");
    view2->put("baz", "baz");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);
  }
}

TEST_CASE("Commit hooks with snapshot" * doctest::test_suite("snapshot"))
{
  kv::Store store;
  constexpr auto string_map = "public:string_map";

  kv::Version snapshot_version = kv::NoVersion;
  INFO("Apply transactions to original store");
  {
    auto tx1 = store.create_tx();
    auto view_1 = tx1.get_view<MapTypes::StringString>(string_map);
    view_1->put("foo", "foo");
    view_1->put("bar", "bar");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK); // Committed at 1

    // New transaction, deleting content from the previous transaction
    auto tx2 = store.create_tx();
    auto view_2 = tx2.get_view<MapTypes::StringString>(string_map);
    view_2->put("baz", "baz");
    view_2->remove("bar");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK); // Committed at 2
    snapshot_version = tx2.commit_version();
  }

  auto snapshot = store.snapshot(snapshot_version);
  auto serialised_snapshot = store.serialise_snapshot(std::move(snapshot));

  INFO("Apply snapshot with local hook on target store");
  {
    kv::Store new_store;

    MapTypes::StringString new_string_map(string_map);

    using Write = MapTypes::StringString::Write;
    std::vector<Write> local_writes;
    std::vector<Write> global_writes;

    INFO("Set hooks on target store");
    {
      auto local_hook = [&](kv::Version v, const Write& w) {
        local_writes.push_back(w);
      };
      auto global_hook = [&](kv::Version v, const Write& w) {
        global_writes.push_back(w);
      };

      new_store.set_local_hook(
        string_map, new_string_map.wrap_commit_hook(local_hook));
      new_store.set_global_hook(
        string_map, new_string_map.wrap_commit_hook(global_hook));
    }

    new_store.deserialise_snapshot(serialised_snapshot);

    INFO("Verify content of snapshot");
    {
      auto tx = new_store.create_tx();
      auto view = tx.get_view<MapTypes::StringString>(string_map);
      REQUIRE(view->get("foo").has_value());
      REQUIRE(!view->get("bar").has_value());
      REQUIRE(view->get("baz").has_value());
    }

    INFO("Verify local hook execution");
    {
      REQUIRE_EQ(local_writes.size(), 1);
      auto writes = local_writes.at(0);
      REQUIRE_EQ(writes.at("foo"), "foo");
      REQUIRE_EQ(writes.find("bar"), writes.end());
      REQUIRE_EQ(writes.at("baz"), "baz");
    }

    INFO("Verify global hook execution after compact");
    {
      new_store.compact(snapshot_version);

      REQUIRE_EQ(global_writes.size(), 1);
      auto writes = global_writes.at(0);
      REQUIRE_EQ(writes.at("foo"), "foo");
      REQUIRE_EQ(writes.find("bar"), writes.end());
      REQUIRE_EQ(writes.at("baz"), "baz");
    }
  }
}