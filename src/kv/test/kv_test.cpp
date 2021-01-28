// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ds/logger.h"
#include "enclave/app_interface.h"
#include "kv/kv_serialiser.h"
#include "kv/store.h"
#include "kv/test/null_encryptor.h"
#include "node/entities.h"
#include "node/history.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <msgpack/msgpack.hpp>
#include <set>
#include <string>
#include <vector>

struct MapTypes
{
  using StringString = kv::Map<std::string, std::string>;
  using NumNum = kv::Map<size_t, size_t>;
  using NumString = kv::Map<size_t, std::string>;
  using StringNum = kv::Map<std::string, size_t>;
};

TEST_CASE("Map name parsing")
{
  using SD = kv::SecurityDomain;
  using AC = kv::AccessCategory;

  auto parse = kv::parse_map_name;
  auto mp = std::make_pair<SD, AC>;

  REQUIRE(parse("foo") == mp(SD::PRIVATE, AC::APPLICATION));
  REQUIRE(parse("public:foo") == mp(SD::PUBLIC, AC::APPLICATION));
  REQUIRE(parse("ccf.gov.foo") == mp(SD::PRIVATE, AC::GOVERNANCE));
  REQUIRE(parse("public:ccf.gov.foo") == mp(SD::PUBLIC, AC::GOVERNANCE));
  REQUIRE(parse("ccf.internal.foo") == mp(SD::PRIVATE, AC::INTERNAL));
  REQUIRE(parse("public:ccf.internal.foo") == mp(SD::PUBLIC, AC::INTERNAL));

  REQUIRE_THROWS(parse("ccf.foo"));
  REQUIRE_THROWS(parse("public:ccf.foo"));

  // Typos may lead to unexpected behaviour!
  REQUIRE(parse("publik:ccf.gov.foo") == mp(SD::PRIVATE, AC::APPLICATION));
  REQUIRE(parse("PUBLIC:ccf.gov.foo") == mp(SD::PRIVATE, AC::APPLICATION));
  REQUIRE(parse("public:Ccf.gov.foo") == mp(SD::PUBLIC, AC::APPLICATION));

  REQUIRE(parse("ccf_foo") == mp(SD::PRIVATE, AC::APPLICATION));
  REQUIRE(parse("public:ccf_foo") == mp(SD::PUBLIC, AC::APPLICATION));
}

TEST_CASE("Reads/writes and deletions")
{
  kv::Store kv_store;

  MapTypes::StringString map("public:map");

  constexpr auto k = "key";
  constexpr auto invalid_key = "invalid_key";
  constexpr auto v1 = "value1";

  INFO("Start empty transaction");
  {
    auto tx = kv_store.create_tx();
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    REQUIRE_THROWS_AS(tx.commit(), std::logic_error);
  }

  INFO("Read own writes");
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    REQUIRE(!view->has(k));
    auto v = view->get(k);
    REQUIRE(!v.has_value());
    view->put(k, v1);
    REQUIRE(view->has(k));
    auto va = view->get(k);
    REQUIRE(va.has_value());
    REQUIRE(va.value() == v1);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  INFO("Read previous writes");
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    REQUIRE(view->has(k));
    auto v = view->get(k);
    REQUIRE(v.has_value());
    REQUIRE(v.value() == v1);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  INFO("Remove keys");
  {
    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->put(k, v1);

      REQUIRE(!view->has(invalid_key));
      REQUIRE(!view->remove(invalid_key));
      REQUIRE(view->remove(k));
      REQUIRE(!view->has(k));
      auto va = view->get(k);
      REQUIRE(!va.has_value());

      view->put(k, v1);
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx2 = kv_store.create_tx();
      auto view2 = tx2.get_view(map);
      REQUIRE(view2->has(k));
      REQUIRE(view2->remove(k));
    }
  }

  INFO("Remove key that was deleted from state");
  {
    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->put(k, v1);
      auto va = view->get_globally_committed(k);
      REQUIRE(!va.has_value());
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx2 = kv_store.create_tx();
      auto view2 = tx2.get_view(map);
      REQUIRE(view2->has(k));
      REQUIRE(view2->remove(k));
      REQUIRE(!view2->has(k));
      REQUIRE(tx2.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx3 = kv_store.create_tx();
      auto view3 = tx3.get_view(map);
      REQUIRE(!view3->has(k));
      auto vc = view3->get(k);
      REQUIRE(!vc.has_value());
    }
  }
}

