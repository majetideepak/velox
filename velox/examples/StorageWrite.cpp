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

#include <folly/init/Init.h>
#include <folly/executors/IOThreadPoolExecutor.h>

#include "velox/common/memory/Memory.h"
#include "velox/connectors/hive/storage_adapters/s3fs/RegisterS3FileSystem.h"
#include "velox/common/config/Config.h"
#include "velox/connectors/hive/HiveConnector.h"
#include "velox/dwio/parquet/RegisterParquetWriter.h"
#include "velox/exec/TableWriter.h"
#include "velox/exec/tests/utils/AssertQueryBuilder.h"
#include "velox/exec/tests/utils/PlanBuilder.h"
#include "velox/vector/tests/utils/VectorMaker.h"

using namespace facebook::velox;
using namespace facebook::velox::dwio::common;
using namespace facebook::velox::exec::test;
using namespace facebook::velox::test;

DEFINE_string(path, "/tmp/", "Path of the output file");
DEFINE_string(config, "", "Path of the config file");
DEFINE_int32(row_count, 1, "Number of rows to write");

std::shared_ptr<config::ConfigBase> readConfig(const std::string& filePath) {
  std::ifstream configFile(filePath);
  if (!configFile.is_open()) {
    throw std::runtime_error(
        fmt::format("Couldn't open config file {} for reading.", filePath));
  }

  std::unordered_map<std::string, std::string> properties;
  std::string line;
  while (getline(configFile, line)) {
    line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());
    if (line[0] == '#' || line.empty()) {
      continue;
    }
    auto delimiterPos = line.find('=');
    auto name = line.substr(0, delimiterPos);
    auto value = line.substr(delimiterPos + 1);
    properties.emplace(name, value);
  }

  return std::make_shared<config::ConfigBase>(std::move(properties));
}

// A temporary program that writes a Parquet file to storage
// Usage: velox_example_write_parquet --path parquet_file_path --config hive.properties --row-count
int main(int argc, char** argv) {
  folly::Init init{&argc, &argv};
  constexpr char const* kHiveHadoop2ConnectorName = "hive-hadoop2";

  // Only S3, local are supported.
  filesystems::registerLocalFileSystem();
  filesystems::registerS3FileSystem();
  parquet::registerParquetWriterFactory();
  facebook::velox::memory::MemoryManager::initialize({});
  auto pool = facebook::velox::memory::memoryManager()->addLeafPool();
  std::shared_ptr<config::ConfigBase> config;
  if (!FLAGS_config.empty()) {
    config = readConfig(FLAGS_config);
  } else {
    std::unordered_map<std::string, std::string> properties;
    config = std::make_shared<config::ConfigBase>(std::move(properties));
  }

  connector::registerConnectorFactory(
    std::make_shared<connector::hive::HiveConnectorFactory>(
        kHiveHadoop2ConnectorName));

  auto ioExecutor = std::make_unique<folly::IOThreadPoolExecutor>(3);
  auto hiveConnector =
      connector::getConnectorFactory(
        kHiveHadoop2ConnectorName)
          ->newConnector(
              "test-hive",
              config,
              ioExecutor.get());
  connector::registerConnector(hiveConnector);

  const std::string_view kOutputDirectory{FLAGS_path};

  auto rowType = ROW(
      {"c0", "c1", "c2", "c3"}, {BIGINT(), INTEGER(), SMALLINT(), DOUBLE()});

  VectorMaker vectorMaker{pool.get()};
  auto input = vectorMaker.rowVector(
      {vectorMaker.flatVector<int32_t>(FLAGS_row_count, [](auto row) { return row; }),
       vectorMaker.flatVector<int32_t>(FLAGS_row_count, [](auto row) { return row; }),
       vectorMaker.flatVector<int16_t>(FLAGS_row_count, [](auto row) { return row; }),
       vectorMaker.flatVector<double>(FLAGS_row_count, [](auto row) { return row; })});

  // Insert into s3 with one writer.
  auto plan =
      PlanBuilder()
          .values({input})
          .tableWrite(
              kOutputDirectory.data(), dwio::common::FileFormat::PARQUET)
          .planNode();

  // Execute the write plan.
  auto results = AssertQueryBuilder(plan).copyResults(pool.get());

  // First column has number of rows written in the first row and nulls in other
  // rows.
  auto rowCount = results->childAt(exec::TableWriteTraits::kRowCountChannel)
                      ->as<FlatVector<int64_t>>();
  if(rowCount->valueAt(0) != FLAGS_row_count) {
    assert(false);
  }
}
