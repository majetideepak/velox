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

#include <algorithm>
#include <gflags/gflags.h>
#include <folly/init/Init.h>

#include "velox/common/file/FileSystems.h"
#include "velox/common/memory/Memory.h"
#include "velox/dwio/common/Reader.h"
#include "velox/dwio/common/ReaderFactory.h"
#include "velox/dwio/dwrf/RegisterDwrfReader.h"
#include "velox/dwio/parquet/RegisterParquetReader.h"

DEFINE_string(data_format, "parquet", "Data format {orc, parquet}");

DEFINE_string(data_path, "", "Data file path");

using namespace facebook::velox;
using namespace facebook::velox::dwio::common;
using namespace facebook::velox::dwrf;

// A temporary program that reads from data file and prints its content
// Usage: velox_example_scan_file --data_format=parquet --data_path file_path
int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, false};
  // To be able to read local files, we need to register the local file
  // filesystem. We also need to register the reader factories.
  filesystems::registerLocalFileSystem();
  dwrf::registerDwrfReaderFactory();
  parquet::registerParquetReaderFactory();
  facebook::velox::memory::MemoryManager::initialize({});
  auto pool = facebook::velox::memory::memoryManager()->addLeafPool();

  dwio::common::ReaderOptions readerOpts{pool.get()};
  FileFormat format = FileFormat::PARQUET;
  if (FLAGS_data_format == "orc") {
    format = FileFormat::ORC;
  } else if (FLAGS_data_format == "parquet") {
    format = FileFormat::PARQUET;
  } else {
    VELOX_FAIL("Invalid data format {}", FLAGS_data_format);
  }
  readerOpts.setFileFormat(format);
  auto reader = dwio::common::getReaderFactory(format)
                    ->createReader(
                        std::make_unique<BufferedInput>(
                            std::make_shared<LocalReadFile>(FLAGS_data_path),
                            readerOpts.memoryPool()),
                        readerOpts);

  VectorPtr batch;
  RowReaderOptions rowReaderOptions;
  auto rowReader = reader->createRowReader(rowReaderOptions);
  while (rowReader->next(500, batch)) {
    auto rowVector = batch->as<RowVector>();
    for (vector_size_t i = 0; i < rowVector->size(); ++i) {
      std::cout << rowVector->toString(i) << std::endl;
    }
  }

  return 0;
}
