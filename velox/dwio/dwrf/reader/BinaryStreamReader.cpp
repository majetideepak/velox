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

#include "velox/dwio/dwrf/reader/BinaryStreamReader.h"
#include "velox/dwio/common/exception/Exception.h"

namespace facebook::velox::dwrf::detail {

namespace {
class UnsupportedStrideIndexProvider : public StrideIndexProvider {
  uint64_t getStrideIndex() const override {
    throw std::runtime_error("StripeStreams do not support stride operations");
  }
};
} // namespace

BinaryStripeStreams::BinaryStripeStreams(
    StripeReaderBase& stripeReader,
    const dwio::common::ColumnSelector& selector,
    const uint32_t stripeIndex)
    : preload_(true), // TODO: is preload required ?
      stripeReadState_{std::make_shared<StripeReadState>(
          stripeReader.readerBaseShared(),
          stripeReader.fetchStripe(stripeIndex, preload_))},
      stripeStreams_{
          stripeReadState_,
          &selector,
          nullptr,
          options_,
          stripeReadState_->stripeMetadata->stripeInfo.offset(),
          static_cast<int64_t>(
              stripeReadState_->stripeMetadata->stripeInfo.numberOfRows()),
          UnsupportedStrideIndexProvider(),
          stripeIndex} {
  if (!preload_) {
    stripeStreams_.loadReadPlan();
  }
  encodingKeys_ = stripeStreams_.getEncodingKeys();
  nodeToStreamIdMap_ = stripeStreams_.getStreamIdentifiers();
}

std::vector<proto::ColumnEncoding> BinaryStripeStreams::getEncodings(
    const uint32_t nodeId) const {
  if (encodingKeys_.count(nodeId) == 0) {
    return {};
  }

  auto sequenceIds = encodingKeys_.at(nodeId);
  std::sort(sequenceIds.begin(), sequenceIds.end());

  std::vector<proto::ColumnEncoding> encodings;
  encodings.reserve(sequenceIds.size());

  for (const auto sequenceId : sequenceIds) {
    auto encoding = stripeStreams_.getEncoding({nodeId, sequenceId});
    encoding.set_node(nodeId);
    encoding.set_sequence(sequenceId);
    encodings.push_back(encoding);
  }
  return encodings;
}

std::vector<DwrfStreamIdentifier> BinaryStripeStreams::getStreamIdentifiers(
    const uint32_t nodeId) const {
  if (nodeToStreamIdMap_.count(nodeId) == 0) {
    return {};
  }

  return nodeToStreamIdMap_.at(nodeId);
}

BinaryStreamReader::BinaryStreamReader(
    const std::shared_ptr<ReaderBase>& reader,
    const std::vector<uint64_t>& columnIds)
    : columnSelector_{reader->schema(), columnIds},
      numStripes_{folly::to<uint32_t>(reader->footer().stripesSize())},
      stripeReaderBase_{reader},
      stripeIndex_{0} {
  VELOX_CHECK(!reader->footer().hasEncryption(), "encryption not supported");
  VELOX_CHECK(!columnIds.empty(), "Atleast one column expected to be read");
}

std::unique_ptr<BinaryStripeStreams> BinaryStreamReader::next() {
  if (stripeIndex_ >= numStripes_) {
    return nullptr;
  }
  return std::make_unique<BinaryStripeStreams>(
      stripeReaderBase_, columnSelector_, stripeIndex_++);
}

std::unordered_map<uint32_t, proto::ColumnStatistics>
BinaryStreamReader::getStatistics() const {
  std::unordered_map<uint32_t, proto::ColumnStatistics> stats;
  auto footerStatsSize =
      stripeReaderBase_.getReader().footer().statisticsSize();
  auto typesSize = stripeReaderBase_.getReader().footer().typesSize();

  if (footerStatsSize == 0) {
    VELOX_CHECK_EQ(
        numStripes_,
        0,
        "Corrupted file detected, Footer stats are missing, but stripes are present");
    for (auto node = 0; node < typesSize; node++) {
      if (columnSelector_.shouldReadNode(node)) {
        stats[node] = proto::ColumnStatistics();
      }
    }
  } else {
    VELOX_CHECK_EQ(
        footerStatsSize, typesSize, "different number of nodes and statistics");
    // Node 0 is always selected by ColumnSelector, though this can be
    // disabled for the current use cases.
    for (auto node = 0; node < footerStatsSize; node++) {
      if (columnSelector_.shouldReadNode(node)) {
        stats[node] =
            stripeReaderBase_.getReader().footer().dwrfStatistics(node);
      }
    }
  }
  return stats;
}

uint32_t BinaryStreamReader::getStrideLen() const {
  return stripeReaderBase_.getReader().footer().rowIndexStride();
}

} // namespace facebook::velox::dwrf::detail