TEST_CASE("foreach")
{
  kv::Store kv_store;
  MapTypes::StringString map("public:map");

  std::map<std::string, std::string> iterated_entries;

  auto store_iterated =
    [&iterated_entries](const auto& key, const auto& value) {
      auto it = iterated_entries.find(key);
      REQUIRE(it == iterated_entries.end());
      iterated_entries[key] = value;
      return true;
    };

  SUBCASE("Empty map")
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->foreach(store_iterated);
    REQUIRE(iterated_entries.empty());
  }

  SUBCASE("Reading own writes")
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("key1", "value1");
    view->put("key2", "value2");
    view->foreach(store_iterated);
    REQUIRE(iterated_entries.size() == 2);
    REQUIRE(iterated_entries["key1"] == "value1");
    REQUIRE(iterated_entries["key2"] == "value2");

    iterated_entries.clear();

    INFO("Uncommitted writes from other txs are not visible");
    auto tx2 = kv_store.create_tx();
    auto view2 = tx2.get_view(map);
    view2->foreach(store_iterated);
    REQUIRE(iterated_entries.empty());
  }

  SUBCASE("Reading committed writes")
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("key1", "value1");
    view->put("key2", "value2");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    auto tx2 = kv_store.create_tx();
    auto view2 = tx2.get_view(map);
    view2->foreach(store_iterated);
    REQUIRE(iterated_entries.size() == 2);
    REQUIRE(iterated_entries["key1"] == "value1");
    REQUIRE(iterated_entries["key2"] == "value2");
  }

  SUBCASE("Mix of committed and own writes")
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("key1", "value1");
    view->put("key2", "value2");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    auto tx2 = kv_store.create_tx();
    auto view2 = tx2.get_view(map);
    view2->put("key2", "replaced2");
    view2->put("key3", "value3");
    view2->foreach(store_iterated);
    REQUIRE(iterated_entries.size() == 3);
    REQUIRE(iterated_entries["key1"] == "value1");
    REQUIRE(iterated_entries["key2"] == "replaced2");
    REQUIRE(iterated_entries["key3"] == "value3");
  }

  SUBCASE("Deletions")
  {
    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->put("key1", "value1");
      view->put("key2", "value2");
      view->put("key3", "value3");
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->remove("key1");
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->foreach(store_iterated);
      REQUIRE(iterated_entries.size() == 2);
      REQUIRE(iterated_entries["key2"] == "value2");
      REQUIRE(iterated_entries["key3"] == "value3");

      iterated_entries.clear();

      view->remove("key2");
      view->foreach(store_iterated);
      REQUIRE(iterated_entries.size() == 1);
      REQUIRE(iterated_entries["key3"] == "value3");

      iterated_entries.clear();

      view->put("key1", "value1");
      view->put("key2", "value2");
      view->foreach(store_iterated);
      REQUIRE(iterated_entries.size() == 3);
      REQUIRE(iterated_entries["key1"] == "value1");
      REQUIRE(iterated_entries["key2"] == "value2");
      REQUIRE(iterated_entries["key3"] == "value3");
    }
  }

  SUBCASE("Early termination")
  {
    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->put("key1", "value1");
      view->put("key2", "value2");
      view->put("key3", "value3");
      size_t ctr = 0;
      view->foreach([&ctr](const auto& key, const auto& value) {
        ++ctr;
        return ctr < 2; // Continue after the first, but not the second (so
                        // never see the third)
      });
      REQUIRE(ctr == 2);
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    {
      auto tx = kv_store.create_tx();
      auto view = tx.get_view(map);
      view->put("key4", "value4");
      view->put("key5", "value5");

      {
        size_t ctr = 0;
        view->foreach([&ctr](const auto&, const auto&) {
          ++ctr;
          return ctr < 2; //< See only committed state
        });
        REQUIRE(ctr == 2);
      }

      {
        size_t ctr = 0;
        view->foreach([&ctr](const auto&, const auto&) {
          ++ctr;
          return ctr < 4; //< See mix of old state and new writes
        });
        REQUIRE(ctr == 4);
      }

      {
        size_t ctr = 0;
        view->foreach([&ctr](const auto&, const auto&) {
          ++ctr;
          return ctr < 100; //< See as much as possible
        });
        REQUIRE(ctr == 5);
      }
    }
  }
}

