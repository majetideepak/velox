/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/dwio/common/ScanSpec.h"
#include "velox/dwio/common/SelectiveStructColumnReader.h"
#include "velox/vector/tests/utils/VectorTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace facebook::velox::common {
namespace {

using testing::ElementsAre;
using testing::Pointer;

class ScanSpecTest : public testing::Test, public test::VectorTestBase {
 protected:
  static void SetUpTestCase() {
    memory::MemoryManager::testingSetInstance(memory::MemoryManager::Options{});
  }
};

TEST_F(ScanSpecTest, applyFilter) {
  auto rowVector = makeRowVector({
      makeFlatVector<int64_t>(64, folly::identity),
      makeFlatVector<int64_t>(128, folly::identity),
  });
  ASSERT_EQ(rowVector->size(), 64);
  ScanSpec scanSpec("<root>");
  scanSpec.addAllChildFields(*rowVector->type());
  scanSpec.childByName("c1")->setFilter(createBigintValues({63, 64}, false));
  uint64_t result = -1ll;
  scanSpec.applyFilter(*rowVector, rowVector->size(), &result);
  ASSERT_EQ(result, 1ull << 63);
  result = -1ll;
  scanSpec.childByName("c1")->applyFilter(
      *rowVector->childAt("c1"), rowVector->size(), &result);
  ASSERT_EQ(result, 1ull << 63);
  rowVector = makeRowVector({
      makeFlatVector<int64_t>(128, folly::identity),
      makeFlatVector<int64_t>(64, folly::identity),
  });
  ASSERT_THROW(
      scanSpec.applyFilter(*rowVector, rowVector->size(), &result),
      VeloxRuntimeError);
}

TEST_F(ScanSpecTest, setFilterResetsHasFilter) {
  auto rowVector = makeRowVector({
      makeFlatVector<int64_t>(64, folly::identity),
      makeFlatVector<int64_t>(64, folly::identity),
  });

  ScanSpec scanSpec("<root>");
  scanSpec.addAllChildFields(*rowVector->type());

  // Initially no filter, hasFilter should be false.
  ASSERT_FALSE(scanSpec.hasFilter());
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_FALSE(scanSpec.childByName("c1")->hasFilter());

  // Set a filter on c0, hasFilter should be true for c0 and root.
  scanSpec.childByName("c0")->setFilter(createBigintValues({1, 2, 3}, false));
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_FALSE(scanSpec.hasFilter());
  // Root's hasFilter_ was cached as false, but setFilter should have reset it.
  // After setting filter on child, root should report hasFilter as true.
  scanSpec.resetCachedValues(false);
  ASSERT_TRUE(scanSpec.hasFilter());
  ASSERT_TRUE(scanSpec.childByName("c0")->hasFilter());

  // Set filter to nullptr, hasFilter should become false.
  scanSpec.childByName("c0")->setFilter(nullptr);
  ASSERT_TRUE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_TRUE(scanSpec.hasFilter());
  scanSpec.resetCachedValues(false);
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_FALSE(scanSpec.hasFilter());

  // Set a new filter on c1, verify hasFilter updates correctly.
  scanSpec.childByName("c1")->setFilter(
      std::make_shared<BigintRange>(10, 50, false));
  ASSERT_FALSE(scanSpec.childByName("c1")->hasFilter());
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  scanSpec.resetCachedValues(false);
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_TRUE(scanSpec.childByName("c1")->hasFilter());
  ASSERT_TRUE(scanSpec.hasFilter());

  // Replace filter on c1 with a different filter.
  scanSpec.childByName("c1")->setFilter(
      std::make_shared<BigintRange>(20, 30, false));
  // hasFilter should still be true after replacing with another filter.
  ASSERT_TRUE(scanSpec.childByName("c1")->hasFilter());
  ASSERT_FALSE(scanSpec.childByName("c0")->hasFilter());
  ASSERT_TRUE(scanSpec.hasFilter());
}

// Offers of this size clear the minimum in a single call.
constexpr vector_size_t kMinRows = ScanSpec::kMinLazyRowsOffered;

TEST_F(ScanSpecTest, eagerMaterializeNeedsMinimumRows) {
  ScanSpec scanSpec("<root>");
  auto* child = scanSpec.addField("c0", 0);
  child->setEagerMaterializeCandidate(true, 1.0);

  // Every offered row was loaded, but too few rows to trust the ratio.
  child->recordLazyOffered(kMinRows - 1);
  child->recordLazyLoaded(kMinRows - 1, false);
  scanSpec.newRead();
  EXPECT_FALSE(child->eagerMaterialize());

  child->recordLazyOffered(1);
  child->recordLazyLoaded(1, false);
  scanSpec.newRead();
  EXPECT_TRUE(child->eagerMaterialize());
}

TEST_F(ScanSpecTest, eagerMaterializeLoadRatio) {
  const auto decide =
      [](vector_size_t offered, vector_size_t loaded, double loadRatio) {
        ScanSpec scanSpec("<root>");
        auto* child = scanSpec.addField("c0", 0);
        child->setEagerMaterializeCandidate(true, loadRatio);
        child->recordLazyOffered(offered);
        child->recordLazyLoaded(loaded, false);
        scanSpec.newRead();
        return child->eagerMaterialize();
      };

  EXPECT_TRUE(decide(2000, 1800, 0.9));
  EXPECT_FALSE(decide(2000, 1799, 0.9));

  // A column behind a short circuiting conjunct: the remaining filter rarely
  // evaluates it, so deferring the read is what the LazyVector is for.
  EXPECT_FALSE(decide(100 * kMinRows, kMinRows, 0.9));
  EXPECT_FALSE(decide(100 * kMinRows, 0, 0.9));

  // A ratio of 1.0 demands that every offered row was loaded.
  EXPECT_TRUE(decide(2048, 2048, 1.0));
  EXPECT_FALSE(decide(2048, 2047, 1.0));
}

TEST_F(ScanSpecTest, eagerMaterializeLoadRatioBounds) {
  ScanSpec scanSpec("<root>");
  auto* child = scanSpec.addField("c0", 0);
  EXPECT_THROW(
      child->setEagerMaterializeCandidate(true, 0.0), VeloxRuntimeError);
  EXPECT_THROW(
      child->setEagerMaterializeCandidate(true, 1.5), VeloxRuntimeError);
}

TEST_F(ScanSpecTest, eagerMaterializeOnlyForCandidates) {
  ScanSpec scanSpec("<root>");
  auto* notCandidate = scanSpec.addField("c0", 0);
  auto* hookTarget = scanSpec.addField("c1", 1);
  hookTarget->setEagerMaterializeCandidate(true, 0.9);

  notCandidate->recordLazyOffered(2 * kMinRows);
  notCandidate->recordLazyLoaded(2 * kMinRows, false);
  hookTarget->recordLazyOffered(2 * kMinRows);
  // One load wrote through a ValueHook, so the column is an aggregation
  // pushdown target. Reading it eagerly would defeat the pushdown however well
  // its loads cover the rows they were offered.
  hookTarget->recordLazyLoaded(kMinRows, true);
  hookTarget->recordLazyLoaded(kMinRows, false);

  scanSpec.newRead();
  EXPECT_FALSE(notCandidate->eagerMaterialize());
  EXPECT_FALSE(hookTarget->eagerMaterialize());

  // The veto does not lapse once later loads stop using a hook.
  hookTarget->recordLazyOffered(kMinRows);
  hookTarget->recordLazyLoaded(kMinRows, false);
  scanSpec.newRead();
  EXPECT_FALSE(hookTarget->eagerMaterialize());
}

// An eager column produces no LazyVector, so its counters stop moving and the
// decision holds for the rest of the query.
TEST_F(ScanSpecTest, eagerMaterializeHoldsWithoutNewOffers) {
  ScanSpec scanSpec("<root>");
  auto* child = scanSpec.addField("c0", 0);
  child->setEagerMaterializeCandidate(true, 0.9);
  child->recordLazyOffered(2 * kMinRows);
  child->recordLazyLoaded(2 * kMinRows, false);
  scanSpec.newRead();
  ASSERT_TRUE(child->eagerMaterialize());

  for (int i = 0; i < 10; ++i) {
    scanSpec.newRead();
    EXPECT_TRUE(child->eagerMaterialize());
  }
}

// moveAdaptationFrom is the only channel that carries the decision to the next
// split, both for a preloaded spec and for one rebuilt from scratch. A field
// left behind silently restarts the learning on every split.
TEST_F(ScanSpecTest, moveAdaptationFromCarriesEagerMaterialize) {
  const auto addFields = [](ScanSpec& spec) {
    spec.addField("switched", 0);
    spec.addField("learning", 1);
    spec.addField("hookTarget", 2);
    spec.addField("constant", 3);
  };

  ScanSpec from("<root>");
  addFields(from);
  from.childByName("switched")->setEagerMaterializeCandidate(true, 0.9);
  from.childByName("switched")->recordLazyOffered(2 * kMinRows);
  from.childByName("switched")->recordLazyLoaded(2 * kMinRows, false);
  // Still below the minimum, so this split reaches no decision and the next one
  // has to finish the measurement with the carried counters and threshold.
  from.childByName("learning")->setEagerMaterializeCandidate(true, 0.5);
  from.childByName("learning")->recordLazyOffered(512);
  from.childByName("learning")->recordLazyLoaded(300, false);
  from.childByName("hookTarget")->setEagerMaterializeCandidate(true, 0.9);
  from.childByName("hookTarget")->recordLazyOffered(2 * kMinRows);
  from.childByName("hookTarget")->recordLazyLoaded(2 * kMinRows, true);
  from.childByName("constant")->setEagerMaterializeCandidate(true, 0.9);
  from.childByName("constant")->recordLazyOffered(2 * kMinRows);
  from.childByName("constant")->recordLazyLoaded(2 * kMinRows, false);
  from.newRead();
  ASSERT_TRUE(from.childByName("switched")->eagerMaterialize());

  ScanSpec to("<root>");
  addFields(to);
  to.childByName("constant")
      ->setConstantValue(BaseVector::createConstant(BIGINT(), 1LL, 1, pool()));
  to.moveAdaptationFrom(from);

  // The decision itself carries, so the first read of the new split is already
  // eager rather than measuring again from zero.
  EXPECT_TRUE(to.childByName("switched")->eagerMaterialize());

  to.childByName("learning")->recordLazyOffered(1536);
  to.childByName("learning")->recordLazyLoaded(725, false);
  to.newRead();
  EXPECT_TRUE(to.childByName("switched")->eagerMaterialize());
  // 1025 of 2048 offered rows: accepted by the carried 0.5 and rejected by the
  // 0.9 default, so both the counters and the threshold travelled.
  EXPECT_TRUE(to.childByName("learning")->eagerMaterialize());
  EXPECT_FALSE(to.childByName("hookTarget")->eagerMaterialize());
  // A constant is never lazy, so it receives no adaptation.
  EXPECT_FALSE(to.childByName("constant")->eagerMaterialize());
}

// The lazy counters are deliberately a separate field pair from 'selectivity_',
// which feeds compareTimeToDropValue.
TEST_F(ScanSpecTest, eagerMaterializeDoesNotAffectFilterOrder) {
  ScanSpec scanSpec("<root>");
  scanSpec.addField("c0", 0);
  auto* candidate = scanSpec.addField("c1", 1);
  scanSpec.addField("c2", 2)->setFilter(
      std::make_shared<BigintRange>(10, 20, false));
  scanSpec.resetCachedValues(false);

  candidate->setEagerMaterializeCandidate(true, 0.9);
  candidate->recordLazyOffered(2 * kMinRows);
  candidate->recordLazyLoaded(2 * kMinRows, false);
  scanSpec.newRead();
  ASSERT_TRUE(candidate->eagerMaterialize());

  // The only filtered child still sorts first, the rest stay in name order.
  std::vector<std::string> order;
  for (const auto& child : scanSpec.children()) {
    order.push_back(child->fieldName());
  }
  EXPECT_THAT(order, ElementsAre("c2", "c0", "c1"));

  // An unfiltered column accumulates no filter statistics, so a non-zero
  // numIn() never makes it participate in stats based reordering.
  EXPECT_EQ(candidate->selectivity().numIn(), 0);
  EXPECT_EQ(candidate->selectivity().numOut(), 0);
}

TEST_F(ScanSpecTest, testFilterOnConstant) {
  auto test = [&](auto&& setup, bool expected) {
    ScanSpec scanSpec("<root>");
    auto* child = scanSpec.addField("c0", 0);
    setup(scanSpec, *child);
    ASSERT_EQ(
        dwio::common::SelectiveStructColumnReaderBase::testFilterOnConstant(
            *child),
        expected);
  };

  // Non-null constants are accepted regardless of filter kind.
  test(
      [&](ScanSpec&, ScanSpec& child) {
        child.setConstantValue(
            BaseVector::createConstant(BIGINT(), 1LL, 1, pool()));
        child.setFilter(std::make_shared<IsNull>());
      },
      true);
  test(
      [&](ScanSpec&, ScanSpec& child) {
        child.setConstantValue(
            BaseVector::createConstant(BIGINT(), 1LL, 1, pool()));
        child.setFilter(std::make_shared<IsNotNull>());
      },
      true);

  // Null constants are accepted only when the filter can match nulls.
  test(
      [&](ScanSpec&, ScanSpec& child) {
        child.setConstantValue(
            BaseVector::createNullConstant(BIGINT(), 1, pool()));
        child.setFilter(std::make_shared<IsNull>());
      },
      true);
  test(
      [&](ScanSpec& scanSpec, ScanSpec& child) {
        child.setConstantValue(
            BaseVector::createNullConstant(BIGINT(), 1, pool()));
        child.setFilter(std::make_shared<IsNotNull>());
      },
      false);

  // For non-constant specs, there is no filter or the filter accepts nulls.
  test([](ScanSpec&, ScanSpec&) {}, true);
  test(
      [](ScanSpec&, ScanSpec& child) {
        child.setFilter(std::make_shared<IsNull>());
      },
      true);
  test(
      [](ScanSpec&, ScanSpec& child) {
        child.setFilter(std::make_shared<IsNotNull>());
      },
      false);
}

// A child added after a snapshot was taken appears at the end of the next one.
TEST_F(ScanSpecTest, stableChildrenAfterAddingChild) {
  ScanSpec scanSpec("<root>");
  scanSpec.addField("c0", 0);
  scanSpec.addField("c1", 1);

  auto* first = scanSpec.childByName("c0");
  auto* second = scanSpec.childByName("c1");
  const auto beforeAdd = scanSpec.stableChildren();
  EXPECT_THAT(*beforeAdd, ElementsAre(Pointer(first), Pointer(second)));

  auto* third = scanSpec.addField("c2", 2);
  EXPECT_THAT(
      *scanSpec.stableChildren(),
      ElementsAre(Pointer(first), Pointer(second), Pointer(third)));

  // The snapshot a reader tree is walking is never mutated.
  EXPECT_THAT(*beforeAdd, ElementsAre(Pointer(first), Pointer(second)));

  // 'c2' is the only child with a filter, so it sorts to the front. The
  // stable order must not follow.
  scanSpec.childByName("c2")->setFilter(
      std::make_shared<BigintRange>(10, 20, false));
  scanSpec.resetCachedValues(true);
  ASSERT_EQ(scanSpec.children().front().get(), third);
  EXPECT_THAT(
      *scanSpec.stableChildren(),
      ElementsAre(Pointer(first), Pointer(second), Pointer(third)));
}

// An add drops the published snapshot. The next call republishes the whole
// order, held or not.
TEST_F(ScanSpecTest, stableChildrenRepublishedAfterAdd) {
  ScanSpec scanSpec("<root>");
  auto* first = scanSpec.addField("c0", 0);
  // Published and dropped, so nothing holds it when 'c1' is added.
  EXPECT_THAT(*scanSpec.stableChildren(), ElementsAre(Pointer(first)));

  auto* second = scanSpec.addField("c1", 1);
  const auto held = scanSpec.stableChildren();
  EXPECT_THAT(*held, ElementsAre(Pointer(first), Pointer(second)));

  // Held this time, so adding 'c2' must leave 'held' alone.
  auto* third = scanSpec.addField("c2", 2);
  EXPECT_THAT(*held, ElementsAre(Pointer(first), Pointer(second)));
  EXPECT_THAT(
      *scanSpec.stableChildren(),
      ElementsAre(Pointer(first), Pointer(second), Pointer(third)));
}

class TypedScanSpecTest : public testing::TestWithParam<TypePtr>,
                          public test::VectorTestBase {
 protected:
  static void SetUpTestCase() {
    memory::MemoryManager::testingSetInstance(memory::MemoryManager::Options{});
  }

  VectorPtr makeConstNullVector(TypePtr type, vector_size_t size) {
    return BaseVector::createNullConstant(type, size, pool());
  }

  void addIsNullFilterRecursive(ScanSpec& scanSpec) {
    scanSpec.setFilter(std::make_shared<velox::common::IsNull>());
    for (auto& child : scanSpec.children()) {
      addIsNullFilterRecursive(*child);
    }
  }

  void addIsNotNullFilterRecursive(ScanSpec& scanSpec) {
    scanSpec.setFilter(std::make_shared<velox::common::IsNotNull>());
    for (auto& child : scanSpec.children()) {
      addIsNullFilterRecursive(*child);
    }
  }

  void addIsNullFilterToLeaf(ScanSpec& scanSpec) {
    if (scanSpec.children().empty()) {
      scanSpec.setFilter(std::make_shared<velox::common::IsNull>());
    } else {
      for (auto& child : scanSpec.children()) {
        addIsNullFilterToLeaf(*child);
      }
    }
  }

  void addIsNotNullFilterToLeaf(ScanSpec& scanSpec) {
    if (scanSpec.children().empty()) {
      scanSpec.setFilter(std::make_shared<velox::common::IsNotNull>());
    } else {
      for (auto& child : scanSpec.children()) {
        addIsNotNullFilterToLeaf(*child);
      }
    }
  }
};

// Due to how subfield filters of maps and arrays are pruning
// and can't affect the row selectivity, the current test skips
// cases when maps and arrays are the lone child of (nested) structs.
INSTANTIATE_TEST_SUITE_P(
    TypedScanSpecTestSuite,
    TypedScanSpecTest,
    testing::Values(
        TINYINT(),
        SMALLINT(),
        INTEGER(),
        BIGINT(),
        REAL(),
        DOUBLE(),
        VARCHAR(),
        VARBINARY(),
        ROW({"int", "real"}, {INTEGER(), REAL()}),
        // TODO: the test cases fail when not specifying names for
        // the struct fields. This indicates bug in internal topology
        // when finding children of nested scan specs.
        ROW({"int", "map"}, {INTEGER(), MAP(INTEGER(), REAL())}),
        ROW({"int", "array"}, {INTEGER(), ARRAY(INTEGER())}),
        ROW({"int0", "array0", "row0"},
            {INTEGER(),
             ARRAY(INTEGER()),
             ROW({"int1", "real1", "row1"},
                 {INTEGER(),
                  REAL(),
                  ROW({"int2", "real2"}, {INTEGER(), REAL()})})})));

TEST_P(TypedScanSpecTest, applyFilterSchemaEvolution) {
  auto rowVector = makeRowVector({
      makeFlatVector<int64_t>(64, folly::identity),
      makeConstNullVector(GetParam(), 64),
  });
  ASSERT_EQ(rowVector->size(), 64);
  LOG(INFO) << "Testing with type: " << rowVector->type()->toString();

  {
    ScanSpec scanSpec("<root>");
    scanSpec.addAllChildFields(*rowVector->type());

    ASSERT_TRUE(scanSpec.childByName("c0"));
    scanSpec.childByName("c0")->setFilter(
        std::make_shared<BigintRange>(32, 64, false));

    ASSERT_TRUE(scanSpec.childByName("c1"));
    addIsNullFilterRecursive(*scanSpec.childByName("c1"));

    uint64_t result = -1ll;
    scanSpec.applyFilter(*rowVector, rowVector->size(), &result);
    ASSERT_EQ(result, -1ll << 32);

    // Now add a non-null filter on the missing column.
    ASSERT_TRUE(scanSpec.childByName("c1"));
    addIsNotNullFilterRecursive(*scanSpec.childByName("c1"));
    result = -1ll;
    scanSpec.applyFilter(*rowVector, rowVector->size(), &result);
    ASSERT_EQ(result, 0);
  }

  {
    ScanSpec scanSpec("<root>");
    scanSpec.addAllChildFields(*rowVector->type());

    ASSERT_TRUE(scanSpec.childByName("c0"));
    scanSpec.childByName("c0")->setFilter(
        std::make_shared<BigintRange>(32, 64, false));

    // Now add a null filter only on the innermost node of the missing column.
    // Should have the same result as recursive filters.
    ASSERT_TRUE(scanSpec.childByName("c1"));
    addIsNullFilterToLeaf(*scanSpec.childByName("c1"));
    uint64_t result = -1ll;
    scanSpec.applyFilter(*rowVector, rowVector->size(), &result);
    ASSERT_EQ(result, -1ll << 32);

    // Now add is not null filter only on the innermost node of the missing
    // column. Should have the same result as recursive filters.
    ASSERT_TRUE(scanSpec.childByName("c1"));
    addIsNotNullFilterToLeaf(*scanSpec.childByName("c1"));
    result = -1ll;
    scanSpec.applyFilter(*rowVector, rowVector->size(), &result);
    ASSERT_EQ(result, 0);
  }
}

} // namespace
} // namespace facebook::velox::common
