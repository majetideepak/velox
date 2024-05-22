#include <vector>

#include "velox/connectors/hive/HiveConnectorSplit.h"
#include "velox/dwio/parquet/reader/ParquetReader.h"
#include "velox/exec/PlanNodeStats.h"
#include "velox/exec/tests/utils/QueryAssertions.h"
#include "velox/exec/tests/utils/PlanBuilder.h"
#include "velox/functions/prestosql/aggregates/RegisterAggregateFunctions.h"
#include "velox/functions/prestosql/registration/RegistrationFunctions.h"
#include "velox/parse/TypeResolver.h"
#include "velox/type/Type.h"

using namespace facebook::velox;

static constexpr const char* kHiveConnectorName = "hive";
static constexpr const char* kHiveConnectorId = "test-hive";

// A Velox app that scans a Parquet file and aggregates some columns.
int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Missing input parquet file";
    return -1;
  }
  int splitCount = 1;
  int driverCount = 1;
  if (argc >= 3) {
    splitCount = std::atoi(argv[2]);
  }
  if (argc >= 4) {
    driverCount = std::atoi(argv[3]);
  }
  std::cout << "SplitCount: " << splitCount << ", DriverCount: " << driverCount << std::endl;
  // register functions, filesystem, hive connector, MemoryManager.
  parse::registerTypeResolver();
  functions::prestosql::registerAllScalarFunctions();
  aggregate::prestosql::registerAllAggregateFunctions();
  filesystems::registerLocalFileSystem();
  auto hiveConfig = std::make_shared<core::MemConfig>();
  auto hiveConnector = connector::getConnectorFactory(kHiveConnectorName)
                           ->newConnector(kHiveConnectorId, hiveConfig);
  connector::registerConnector(hiveConnector);
  // Initializes the process-wide memory-manager with the default options.
  memory::initializeMemoryManager({});

  // Build the plan.
  auto filePath = argv[1];
  auto pool = memory::memoryManager()->addLeafPool();
  auto selectedRowType =
      ROW({"l_shipdate", "l_extendedprice", "l_quantity", "l_discount"},
          {DATE(), DOUBLE(), DOUBLE(), DOUBLE()});

  core::PlanNodeId lineitemPlanNodeId;
  auto plan =
      exec::test::PlanBuilder(pool.get())
          .tableScan(
              selectedRowType)
          .capturePlanNodeId(lineitemPlanNodeId)
          .project({"l_extendedprice * l_discount"})
          .partialAggregation({}, {"sum(p0)"})
          .localPartition(std::vector<std::string>{})
          .finalAggregation()
          .planNode();

  // Execute the plan.
  bool noMoreSplits = false;
  auto file =
      filesystems::getFileSystem(filePath, nullptr)->openFileForRead(filePath);
  auto fileSize = file->size();
  auto splitSize = std::ceil((fileSize) / splitCount);
  auto addSplits = [&](exec::Task* task) {
    if (!noMoreSplits) {
      for (int i = 0; i < splitCount; i++) {
        auto split = std::make_shared<connector::hive::HiveConnectorSplit>(
            kHiveConnectorId, filePath, dwio::common::FileFormat::PARQUET, i * splitSize, splitSize);
        task->addSplit(lineitemPlanNodeId, exec::Split(split));
      }
      task->noMoreSplits(lineitemPlanNodeId);
    }
    noMoreSplits = true;
  };
  exec::test::CursorParameters params;
  params.maxDrivers = driverCount;
  params.planNode = plan;
  auto [cursor, resultVec] = exec::test::readCursor(params, addSplits);

  // Consume the results.
  assert(1 == resultVec.size());
  assert(1 == resultVec[0]->size());
  auto result = resultVec[0]->childAt(0)->as<SimpleVector<double>>()->valueAt(0);
  std::cout << "Result:" << result << std::endl;
  auto task = cursor->task();
  assert(task->isFinished());
  return 1;
}
