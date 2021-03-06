// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#pragma once
#ifndef ROCKSDB_LITE
#include <stdint.h>
#include <string>
#include <vector>
#include "table/bloom_block.h"
#include "table/plain_table_index.h"
#include "table/plain_table_key_coding.h"
#include "table/table_builder.h"
#include "xengine/options.h"
#include "xengine/status.h"
#include "xengine/table.h"
#include "xengine/table_properties.h"

namespace xengine {

namespace util {
class WritableFile;
}

namespace table {

class BlockBuilder;
class BlockHandle;
class TableBuilder;

class PlainTableBuilder : public TableBuilder {
 public:
  // Create a builder that will store the contents of the table it is
  // building in *file.  Does not close the file.  It is up to the
  // caller to close the file after calling Finish(). The output file
  // will be part of level specified by 'level'.  A value of -1 means
  // that the caller does not know which level the output file will reside.
  PlainTableBuilder(
      const common::ImmutableCFOptions& ioptions,
      const std::vector<std::unique_ptr<db::IntTblPropCollectorFactory>>*
          int_tbl_prop_collector_factories,
      uint32_t column_family_id, util::WritableFileWriter* file,
      uint32_t user_key_size, EncodingType encoding_type,
      size_t index_sparseness, uint32_t bloom_bits_per_key,
      const std::string& column_family_name, uint32_t num_probes = 6,
      size_t huge_page_tlb_size = 0, double hash_table_ratio = 0,
      bool store_index_in_file = false);

  // REQUIRES: Either Finish() or Abandon() has been called.
  ~PlainTableBuilder();

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  int Add(const common::Slice& key, const common::Slice& value) override;

  // Return non-ok iff some error has been detected.
  common::Status status() const override;

  // Finish building the table.  Stops using the file passed to the
  // constructor after this function returns.
  // REQUIRES: Finish(), Abandon() have not been called
  int Finish() override;

  // Indicate that the contents of this builder should be abandoned.  Stops
  // using the file passed to the constructor after this function returns.
  // If the caller is not going to call Finish(), it must call Abandon()
  // before destroying this builder.
  // REQUIRES: Finish(), Abandon() have not been called
  int Abandon() override;

  // Number of calls to Add() so far.
  uint64_t NumEntries() const override;

  // Size of the file generated so far.  If invoked after a successful
  // Finish() call, returns the size of the final generated file.
  uint64_t FileSize() const override;

  TableProperties GetTableProperties() const override { return properties_; }

  bool SaveIndexInFile() const { return store_index_in_file_; }

 private:
  util::Arena arena_;
  const common::ImmutableCFOptions& ioptions_;
  std::vector<std::unique_ptr<db::IntTblPropCollector>>
      table_properties_collectors_;

  BloomBlockBuilder bloom_block_;
  std::unique_ptr<PlainTableIndexBuilder> index_builder_;

  util::WritableFileWriter* file_;
  uint64_t offset_ = 0;
  uint32_t bloom_bits_per_key_;
  size_t huge_page_tlb_size_;
  common::Status status_;
  TableProperties properties_;
  PlainTableKeyEncoder encoder_;

  bool store_index_in_file_;

  std::vector<uint32_t> keys_or_prefixes_hashes_;
  bool closed_ = false;  // Either Finish() or Abandon() has been called.

  const common::SliceTransform* prefix_extractor_;

  common::Slice GetPrefix(const common::Slice& target) const {
    assert(target.size() >= 8);  // target is internal key
    return GetPrefixFromUserKey(GetUserKey(target));
  }

  common::Slice GetPrefix(const db::ParsedInternalKey& target) const {
    return GetPrefixFromUserKey(target.user_key);
  }

  common::Slice GetUserKey(const common::Slice& key) const {
    return common::Slice(key.data(), key.size() - 8);
  }

  common::Slice GetPrefixFromUserKey(const common::Slice& user_key) const {
    if (!IsTotalOrderMode()) {
      return prefix_extractor_->Transform(user_key);
    } else {
      // Use empty slice as prefix if prefix_extractor is not set.
      // In that case,
      // it falls back to pure binary search and
      // total iterator seek is supported.
      return common::Slice();
    }
  }

  bool IsTotalOrderMode() const { return (prefix_extractor_ == nullptr); }

  // No copying allowed
  PlainTableBuilder(const PlainTableBuilder&) = delete;
  void operator=(const PlainTableBuilder&) = delete;
};

}  // namespace table
}  // namespace xengine

#endif  // ROCKSDB_LITE