TEST_CASE("Modifications during foreach iteration")
{
  kv::Store kv_store;
  MapTypes::NumString map("public:map");

  const auto value1 = "foo";
  const auto value2 = "bar";

  std::set<size_t> keys;
  {
    INFO("Insert initial keys");

    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    for (size_t i = 0; i < 60; ++i)
    {
      keys.insert(i);
      view->put(i, value1);
    }

    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  auto tx = kv_store.create_tx();
  auto view = tx.get_view(map);

  // 5 types of key:
  // 1) previously committed and unmodified
  const auto initial_keys_size = keys.size();
  const auto keys_per_category = keys.size() / 3;
  // We do nothing to the first keys_per_category keys

  // 2) previously committed and had their values changed
  for (size_t i = keys_per_category; i < 2 * keys_per_category; ++i)
  {
    keys.insert(i);
    view->put(i, value2);
  }

  // 3) previously committed and now removed
  for (size_t i = 2 * keys_per_category; i < initial_keys_size; ++i)
  {
    keys.erase(i);
    view->remove(i);
  }

  // 4) newly written
  for (size_t i = initial_keys_size; i < initial_keys_size + keys_per_category;
       ++i)
  {
    keys.insert(i);
    view->put(i, value2);
  }

  // 5) newly written and then removed
  for (size_t i = initial_keys_size + keys_per_category;
       i < initial_keys_size + 2 * keys_per_category;
       ++i)
  {
    keys.insert(i);
    view->put(i, value2);

    keys.erase(i);
    view->remove(i);
  }

  size_t keys_seen = 0;
  const auto expected_keys_seen = keys.size();

  SUBCASE("Removing current key while iterating")
  {
    auto should_remove = [](size_t n) { return n % 3 == 0 || n % 5 == 0; };

    view->foreach(
      [&view, &keys, &keys_seen, should_remove](const auto& k, const auto&) {
        ++keys_seen;
        const auto it = keys.find(k);
        REQUIRE(it != keys.end());

        // Remove a 'random' set of keys while iterating
        if (should_remove(k))
        {
          view->remove(k);
          keys.erase(it);
        }

        return true;
      });

    REQUIRE(keys_seen == expected_keys_seen);

    // Check all expected keys are still there...
    view->foreach([&keys, should_remove](const auto& k, const auto&) {
      REQUIRE(!should_remove(k));
      const auto it = keys.find(k);
      REQUIRE(it != keys.end());
      keys.erase(it);
      return true;
    });

    // ...and nothing else
    REQUIRE(keys.empty());
  }

  SUBCASE("Removing other keys while iterating")
  {
    auto should_remove = [](size_t n) { return n % 3 == 0 || n % 5 == 0; };

    std::optional<size_t> removal_trigger = std::nullopt;

    view->foreach([&view, &keys, &keys_seen, &removal_trigger, should_remove](
                    const auto& k, const auto&) {
      ++keys_seen;

      // The first time we find a removable, remove _all the others_ (not
      // ourself!)
      if (should_remove(k) && !removal_trigger.has_value())
      {
        REQUIRE(!removal_trigger.has_value());
        removal_trigger = k;

        auto remove_it = keys.begin();
        while (remove_it != keys.end())
        {
          const auto n = *remove_it;
          if (should_remove(n) && n != k)
          {
            view->remove(n);
            remove_it = keys.erase(remove_it);
          }
          else
          {
            ++remove_it;
          }
        }
      }

      return true;
    });

    REQUIRE(keys_seen == expected_keys_seen);

    REQUIRE(removal_trigger.has_value());

    // Check all expected keys are still there...
    view->foreach(
      [&keys, removal_trigger, should_remove](const auto& k, const auto&) {
        const auto should_be_here =
          !should_remove(k) || k == removal_trigger.value();
        REQUIRE(should_be_here);
        const auto it = keys.find(k);
        REQUIRE(it != keys.end());
        keys.erase(it);
        return true;
      });

    // ...and nothing else
    REQUIRE(keys.empty());
  }

  static constexpr auto value3 = "baz";

  SUBCASE("Modifying and adding other keys while iterating")
  {
    auto should_modify = [](size_t n) { return n % 3 == 0 || n % 5 == 0; };

    std::set<size_t> updated_keys;

    view->foreach([&view, &keys, &keys_seen, &updated_keys, should_modify](
                    const auto& k, const auto& v) {
      ++keys_seen;

      if (should_modify(k))
      {
        // Modify ourselves
        view->put(k, value3);
        updated_keys.insert(k);

        // Modify someone else ('before' and 'after' are guesses - iteration
        // order is undefined!)
        const auto before = k / 2;
        view->put(before, value3);
        keys.insert(before);
        updated_keys.insert(before);

        const auto after = k * 2;
        view->put(after, value3);
        keys.insert(after);
        updated_keys.insert(after);

        // Note discrepancy with externally visible value
        const auto visible_v = view->get(k);
        REQUIRE(visible_v.has_value());
        REQUIRE(visible_v.value() == value3);
        REQUIRE(visible_v.value() != v); // !!
      }

      return true;
    });

    REQUIRE(keys_seen == expected_keys_seen);

    // Check all expected keys are still there...
    view->foreach([&keys, &updated_keys](const auto& k, const auto& v) {
      const auto updated_it = updated_keys.find(k);
      if (updated_it != updated_keys.end())
      {
        REQUIRE(v == value3);
        updated_keys.erase(updated_it);
      }
      else
      {
        REQUIRE(v != value3);
      }

      const auto it = keys.find(k);
      if (it != keys.end())
      {
        keys.erase(it);
      }

      return true;
    });

    // ...and nothing else
    REQUIRE(keys.empty());
    REQUIRE(updated_keys.empty());
  }

  SUBCASE("Rewriting to new keys")
  {
    // Rewrite map, placing each value at a new key
    view->foreach([&view, &keys_seen](const auto& k, const auto& v) {
      ++keys_seen;

      view->remove(k);

      const auto new_key = k + 1000;
      REQUIRE(!view->has(new_key));
      view->put(new_key, v);

      return true;
    });

    REQUIRE(keys_seen == expected_keys_seen);

    // Check map contains only new keys, and the same count
    keys_seen = 0;
    view->foreach([&view, &keys, &keys_seen](const auto& k, const auto& v) {
      ++keys_seen;

      REQUIRE(keys.find(k) == keys.end());

      return true;
    });

    REQUIRE(keys_seen == expected_keys_seen);
  }
}

TEST_CASE("Read-only tx")
{
  kv::Store kv_store;
  MapTypes::StringString map("public:map");

  constexpr auto k = "key";
  constexpr auto invalid_key = "invalid_key";
  constexpr auto v1 = "value1";

  INFO("Write some keys");
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    auto v = view->get(k);
    REQUIRE(!v.has_value());
    view->put(k, v1);
    auto va = view->get(k);
    REQUIRE(va.has_value());
    REQUIRE(va.value() == v1);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  INFO("Do only reads with an overpowered Tx");
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_read_only_view(map);
    REQUIRE(view->has(k));
    const auto v = view->get(k);
    REQUIRE(v.has_value());
    REQUIRE(v.value() == v1);

    REQUIRE(!view->has(invalid_key));
    const auto invalid_v = view->get(invalid_key);
    REQUIRE(!invalid_v.has_value());

    // The following won't compile:
    // view->put(k, v1);
    // view->remove(k);
  }

  INFO("Read with read-only tx");
  {
    auto tx = kv_store.create_read_only_tx();
    auto view = tx.get_read_only_view(map);
    REQUIRE(view->has(k));
    const auto v = view->get(k);
    REQUIRE(v.has_value());
    REQUIRE(v.value() == v1);

    REQUIRE(!view->has(invalid_key));
    const auto invalid_v = view->get(invalid_key);
    REQUIRE(!invalid_v.has_value());

    // The following won't compile:
    // view->put(k, v1);
    // view->remove(k);
  }
}

