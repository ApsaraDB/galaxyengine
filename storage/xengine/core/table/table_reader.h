// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <memory>
#include "table/internal_iterator.h"

namespace xengine {

namespace common {
class Slice;
struct ReadOptions;
}

namespace util {
class Arena;
}

namespace table {

struct TableProperties;
class GetContext;
class InternalIterator;

// A Table is a sorted map from strings to strings.  Tables are
// immutable and persistent.  A Table may be safely accessed from
// multiple threads without external synchronization.
class TableReader {
 public:
  virtual ~TableReader() {}

  // Returns a new iterator over the table contents.
  // The result of NewIterator() is initially invalid (caller must
  // call one of the Seek methods on the iterator before using it).
  // arena: If not null, the arena needs to be used to allocate the Iterator.
  //        When destroying the iterator, the caller will not call "delete"
  //        but Iterator::~Iterator() directly. The destructor needs to destroy
  //        all the states but those allocated in arena.
  // skip_filters: disables checking the bloom filters even if they exist. This
  //               option is effective only for block-based table format.
  virtual InternalIterator* NewIterator(const common::ReadOptions&,
                                        memory::SimpleAllocator* arena = nullptr,
                                        bool skip_filters = false,
                                        const uint64_t scan_add_blocks_limit = 0) = 0;

  virtual InternalIterator* NewRangeTombstoneIterator(
      const common::ReadOptions& read_options) {
    return nullptr;
  }

  // Given a key, return an approximate byte offset in the file where
  // the data for that key begins (or would begin if the key were
  // present in the file).  The returned value is in terms of file
  // bytes, and so includes effects like compression of the underlying data.
  // E.g., the approximate offset of the last key in the table will
  // be close to the file length.
  virtual uint64_t ApproximateOffsetOf(const common::Slice& key) = 0;

  // Set up the table for Compaction. Might change some parameters with
  // posix_fadvise
  virtual void SetupForCompaction() = 0;

  virtual std::shared_ptr<const TableProperties> GetTableProperties() const = 0;

  // Prepare work that can be done before the real Get()
  virtual void Prepare(const common::Slice& target) {}

  // Report an approximation of how much memory has been used.
  virtual size_t ApproximateMemoryUsage() const = 0;

  // set mod id for index reader
  virtual void set_mod_id(const size_t mod_id) const { UNUSED(mod_id); }
  // Calls get_context->SaveValue() repeatedly, starting with
  // the entry found after a call to Seek(key), until it returns false.
  // May not make such a call if filter policy says that key is not present.
  //
  // get_context->MarkKeyMayExist needs to be called when it is configured to be
  // memory only and the key is not found in the block cache.
  //
  // readOptions is the options for the read
  // key is the key to search for
  // skip_filters: disables checking the bloom filters even if they exist. This
  //               option is effective only for block-based table format.
  virtual common::Status Get(const common::ReadOptions& readOptions,
                             const common::Slice& key, GetContext* get_context,
                             bool skip_filters = false) = 0;

  // Prefetch data corresponding to a give range of keys
  // Typically this functionality is required for table implementations that
  // persists the data on a non volatile storage medium like disk/SSD
  virtual common::Status Prefetch(const common::Slice* begin = nullptr,
                                  const common::Slice* end = nullptr) {
    (void)begin;
    (void)end;
    // Default implementation is NOOP.
    // The child class should implement functionality when applicable
    return common::Status::OK();
  }

  // convert db file to a human readable form
  virtual common::Status DumpTable(util::WritableFile* out_file) {
    return common::Status::NotSupported("DumpTable() not supported");
  }

  virtual common::Status check_range(const common::Slice &start, const common::Slice &end, bool &result) {
    return common::Status::NotSupported("check_range() not supported");
  }

  virtual void Close() {}

  virtual uint64_t get_usable_size() { return 1; }
};

}  // namespace table
}  // namespace xengine
