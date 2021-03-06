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

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "db/table_properties_collector.h"
#include "options/cf_options.h"
#include "util/file_reader_writer.h"
#include "xengine/options.h"
#include "xengine/table_properties.h"
#include "storage/storage_common.h"

namespace xengine {

namespace monitor {
class HistogramImpl;
}

namespace common {
class Slice;
class Status;
}

namespace db {
class FileDescriptor;
}

namespace table {

struct TableReaderOptions {
  // @param skip_filters Disables loading/accessing the filter block
  TableReaderOptions(const common::ImmutableCFOptions& _ioptions,
                     const util::EnvOptions& _env_options,
                     const db::InternalKeyComparator& _internal_comparator,
                     const db::FileDescriptor* fd = nullptr,
                     monitor::HistogramImpl* file_read_hist = nullptr,
                     bool _skip_filters = false, int _level = -1)
      : ioptions(_ioptions),
        env_options(_env_options),
        internal_comparator(_internal_comparator),
        skip_filters(_skip_filters),
        level(_level),
        fd_(fd),
        file_read_hist_(file_read_hist) {}

  const common::ImmutableCFOptions& ioptions;
  const util::EnvOptions& env_options;
  const db::InternalKeyComparator& internal_comparator;
  // This is only used for BlockBasedTable (reader)
  bool skip_filters;
  // what level this table/file is on, -1 for "not set, don't know"
  int level;
  const db::FileDescriptor* fd_;
  monitor::HistogramImpl* file_read_hist_;
};

struct TableBuilderOptions {
  TableBuilderOptions(
      const common::ImmutableCFOptions& _ioptions,
      const db::InternalKeyComparator& _internal_comparator,
      const std::vector<std::unique_ptr<db::IntTblPropCollectorFactory>>*
          _int_tbl_prop_collector_factories,
      common::CompressionType _compression_type,
      const common::CompressionOptions& _compression_opts,
      const std::string* _compression_dict, bool _skip_filters,
      const std::string& _column_family_name, const storage::LayerPosition &output_position,
      bool _is_flush = false)
      : ioptions(_ioptions),
        internal_comparator(_internal_comparator),
        int_tbl_prop_collector_factories(_int_tbl_prop_collector_factories),
        compression_type(_compression_type),
        compression_opts(_compression_opts),
        compression_dict(_compression_dict),
        skip_filters(_skip_filters),
        column_family_name(_column_family_name),
        output_position_(output_position),
        is_flush(_is_flush) {}
  const common::ImmutableCFOptions& ioptions;
  const db::InternalKeyComparator& internal_comparator;
  const std::vector<std::unique_ptr<db::IntTblPropCollectorFactory>>*
      int_tbl_prop_collector_factories;
  common::CompressionType compression_type;
  const common::CompressionOptions& compression_opts;
  // Data for presetting the compression library's dictionary, or nullptr.
  const std::string* compression_dict;
  bool skip_filters;  // only used by BlockBasedTableBuilder
  const std::string& column_family_name;
  storage::LayerPosition output_position_;  // what level this table/file is on, -1 for "not set, don't know"
  bool is_flush; //used for block cache adding
};

// TableBuilder provides the interface used to build a Table
// (an immutable and sorted map from keys to values).
//
// Multiple threads can invoke const methods on a TableBuilder without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same TableBuilder must use
// external synchronization.
class TableBuilder {
 public:
  // REQUIRES: Either Finish() or Abandon() has been called.
  virtual ~TableBuilder() {}

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  virtual int Add(const common::Slice& key, const common::Slice& value) = 0;
  virtual int set_in_cache_flag() { return 0; }
  virtual bool SupportAddBlock() const { return false; }

  virtual int AddBlock(const common::Slice& block_content,
                                  const common::Slice& block_stats,
                                  const common::Slice& last_key,
                                  const bool has_trailer = true) {
    return common::Status::kNotSupported;
  }

  // Return non-ok iff some error has been detected.
  virtual common::Status status() const = 0;

  // Finish building the table.
  // REQUIRES: Finish(), Abandon() have not been called
  virtual int Finish() = 0;

  // Indicate that the contents of this builder should be abandoned.
  // If the caller is not going to call Finish(), it must call Abandon()
  // before destroying this builder.
  // REQUIRES: Finish(), Abandon() have not been called
  virtual int Abandon() = 0;

  // Number of calls to Add() so far.
  virtual uint64_t NumEntries() const = 0;

  // Size of the file generated so far.  If invoked after a successful
  // Finish() call, returns the size of the final generated file.
  virtual uint64_t FileSize() const = 0;

  // If the user defined table properties collector suggest the file to
  // be further compacted.
  virtual bool NeedCompact() const { return false; }

  // Returns table properties
  virtual TableProperties GetTableProperties() const = 0;
};

}  // namespace table
}  // namespace xengine
