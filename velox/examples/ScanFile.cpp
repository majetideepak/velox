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
#include <algorithm>
#include <filesystem>

#include "velox/common/file/FileSystems.h"
#include "velox/common/io/IoStatistics.h"
#include "velox/common/memory/Memory.h"
#include "velox/common/testutil/TempDirectoryPath.h"
#include "velox/dwio/common/Reader.h"
#include "velox/dwio/common/ReaderFactory.h"
#include "velox/dwio/dwrf/RegisterDwrfReader.h"
#ifdef VELOX_ENABLE_PARQUET
#include "velox/dwio/parquet/RegisterParquetReader.h"
#endif
#include "velox/vector/BaseVector.h"

using namespace facebook::velox;
using namespace facebook::velox::common::testutil;
using namespace facebook::velox::dwio::common;
using namespace facebook::velox::dwrf;

namespace {

FileFormat inferFileFormat(std::string_view format) {
  if (format == "orc") {
    return FileFormat::ORC;
  }
  if (format == "parquet" || ext == "pq") {
    return FileFormat::PARQUET;
  }
  if (format == "dwrf") {
    return FileFormat::DWRF;
  }
  return FileFormat::UNKNOWN;
}

} // namespace

// A temporary program that reads from an ORC or Parquet file and prints its
// content. The file format is inferred from the file extension (.orc /
// .parquet).
// Usage: velox_example_scan_orc {file_path}
int main(int argc, char** argv) {
  folly::Init init{&argc, &argv};

  if (argc < 3) {
    return 1;
  }

  std::string filePath{argv[1]};
  const auto format = inferFileFormat(argv[2]);
  if (format == FileFormat::UNKNOWN) {
    std::cerr << "Unsupported file format << argv[2]  << std::endl;
    return 1;
  }
#ifndef VELOX_ENABLE_PARQUET
  if (format == FileFormat::PARQUET) {
    std::cerr << "Parquet support not built in (VELOX_ENABLE_PARQUET=OFF)."
              << std::endl;
    return 1;
  }
#endif

  // To be able to read local files, we need to register the local file
  // filesystem. We also need to register the dwrf and parquet reader
  // factories.
  filesystems::registerLocalFileSystem();
  dwrf::registerDwrfReaderFactory();
#ifdef VELOX_ENABLE_PARQUET
  parquet::registerParquetReaderFactory();
#endif
  facebook::velox::memory::MemoryManager::initialize(
      facebook::velox::memory::MemoryManager::Options{});
  auto pool = facebook::velox::memory::memoryManager()->addLeafPool();

<<<<<<< Updated upstream
  std::string filePath{argv[1]};
  auto dataIoStats = std::make_shared<io::IoStatistics>();
  auto metadataIoStats = std::make_shared<io::IoStatistics>();
  dwio::common::ReaderOptions readerOpts(pool.get());
  readerOpts.setDataIoStats(dataIoStats);
  readerOpts.setMetadataIoStats(metadataIoStats);
  // To make DwrfReader reads ORC file, setFileFormat to FileFormat::ORC
  readerOpts.setFileFormat(FileFormat::ORC);
  auto reader = dwio::common::getReaderFactory(FileFormat::ORC)
=======
  dwio::common::ReaderOptions readerOpts{pool.get()};
  readerOpts.setFileFormat(format);
  // ORC files are served by the DWRF reader factory; Parquet has its own.
  const auto factoryFormat =
      format == FileFormat::ORC ? FileFormat::DWRF : format;
  auto reader = dwio::common::getReaderFactory(factoryFormat)
>>>>>>> Stashed changes
                    ->createReader(
                        std::make_unique<BufferedInput>(
                            std::make_shared<LocalReadFile>(filePath),
                            readerOpts.memoryPool()),
                        readerOpts);

  // The Parquet reader expects the caller to provide an allocated result
  // vector and fills it in place; the DWRF reader allocates it on first read
  // when null. Pre-allocating here works for both.
  constexpr vector_size_t kBatchSize = 500;
  VectorPtr batch =
      BaseVector::create(reader->rowType(), kBatchSize, pool.get());
  RowReaderOptions rowReaderOptions;
  auto rowReader = reader->createRowReader(rowReaderOptions);
  while (rowReader->next(kBatchSize, batch)) {
    auto rowVector = batch->as<RowVector>();
    for (vector_size_t i = 0; i < rowVector->size(); ++i) {
      std::cout << rowVector->toString(i) << std::endl;
    }
  }

  return 0;
}