TEST_CASE("Rollback and compact")
{
  kv::Store kv_store;
  MapTypes::StringString map("public:map");

  constexpr auto k = "key";
  constexpr auto v1 = "value1";

  INFO("Do not read transactions that have been rolled back");
  {
    auto tx = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put(k, v1);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    kv_store.rollback(0);
    auto view2 = tx2.get_view(map);
    auto v = view2->get(k);
    REQUIRE(!v.has_value());
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);
  }

  INFO("Read committed key");
  {
    auto tx = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put(k, v1);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    kv_store.compact(kv_store.current_version());

    auto view2 = tx2.get_view(map);
    auto va = view2->get_globally_committed(k);
    REQUIRE(va.has_value());
    REQUIRE(va.value() == v1);
  }

  INFO("Read deleted committed key");
  {
    auto tx = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto view = tx.get_view(map);
    REQUIRE(view->remove(k));
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    kv_store.compact(kv_store.current_version());

    auto view2 = tx2.get_view(map);
    auto va = view2->get_globally_committed(k);
    REQUIRE(!va.has_value());
  }
}

TEST_CASE("Local commit hooks")
{
  using Write = MapTypes::StringString::Write;
  std::vector<Write> local_writes;
  std::vector<Write> global_writes;

  auto local_hook = [&](kv::Version v, const Write& w) {
    local_writes.push_back(w);
  };
  auto global_hook = [&](kv::Version v, const Write& w) {
    global_writes.push_back(w);
  };

  kv::Store kv_store;
  constexpr auto map_name = "public:map";
  MapTypes::StringString map(map_name);
  kv_store.set_local_hook(map_name, map.wrap_commit_hook(local_hook));
  kv_store.set_global_hook(map_name, map.wrap_commit_hook(global_hook));

  INFO("Write with hooks");
  {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("key1", "value1");
    view->put("key2", "value2");
    view->remove("key2");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    REQUIRE(global_writes.size() == 0);
    REQUIRE(local_writes.size() == 1);
    const auto& latest_writes = local_writes.front();
    REQUIRE(latest_writes.at("key1").has_value());
    REQUIRE(latest_writes.at("key1").value() == "value1");
    INFO("Local removals are not seen");
    REQUIRE(latest_writes.find("key2") == latest_writes.end());
    REQUIRE(latest_writes.size() == 1);

    local_writes.clear();
  }

  INFO("Write without hooks");
  {
    kv_store.unset_local_hook(map_name);
    kv_store.unset_global_hook(map_name);

    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("key2", "value2");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    REQUIRE(local_writes.size() == 0);
    REQUIRE(global_writes.size() == 0);
  }

  INFO("Write with hook again");
  {
    kv_store.set_local_hook(map_name, map.wrap_commit_hook(local_hook));
    kv_store.set_global_hook(map_name, map.wrap_commit_hook(global_hook));

    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->remove("key2");
    view->put("key3", "value3");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);

    REQUIRE(global_writes.size() == 0);
    REQUIRE(local_writes.size() == 1);
    const auto& latest_writes = local_writes.front();
    INFO("Old writes are not included");
    REQUIRE(latest_writes.find("key1") == latest_writes.end());
    INFO("Visible removals are included");
    const auto it2 = latest_writes.find("key2");
    REQUIRE(it2 != latest_writes.end());
    REQUIRE(!it2->second.has_value());
    const auto it3 = latest_writes.find("key3");
    REQUIRE(it3 != latest_writes.end());
    REQUIRE(it3->second.has_value());
    REQUIRE(it3->second.value() == "value3");
    REQUIRE(latest_writes.size() == 2);

    local_writes.clear();
  }
}

