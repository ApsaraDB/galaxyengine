// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/index_builder.h"
#include <assert.h>
#include <inttypes.h>

#include <list>
#include <string>

#include "table/format.h"
#include "table/partitioned_filter_block.h"
#include "xengine/comparator.h"
#include "xengine/flush_block_policy.h"

using namespace xengine;
using namespace db;
using namespace util;
using namespace common;

// Without anonymous namespace here, we fail the warning -Wmissing-prototypes
namespace xengine {
namespace table {
// Create a index builder based on its type.
IndexBuilder* IndexBuilder::CreateIndexBuilder(
    BlockBasedTableOptions::IndexType index_type,
    const InternalKeyComparator* comparator,
    const InternalKeySliceTransform* int_key_slice_transform,
    const BlockBasedTableOptions& table_opt, WritableBuffer* buf) {
  switch (index_type) {
    case BlockBasedTableOptions::kBinarySearch: {
      return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, ShortenedIndexBuilder,
          comparator, table_opt.index_block_restart_interval, buf);
//      return new ShortenedIndexBuilder(
//          comparator, table_opt.index_block_restart_interval, buf);
    }
    case BlockBasedTableOptions::kHashSearch: {
      return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, HashIndexBuilder, comparator,
          int_key_slice_transform, table_opt.index_block_restart_interval);
//      return new HashIndexBuilder(comparator, int_key_slice_transform,
//                                  table_opt.index_block_restart_interval);
    }
    case BlockBasedTableOptions::kTwoLevelIndexSearch: {
      return PartitionedIndexBuilder::CreateIndexBuilder(comparator, table_opt);
    }
    default: {
      assert(!"Do not recognize the index type ");
      return nullptr;
    }
  }
  // impossible.
  assert(false);
  return nullptr;
}

PartitionedIndexBuilder* PartitionedIndexBuilder::CreateIndexBuilder(
    const InternalKeyComparator* comparator,
    const BlockBasedTableOptions& table_opt) {
//  return new PartitionedIndexBuilder(comparator, table_opt);
  return MOD_NEW_OBJECT(memory::ModId::kDefaultMod, PartitionedIndexBuilder, comparator, table_opt);
}

PartitionedIndexBuilder::PartitionedIndexBuilder(
    const InternalKeyComparator* comparator,
    const BlockBasedTableOptions& table_opt)
    : IndexBuilder(comparator),
      index_block_builder_(table_opt.index_block_restart_interval),
      sub_index_builder_(nullptr),
      table_opt_(table_opt) {}

PartitionedIndexBuilder::~PartitionedIndexBuilder() {
//  delete sub_index_builder_;
  MOD_DELETE_OBJECT(ShortenedIndexBuilder, sub_index_builder_);
}

void PartitionedIndexBuilder::MakeNewSubIndexBuilder() {
  assert(sub_index_builder_ == nullptr);
//  sub_index_builder_ = new ShortenedIndexBuilder(
//      comparator_, table_opt_.index_block_restart_interval);
  sub_index_builder_ = MOD_NEW_OBJECT(memory::ModId::kDefaultMod, ShortenedIndexBuilder,
      comparator_, table_opt_.index_block_restart_interval);
  flush_policy_.reset(FlushBlockBySizePolicyFactory::NewFlushBlockPolicy(
      table_opt_.metadata_block_size, table_opt_.block_size_deviation,
      sub_index_builder_->index_block_builder_));
}

void PartitionedIndexBuilder::AddIndexEntry(
    std::string* last_key_in_current_block,
    const Slice* first_key_in_next_block, const BlockHandle& block_handle) {
  // Note: to avoid two consecuitive flush in the same method call, we do not
  // check flush policy when adding the last key
  if (UNLIKELY(first_key_in_next_block == nullptr)) {  // no more keys
    if (sub_index_builder_ == nullptr) {
      MakeNewSubIndexBuilder();
    }
    sub_index_builder_->AddIndexEntry(last_key_in_current_block,
                                      first_key_in_next_block, block_handle);
    sub_index_last_key_ = std::string(*last_key_in_current_block);
    entries_.push_back(
        {sub_index_last_key_,
         std::unique_ptr<ShortenedIndexBuilder>(sub_index_builder_)});
    sub_index_builder_ = nullptr;
    cut_filter_block = true;
  } else {
    // apply flush policy only to non-empty sub_index_builder_
    if (sub_index_builder_ != nullptr) {
      std::string handle_encoding;
      block_handle.EncodeTo(&handle_encoding);
      bool do_flush =
          flush_policy_->Update(*last_key_in_current_block, handle_encoding);
      if (do_flush) {
        entries_.push_back(
            {sub_index_last_key_,
             std::unique_ptr<ShortenedIndexBuilder>(sub_index_builder_)});
        cut_filter_block = true;
        sub_index_builder_ = nullptr;
      }
    }
    if (sub_index_builder_ == nullptr) {
      MakeNewSubIndexBuilder();
    }
    sub_index_builder_->AddIndexEntry(last_key_in_current_block,
                                      first_key_in_next_block, block_handle);
    sub_index_last_key_ = std::string(*last_key_in_current_block);
  }
}

Status PartitionedIndexBuilder::Finish(
    IndexBlocks* index_blocks, const BlockHandle& last_partition_block_handle) {
  assert(!entries_.empty());
  // It must be set to null after last key is added
  assert(sub_index_builder_ == nullptr);
  if (finishing_indexes == true) {
    Entry& last_entry = entries_.front();
    std::string handle_encoding;
    last_partition_block_handle.EncodeTo(&handle_encoding);
    index_block_builder_.Add(last_entry.key, handle_encoding);
    entries_.pop_front();
  }
  // If there is no sub_index left, then return the 2nd level index.
  if (UNLIKELY(entries_.empty())) {
    index_blocks->index_block_contents = index_block_builder_.Finish();
    return Status::OK();
  } else {
    // Finish the next partition index in line and Incomplete() to indicate we
    // expect more calls to Finish
    Entry& entry = entries_.front();
    auto s = entry.value->Finish(index_blocks);
    finishing_indexes = true;
    return s.ok() ? Status::Incomplete() : s;
  }
}

size_t PartitionedIndexBuilder::EstimatedSize() const {
  size_t total = 0;
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    total += it->value->EstimatedSize();
  }
  total += index_block_builder_.CurrentSizeEstimate();
  total +=
      sub_index_builder_ == nullptr ? 0 : sub_index_builder_->EstimatedSize();
  return total;
}
}  // namespace table
}  // namespace xengine
