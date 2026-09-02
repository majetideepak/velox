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

#include "velox/connectors/hive/FooterPrefetchListener.h"

#include "velox/connectors/hive/FileConnectorSplit.h"
#include "velox/dwio/common/Options.h"

namespace facebook::velox::connector::hive {

FooterPrefetchListener::FooterPrefetchListener(
    const std::string& taskId,
    const std::string& taskUuid,
    cache::AsyncDataCache* cache,
    FileHandleFactory* fileHandleFactory,
    folly::Executor* ioExecutor,
    uint64_t speculativeIoSize)
    : SplitListener(taskId, taskUuid),
      cache_(cache),
      fileHandleFactory_(fileHandleFactory),
      ioExecutor_(ioExecutor),
      speculativeIoSize_(speculativeIoSize) {}

void FooterPrefetchListener::onAddSplit(
    const core::PlanNodeId& /*planNodeId*/,
    const exec::Split& split) {
  if (!split.hasConnectorSplit()) {
    return;
  }

  auto* fileSplit =
      dynamic_cast<const FileConnectorSplit*>(split.connectorSplit.get());
  if (fileSplit == nullptr) {
    return;
  }

  if (fileSplit->fileFormat != dwio::common::FileFormat::PARQUET) {
    return;
  }

  if (!fileSplit->properties.has_value() ||
      !fileSplit->properties->fileSize.has_value()) {
    return;
  }
  const uint64_t fileSize = fileSplit->properties->fileSize.value();
  if (fileSize == 0) {
    return;
  }

  const auto readSize = std::min(speculativeIoSize_, fileSize);
  const auto splitOffset = fileSize - readSize;

  // Schedule the footer read on the IO executor. Capture by value so the
  // lambda outlives this call.
  const auto filePath = fileSplit->filePath;
  const auto speculativeIoSize = speculativeIoSize_;
  ioExecutor_->add([cache = cache_,
                    fileHandleFactory = fileHandleFactory_,
                    filePath,
                    speculativeIoSize]() {
    try {
      auto fileHandle = fileHandleFactory->generate(
          FileHandleKey{filePath}, nullptr, nullptr);
      if (!fileHandle.get()) {
        return;
      }

      // Use the actual file size from ReadFile to match what the reader
      // will use in ReaderBase::loadFileMetaData().
      const auto actualFileSize = fileHandle->file->size();
      const auto readSize = std::min(speculativeIoSize, actualFileSize);
      const auto offset = actualFileSize - readSize;

      const auto fileNum = fileHandle->uuid.id();
      cache::RawFileCacheKey key{fileNum, offset};
      if (cache->exists(key)) {
        return;
      }

      folly::SemiFuture<bool> waitFuture(false);
      auto pin = cache->findOrCreate(key, readSize, false, &waitFuture);
      if (pin.empty()) {
        return;
      }

      auto* entry = pin.checkedEntry();
      if (!entry->isExclusive()) {
        // Already loaded by another thread.
        return;
      }

      entry->setGroupId(fileHandle->groupId.id());

      const auto ranges = entry->dataRanges(readSize);
      fileHandle->file->preadv(ranges, offset);
      entry->setExclusiveToShared(
          /*ssdSavable=*/true, /*ssdSavePriority=*/true);
    } catch (const std::exception&) {
      // Footer prefetch is best-effort. If it fails, the normal reader
      // path will fetch the footer synchronously.
    }
  });
}

FooterPrefetchListenerFactory::FooterPrefetchListenerFactory(
    cache::AsyncDataCache* cache,
    FileHandleFactory* fileHandleFactory,
    folly::Executor* ioExecutor,
    uint64_t speculativeIoSize)
    : cache_(cache),
      fileHandleFactory_(fileHandleFactory),
      ioExecutor_(ioExecutor),
      speculativeIoSize_(speculativeIoSize) {}

std::unique_ptr<exec::SplitListener> FooterPrefetchListenerFactory::create(
    const std::string& taskId,
    const std::string& taskUuid,
    const core::QueryConfig& /*config*/) {
  if (cache_ == nullptr || ioExecutor_ == nullptr ||
      fileHandleFactory_ == nullptr) {
    return nullptr;
  }
  return std::make_unique<FooterPrefetchListener>(
      taskId,
      taskUuid,
      cache_,
      fileHandleFactory_,
      ioExecutor_,
      speculativeIoSize_);
}

} // namespace facebook::velox::connector::hive