TEST_CASE("Global commit hooks")
{
  using Write = MapTypes::StringString::Write;

  struct GlobalHookInput
  {
    kv::Version version;
    Write writes;
  };

  std::vector<GlobalHookInput> global_writes;

  auto global_hook = [&](kv::Version v, const Write& w) {
    global_writes.emplace_back(GlobalHookInput({v, w}));
  };

  kv::Store kv_store;
  using MapT = kv::Map<std::string, std::string>;
  MapT map_with_hook("public:map_with_hook");
  kv_store.set_global_hook(
    map_with_hook.get_name(), map_with_hook.wrap_commit_hook(global_hook));

  MapT map_no_hook("public:map_no_hook");

  INFO("Compact an empty store");
  {
    kv_store.compact(0);

    REQUIRE(global_writes.size() == 0);
  }

  SUBCASE("Compact one transaction")
  {
    auto tx1 = kv_store.create_tx();
    auto view_hook = tx1.get_view(map_with_hook);
    view_hook->put("key1", "value1");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);

    kv_store.compact(1);

    REQUIRE(global_writes.size() == 1);
    const auto& latest_writes = global_writes.front();
    REQUIRE(latest_writes.version == 1);
    const auto it1 = latest_writes.writes.find("key1");
    REQUIRE(it1 != latest_writes.writes.end());
    REQUIRE(it1->second.has_value());
    REQUIRE(it1->second.value() == "value1");
  }

  SUBCASE("Compact beyond the last map version")
  {
    auto tx1 = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto tx3 = kv_store.create_tx();
    auto view_hook = tx1.get_view(map_with_hook);
    view_hook->put("key1", "value1");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);

    view_hook = tx2.get_view(map_with_hook);
    view_hook->put("key2", "value2");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);

    const auto compact_version = kv_store.current_version();

    // This does not affect map_with_hook but still increments the current
    // version of the store
    auto view_no_hook = tx3.get_view(map_no_hook);
    view_no_hook->put("key3", "value3");
    REQUIRE(tx3.commit() == kv::CommitSuccess::OK);

    kv_store.compact(compact_version);

    // Only the changes made to map_with_hook should be passed to the global
    // hook
    REQUIRE(global_writes.size() == 2);
    REQUIRE(global_writes.at(0).version == 1);
    const auto it1 = global_writes.at(0).writes.find("key1");
    REQUIRE(it1 != global_writes.at(0).writes.end());
    REQUIRE(it1->second.has_value());
    REQUIRE(it1->second.value() == "value1");
    const auto it2 = global_writes.at(1).writes.find("key2");
    REQUIRE(it2 != global_writes.at(1).writes.end());
    REQUIRE(it2->second.has_value());
    REQUIRE(it2->second.value() == "value2");
  }

  SUBCASE("Compact in between two map versions")
  {
    auto tx1 = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto tx3 = kv_store.create_tx();
    auto view_hook = tx1.get_view(map_with_hook);
    view_hook->put("key1", "value1");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);

    // This does not affect map_with_hook but still increments the current
    // version of the store
    auto view_no_hook = tx2.get_view(map_no_hook);
    view_no_hook->put("key2", "value2");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);

    const auto compact_version = kv_store.current_version();

    view_hook = tx3.get_view(map_with_hook);
    view_hook->put("key3", "value3");
    REQUIRE(tx3.commit() == kv::CommitSuccess::OK);

    kv_store.compact(compact_version);

    // Only the changes made to map_with_hook should be passed to the global
    // hook
    REQUIRE(global_writes.size() == 1);
    REQUIRE(global_writes.at(0).version == 1);
    const auto it1 = global_writes.at(0).writes.find("key1");
    REQUIRE(it1 != global_writes.at(0).writes.end());
    REQUIRE(it1->second.has_value());
    REQUIRE(it1->second.value() == "value1");
  }

  SUBCASE("Compact twice")
  {
    auto tx1 = kv_store.create_tx();
    auto tx2 = kv_store.create_tx();
    auto view_hook = tx1.get_view(map_with_hook);
    view_hook->put("key1", "value1");
    REQUIRE(tx1.commit() == kv::CommitSuccess::OK);

    kv_store.compact(kv_store.current_version());
    global_writes.clear();

    view_hook = tx2.get_view(map_with_hook);
    view_hook->put("key2", "value2");
    REQUIRE(tx2.commit() == kv::CommitSuccess::OK);

    kv_store.compact(kv_store.current_version());

    // Only writes since the last compact are passed to the global hook
    REQUIRE(global_writes.size() == 1);
    REQUIRE(global_writes.at(0).version == 2);
    const auto it2 = global_writes.at(0).writes.find("key2");
    REQUIRE(it2 != global_writes.at(0).writes.end());
    REQUIRE(it2->second.has_value());
    REQUIRE(it2->second.value() == "value2");
  }
}

