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

#pragma once

#include <folly/Executor.h>

#include "velox/common/caching/AsyncDataCache.h"
#include "velox/common/caching/FileHandle.h"
#include "velox/exec/Task.h"

namespace facebook::velox::connector::hive {

/// Prefetches Parquet footer bytes into the AsyncDataCache when splits are
/// added to a task. This warms the cache so that when the driver later opens
/// the reader, the footer read is a RAM cache hit instead of a remote storage
/// fetch.
class FooterPrefetchListener : public exec::SplitListener {
 public:
  FooterPrefetchListener(
      const std::string& taskId,
      const std::string& taskUuid,
      cache::AsyncDataCache* cache,
      FileHandleFactory* fileHandleFactory,
      folly::Executor* ioExecutor,
      uint64_t speculativeIoSize);

  void onAddSplit(const core::PlanNodeId& planNodeId, const exec::Split& split)
      override;

  void onTaskCompletion() override {}

 private:
  cache::AsyncDataCache* const cache_;
  FileHandleFactory* const fileHandleFactory_;
  folly::Executor* const ioExecutor_;
  const uint64_t speculativeIoSize_;
};

/// Creates FooterPrefetchListener instances for tasks when footer prefetching
/// is enabled.
class FooterPrefetchListenerFactory : public exec::SplitListenerFactory {
 public:
  FooterPrefetchListenerFactory(
      cache::AsyncDataCache* cache,
      FileHandleFactory* fileHandleFactory,
      folly::Executor* ioExecutor,
      uint64_t speculativeIoSize);

  std::unique_ptr<exec::SplitListener> create(
      const std::string& taskId,
      const std::string& taskUuid,
      const core::QueryConfig& config) override;

 private:
  cache::AsyncDataCache* const cache_;
  FileHandleFactory* const fileHandleFactory_;
  folly::Executor* const ioExecutor_;
  const uint64_t speculativeIoSize_;
};

} // namespace facebook::velox::connector::hive