TEST_CASE("Deserialising from other Store")
{
  auto encryptor = std::make_shared<kv::NullTxEncryptor>();
  kv::Store store;
  store.set_encryptor(encryptor);

  MapTypes::NumString public_map("public:public");
  MapTypes::NumString private_map("private");
  auto tx1 = store.create_reserved_tx(store.next_version());
  auto [view1, view2] = tx1.get_view(public_map, private_map);
  view1->put(42, "aardvark");
  view2->put(14, "alligator");
  auto [success, reqid, data] = tx1.commit_reserved();
  REQUIRE(success == kv::CommitSuccess::OK);

  kv::Store clone;
  clone.set_encryptor(encryptor);

  REQUIRE(clone.deserialise(data) == kv::DeserialiseSuccess::PASS);
}

TEST_CASE("Deserialise return status")
{
  kv::Store store;

  ccf::Signatures signatures(ccf::Tables::SIGNATURES);
  ccf::Nodes nodes(ccf::Tables::NODES);
  MapTypes::NumNum data("public:data");

  auto kp = tls::make_key_pair();

  auto history = std::make_shared<ccf::NullTxHistory>(store, 0, *kp);
  store.set_history(history);

  {
    auto tx = store.create_reserved_tx(store.next_version());
    auto data_view = tx.get_view(data);
    data_view->put(42, 42);
    auto [success, reqid, data] = tx.commit_reserved();
    REQUIRE(success == kv::CommitSuccess::OK);

    REQUIRE(store.deserialise(data) == kv::DeserialiseSuccess::PASS);
  }

  {
    auto tx = store.create_reserved_tx(store.next_version());
    auto sig_view = tx.get_view(signatures);
    ccf::PrimarySignature sigv(0, 2);
    sig_view->put(0, sigv);
    auto [success, reqid, data] = tx.commit_reserved();
    REQUIRE(success == kv::CommitSuccess::OK);

    REQUIRE(store.deserialise(data) == kv::DeserialiseSuccess::PASS_SIGNATURE);
  }

  INFO("Signature transactions with additional contents should fail");
  {
    auto tx = store.create_reserved_tx(store.next_version());
    auto [sig_view, data_view] = tx.get_view(signatures, data);
    ccf::PrimarySignature sigv(0, 2);
    sig_view->put(0, sigv);
    data_view->put(43, 43);
    auto [success, reqid, data] = tx.commit_reserved();
    REQUIRE(success == kv::CommitSuccess::OK);

    REQUIRE(store.deserialise(data) == kv::DeserialiseSuccess::FAILED);
  }
}

TEST_CASE("Map swap between stores")
{
  auto encryptor = std::make_shared<kv::NullTxEncryptor>();
  kv::Store s1;
  s1.set_encryptor(encryptor);

  kv::Store s2;
  s2.set_encryptor(encryptor);

  MapTypes::NumNum d("data");
  MapTypes::NumNum pd("public:data");

  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(d);
    v->put(42, 42);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pd);
    v->put(14, 14);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  const auto target_version = s1.current_version();
  while (s2.current_version() < target_version)
  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(d);
    v->put(41, 41);
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  s2.swap_private_maps(s1);

  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(d);
    auto val = v->get(41);
    REQUIRE_FALSE(v->get(42).has_value());
    REQUIRE(val.has_value());
    REQUIRE(val.value() == 41);
  }

  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pd);
    auto val = v->get(14);
    REQUIRE(val.has_value());
    REQUIRE(val.value() == 14);
  }

  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(d);
    auto val = v->get(42);
    REQUIRE_FALSE(v->get(41).has_value());
    REQUIRE(val.has_value());
    REQUIRE(val.value() == 42);
  }

  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(pd);
    REQUIRE_FALSE(v->get(14).has_value());
  }
}

TEST_CASE("Private recovery map swap")
{
  auto encryptor = std::make_shared<kv::NullTxEncryptor>();
  kv::Store s1;
  s1.set_encryptor(encryptor);
  MapTypes::NumNum priv1("private");
  MapTypes::NumString pub1("public:data");

  kv::Store s2;
  s2.set_encryptor(encryptor);
  MapTypes::NumNum priv2("private");
  MapTypes::NumString pub2("public:data");

  INFO("Populate s1 with public entries");
  // We compact twice, deliberately. A public KV during recovery
  // would have compacted some number of times.
  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pub1);
    v->put(42, "42");
    tx.commit();
  }
  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pub1);
    v->put(42, "43");
    tx.commit();
  }
  s1.compact(s1.current_version());
  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pub1);
    v->put(44, "44");
    tx.commit();
  }
  s1.compact(s1.current_version());
  {
    auto tx = s1.create_tx();
    auto v = tx.get_view(pub1);
    v->put(45, "45");
    tx.commit();
  }

  INFO("Populate s2 with private entries");
  // We compact only once, at a lower index than we did for the public
  // KV, which is what we expect during recovery of the private KV. We do expect
  // that the _entire_ private state is compacted
  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(priv2);
    v->put(12, 12);
    tx.commit();
  }
  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(priv2);
    v->put(13, 13);
    tx.commit();
  }
  s2.compact(s2.current_version());
  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(priv2);
    v->put(14, 14);
    tx.commit();
  }
  {
    auto tx = s2.create_tx();
    auto v = tx.get_view(priv2);
    v->put(15, 15);
    tx.commit();
  }

  INFO("Swap in private maps");
  REQUIRE(s1.current_version() == s2.current_version());
  REQUIRE_NOTHROW(s1.swap_private_maps(s2));

  INFO("Check state looks as expected in s1");
  {
    auto tx = s1.create_tx();
    auto [priv, pub] = tx.get_view(priv1, pub1);
    {
      auto val = pub->get(42);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == "43");

      val = pub->get(44);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == "44");

      val = pub->get(45);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == "45");

      REQUIRE(s1.commit_version() == 3);
    }
    {
      for (size_t i : {12, 13, 14, 15})
      {
        auto val = priv->get(i);
        REQUIRE(val.has_value());
        REQUIRE(val.value() == i);
      }
    }
  }

  INFO("Check committed state looks as expected in s1");
  {
    auto tx = s1.create_tx();
    auto [priv, pub] = tx.get_view(priv1, pub1);
    {
      auto val = pub->get_globally_committed(42);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == "43");

      val = pub->get_globally_committed(44);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == "44");

      val = pub->get_globally_committed(45);
      REQUIRE_FALSE(val.has_value());
    }
    {
      auto val = priv->get_globally_committed(12);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == 12);

      val = priv->get_globally_committed(13);
      REQUIRE(val.has_value());
      REQUIRE(val.value() == 13);

      // Uncompacted state is visible, which is expected, but isn't
      // something that would happen in recovery (only compacted state
      // would be swapped in). There is deliberately no check for compacted
      // state later than the compact level on the public KV, as this is
      // impossible during recovery.
    }
  }
}

TEST_CASE("Conflict resolution")
{
  kv::Store kv_store;
  MapTypes::StringString map("public:map");

  {
    // Ensure this map already exists, by making a prior write to it
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);
    view->put("foo", "initial");
    REQUIRE(tx.commit() == kv::CommitSuccess::OK);
  }

  auto try_write = [&](kv::Tx& tx, const std::string& s) {
    auto view = tx.get_view(map);

    // Introduce read-dependency
    view->get("foo");
    view->put("foo", s);

    view->put(s, s);
  };

  auto confirm_state = [&](
                         const std::vector<std::string>& present,
                         const std::vector<std::string>& missing) {
    auto tx = kv_store.create_tx();
    auto view = tx.get_view(map);

    for (const auto& s : present)
    {
      const auto it = view->get(s);
      REQUIRE(it.has_value());
      REQUIRE(view->has(s));
      REQUIRE(it.value() == s);
    }

    for (const auto& s : missing)
    {
      const auto it = view->get(s);
      REQUIRE(!it.has_value());
      REQUIRE(!view->has(s));
    }
  };

  // Simulate parallel execution by interleaving tx steps
  auto tx1 = kv_store.create_tx();
  auto tx2 = kv_store.create_tx();

  // First transaction tries to write a value, depending on initial version
  try_write(tx1, "bar");

  {
    // A second transaction is committed, conflicting with the first
    try_write(tx2, "baz");
    const auto res2 = tx2.commit();
    REQUIRE(res2 == kv::CommitSuccess::OK);

    confirm_state({"baz"}, {"bar"});
  }

  // Trying to commit first transaction produces a conflict
  auto res1 = tx1.commit();
  REQUIRE(res1 == kv::CommitSuccess::CONFLICT);
  confirm_state({"baz"}, {"bar"});

  // A third transaction just wants to read the value
  auto tx3 = kv_store.create_tx();
  auto view3 = tx3.get_view(map);
  REQUIRE(view3->has("foo"));

  // First transaction is rerun with same object, producing different result
  try_write(tx1, "buzz");

  // Expected results are committed
  res1 = tx1.commit();
  REQUIRE(res1 == kv::CommitSuccess::OK);
  confirm_state({"baz", "buzz"}, {"bar"});

  // Third transaction completes later, has no conflicts but reports the earlier
  // version it read
  auto res3 = tx3.commit();
  REQUIRE(res3 == kv::CommitSuccess::OK);

  REQUIRE(tx1.commit_version() > tx2.commit_version());
  REQUIRE(tx2.get_read_version() >= tx2.get_read_version());

  // Re-running a _committed_ transaction is exceptionally bad
  REQUIRE_THROWS(tx1.commit());
  REQUIRE_THROWS(tx2.commit());
}

TEST_CASE("Mid-tx compaction")
{
  kv::Store kv_store;
  MapTypes::StringNum map_a("public:A");
  MapTypes::StringNum map_b("public:B");

  constexpr auto key_a = "a";
  constexpr auto key_b = "b";

  auto increment_vals = [&]() {
    auto tx = kv_store.create_tx();
    auto [view_a, view_b] = tx.get_view(map_a, map_b);

    auto a_opt = view_a->get(key_a);
    auto b_opt = view_b->get(key_b);

    REQUIRE(a_opt == b_opt);

    const auto new_val = a_opt.has_value() ? *a_opt + 1 : 0;

    view_a->put(key_a, new_val);
    view_b->put(key_b, new_val);

    const auto result = tx.commit();
    REQUIRE(result == kv::CommitSuccess::OK);
  };

  increment_vals();

  {
    INFO("Compaction before get_views");
    auto tx = kv_store.create_tx();

    increment_vals();
    kv_store.compact(kv_store.current_version());

    auto view_a = tx.get_view(map_a);
    auto view_b = tx.get_view(map_b);

    auto a_opt = view_a->get(key_a);
    auto b_opt = view_b->get(key_b);

    REQUIRE(a_opt == b_opt);

    const auto result = tx.commit();
    REQUIRE(result == kv::CommitSuccess::OK);
  }

  {
    INFO("Compaction after get_views");
    auto tx = kv_store.create_tx();

    auto view_a = tx.get_view(map_a);
    increment_vals();
    auto view_b = tx.get_view(map_b);
    kv_store.compact(kv_store.current_version());

    auto a_opt = view_a->get(key_a);
    auto b_opt = view_b->get(key_b);

    REQUIRE(a_opt == b_opt);

    const auto result = tx.commit();
    REQUIRE(result == kv::CommitSuccess::OK);
  }

  {
    INFO("Compaction between get_views");
    bool threw = false;

    try
    {
      auto tx = kv_store.create_tx();

      auto view_a = tx.get_view(map_a);
      // This transaction does something slow. Meanwhile...

      // ...another transaction commits...
      increment_vals();
      // ...and is compacted...
      kv_store.compact(kv_store.current_version());

      // ...then the original transaction proceeds, expecting to read a single
      // version
      // This should throw a CompactedVersionConflict error
      auto view_b = tx.get_view(map_b);

      auto a_opt = view_a->get(key_a);
      auto b_opt = view_b->get(key_b);

      REQUIRE(a_opt == b_opt);

      const auto result = tx.commit();
      REQUIRE(result == kv::CommitSuccess::OK);
    }
    catch (const kv::CompactedVersionConflict& e)
    {
      threw = true;
    }

    REQUIRE(threw);
    // In real operation, this transaction would be re-executed and hope to not
    // intersect a compaction
  }
}

TEST_CASE("Store clear")
{
  kv::Store kv_store;
  kv_store.set_term(42);

  auto map_a_name = "public:A";
  auto map_b_name = "public:B";
  MapTypes::StringNum map_a(map_a_name);
  MapTypes::StringNum map_b(map_b_name);

  INFO("Apply transactions and compact store");
  {
    size_t tx_count = 10;
    for (int i = 0; i < tx_count; i++)
    {
      auto tx = kv_store.create_tx();
      auto [view_a, view_b] = tx.get_view(map_a, map_b);

      view_a->put("key" + std::to_string(i), 42);
      view_b->put("key" + std::to_string(i), 42);
      REQUIRE(tx.commit() == kv::CommitSuccess::OK);
    }

    auto current_version = kv_store.current_version();
    kv_store.compact(current_version);

    REQUIRE(kv_store.get_map(current_version, map_a_name) != nullptr);
    REQUIRE(kv_store.get_map(current_version, map_b_name) != nullptr);

    REQUIRE(kv_store.current_version() != 0);
    REQUIRE(kv_store.commit_version() != 0);
    auto tx_id = kv_store.current_txid();
    REQUIRE(tx_id.term != 0);
    REQUIRE(tx_id.version != 0);
  }

  INFO("Verify that store state is cleared");
  {
    kv_store.clear();
    auto current_version = kv_store.current_version();

    REQUIRE(kv_store.get_map(current_version, map_a_name) == nullptr);
    REQUIRE(kv_store.get_map(current_version, map_b_name) == nullptr);

    REQUIRE(kv_store.current_version() == 0);
    REQUIRE(kv_store.commit_version() == 0);
    auto tx_id = kv_store.current_txid();
    REQUIRE(tx_id.term == 0);
    REQUIRE(tx_id.version == 0);
  }
}