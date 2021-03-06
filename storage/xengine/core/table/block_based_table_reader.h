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
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "options/cf_options.h"
#include "table/filter_block.h"
#include "table/format.h"
#include "table/persistent_cache_helper.h"
#include "table/table_properties_internal.h"
#include "table/table_reader.h"
#include "table/two_level_iterator.h"
#include "util/coding.h"
#include "util/file_reader_writer.h"
#include "xengine/options.h"
#include "xengine/persistent_cache.h"
#include "xengine/statistics.h"
#include "xengine/status.h"
#include "xengine/table.h"

namespace xengine {

namespace cache {
class Cache;
}

namespace util {
struct EnvOptions;
class RandomAccessFile;
class WritableFile;
}

namespace common {
struct ReadOptions;
}

namespace db {
class InternalKeyComparator;
class TableCache;
}

namespace table {

class Block;
class BlockIter;
class BlockHandle;
class FilterBlockReader;
class BlockBasedFilterBlockReader;
class FullFilterBlockReader;
class Footer;
class TableReader;
struct BlockBasedTableOptions;
class GetContext;
class InternalIterator;

typedef std::vector<std::pair<std::string, std::string>> KVPairBlock;

// A Table is a sorted map from strings to strings.  Tables are
// immutable and persistent.  A Table may be safely accessed from
// multiple threads without external synchronization.
class BlockBasedTable : public TableReader {
 public:
  static const std::string kFilterBlockPrefix;
  static const std::string kFullFilterBlockPrefix;
  static const std::string kPartitionedFilterBlockPrefix;
  // The longest prefix of the cache key used to identify blocks.
  // For Posix files the unique ID is three varints.
  static const size_t kMaxCacheKeyPrefixSize = util::kMaxVarint64Length * 3 + 1;

  // Attempt to open the table that is stored in bytes [0..file_size)
  // of "file", and read the metadata entries necessary to allow
  // retrieving data from the table.
  //
  // If successful, returns ok and sets "*table_reader" to the newly opened
  // table.  The client should delete "*table_reader" when no longer needed.
  // If there was an error while initializing the table, sets "*table_reader"
  // to nullptr and returns a non-ok status.
  //
  // @param file must remain live while this Table is in use.
  // @param prefetch_index_and_filter_in_cache can be used to disable
  // prefetching of
  //    index and filter blocks into block cache at startup
  // @param skip_filters Disables loading/accessing the filter block. Overrides
  //    prefetch_index_and_filter_in_cache, so filter will be skipped if both
  //    are set.
  static common::Status Open(
      const common::ImmutableCFOptions& ioptions,
      const util::EnvOptions& env_options,
      const BlockBasedTableOptions& table_options,
      const db::InternalKeyComparator& internal_key_comparator,
      std::unique_ptr<util::RandomAccessFileReader>&& file, uint64_t file_size,
      std::unique_ptr<TableReader>* table_reader,
      bool prefetch_index_and_filter_in_cache = true, bool skip_filters = false,
      int level = -1);

  bool PrefixMayMatch(const common::Slice& internal_key);

  // Returns a new iterator over the table contents.
  // The result of NewIterator() is initially invalid (caller must
  // call one of the Seek methods on the iterator before using it).
  // @param skip_filters Disables loading/accessing the filter block
  InternalIterator* NewIterator(const common::ReadOptions&,
                                memory::SimpleAllocator* arena = nullptr,
                                bool skip_filters = false,
                                const uint64_t scan_add_blocks_limit = 0) override;

  InternalIterator* NewRangeTombstoneIterator(
      const common::ReadOptions& read_options) override;

  // @param skip_filters Disables loading/accessing the filter block
  common::Status Get(const common::ReadOptions& readOptions,
                     const common::Slice& key, GetContext* get_context,
                     bool skip_filters = false) override;

  // Pre-fetch the disk blocks that correspond to the key range specified by
  // (kbegin, kend). The call will return error status in the event of
  // IO or iteration error.
  common::Status Prefetch(const common::Slice* begin,
                          const common::Slice* end) override;

  // Given a key, return an approximate byte offset in the file where
  // the data for that key begins (or would begin if the key were
  // present in the file).  The returned value is in terms of file
  // bytes, and so includes effects like compression of the underlying data.
  // E.g., the approximate offset of the last key in the table will
  // be close to the file length.
  uint64_t ApproximateOffsetOf(const common::Slice& key) override;

  // Returns true if the block for the specified key is in cache.
  // REQUIRES: key is in this table && block cache enabled
  bool TEST_KeyInCache(const common::ReadOptions& options,
                       const common::Slice& key);

  // Set up the table for Compaction. Might change some parameters with
  // posix_fadvise
  void SetupForCompaction() override;

  std::shared_ptr<const TableProperties> GetTableProperties() const override;

  size_t ApproximateMemoryUsage() const override;
  void set_mod_id(const size_t mod_id) const override;
  // convert SST file to a human readable form
  common::Status DumpTable(util::WritableFile* out_file) override;

  void Close() override;

  ~BlockBasedTable();

  bool TEST_filter_block_preloaded() const;
  bool TEST_index_reader_preloaded() const;

  // IndexReader is the interface that provide the functionality for index
  // access.
  class IndexReader {
   public:
    explicit IndexReader(const util::Comparator* comparator,
                         monitor::Statistics* stats)
        : comparator_(comparator), statistics_(stats) {}

    virtual ~IndexReader() {}

    // Create an iterator for index access.
    // If iter is null then a new object is created on heap and the callee will
    // have the ownership. If a non-null iter is passed in it will be used, and
    // the returned value is either the same as iter or a new on-heap object
    // that
    // wrapps the passed iter. In the latter case the return value would point
    // to
    // a different object then iter and the callee has the ownership of the
    // returned object.
    virtual InternalIterator* NewIterator(BlockIter* iter = nullptr,
                                          bool total_order_seek = true) = 0;

    // The size of the index.
    virtual size_t size() const = 0;
    // Memory usage of the index block
    virtual size_t usable_size() const = 0;
    // return the statistics pointer
    virtual monitor::Statistics* statistics() const { return statistics_; }
    // Report an approximation of how much memory has been used other than
    // memory
    // that was allocated in block cache.
    virtual size_t ApproximateMemoryUsage() const = 0;
    // set mod_id for index_block's data
    virtual void set_mod_id(const size_t mod_id) const = 0;

   protected:
    const util::Comparator* comparator_;

   private:
    monitor::Statistics* statistics_;
  };

  static common::Slice GetCacheKey(const char* cache_key_prefix,
                                   size_t cache_key_prefix_size,
                                   const BlockHandle& handle, char* cache_key);

  // Retrieve all key value pairs from data blocks in the table.
  // The key retrieved are internal keys.
  common::Status GetKVPairsFromDataBlocks(
      std::vector<KVPairBlock>* kv_pair_blocks);

  class BlockEntryIteratorState;

  friend class PartitionIndexReader;

  template <class TValue>
  struct CachableEntry;
  InternalIterator* create_index_iterator(
      const common::ReadOptions& read_options, BlockIter* input_iter,
      CachableEntry<IndexReader>* index_entry) {
    return NewIndexIterator(read_options, input_iter, index_entry);
  }
  InternalIterator* create_data_block_iterator(const common::ReadOptions& ro,
                                               const BlockHandle& block_handle,
                                               BlockIter* input_iter) {
    return NewDataBlockIterator(rep_, ro, block_handle, input_iter);
  }

 protected:
  struct Rep;
  Rep* rep_;
  explicit BlockBasedTable(Rep* rep)
      : rep_(rep), compaction_optimized_(false) {}

 private:
  bool compaction_optimized_;

  // input_iter: if it is not null, update this one and return it as Iterator
  static InternalIterator* NewDataBlockIterator(
      Rep* rep, const common::ReadOptions& ro, const common::Slice& index_value,
      BlockIter* input_iter = nullptr, bool is_index = false);
  static InternalIterator* NewDataBlockIterator(
      Rep* rep, const common::ReadOptions& ro, const BlockHandle& block_hanlde,
      BlockIter* input_iter = nullptr, bool is_index = false,
      common::Status s = common::Status());
  // If block cache enabled (compressed or uncompressed), looks for the block
  // identified by handle in (1) uncompressed cache, (2) compressed cache, and
  // then (3) file. If found, inserts into the cache(s) that were searched
  // unsuccessfully (e.g., if found in file, will add to both uncompressed and
  // compressed caches if they're enabled).
  //
  // @param block_entry value is set to the uncompressed block if found. If
  //    in uncompressed block cache, also sets cache_handle to reference that
  //    block.
  static common::Status MaybeLoadDataBlockToCache(
      Rep* rep, const common::ReadOptions& ro, const BlockHandle& handle,
      common::Slice compression_dict, CachableEntry<Block>* block_entry,
      bool is_index = false);

  // For the following two functions:
  // if `no_io == true`, we will not try to read filter/index from sst file
  // were they not present in cache yet.
  CachableEntry<FilterBlockReader> GetFilter(bool no_io = false) const;
  virtual CachableEntry<FilterBlockReader> GetFilter(
      const BlockHandle& filter_blk_handle, const bool is_a_filter_partition,
      bool no_io) const;

  // Get the iterator from the index reader.
  // If input_iter is not set, return new Iterator
  // If input_iter is set, update it and return it as Iterator
  //
  // Note: ErrorIterator with common::Status::Incomplete shall be returned if
  // all the
  // following conditions are met:
  //  1. We enabled table_options.cache_index_and_filter_blocks.
  //  2. index is not present in block cache.
  //  3. We disallowed any io to be performed, that is, read_options ==
  //     kBlockCacheTier
  InternalIterator* NewIndexIterator(
      const common::ReadOptions& read_options, BlockIter* input_iter = nullptr,
      CachableEntry<IndexReader>* index_entry = nullptr);

  // Read block cache from block caches (if set): block_cache and
  // block_cache_compressed.
  // On success, common::Status::OK with be returned and @block will be
  // populated with
  // pointer to the block as well as its block handle.
  // @param compression_dict Data for presetting the compression library's
  //    dictionary.
  static common::Status GetDataBlockFromCache(
      const common::Slice& block_cache_key,
      const common::Slice& compressed_block_cache_key,
      cache::Cache* block_cache, cache::Cache* block_cache_compressed,
      const common::ImmutableCFOptions& ioptions,
      const common::ReadOptions& read_options,
      BlockBasedTable::CachableEntry<Block>* block, uint32_t format_version,
      const common::Slice& compression_dict, size_t read_amp_bytes_per_bit,
      bool is_index = false);

  // Put a raw block (maybe compressed) to the corresponding block caches.
  // This method will perform decompression against raw_block if needed and then
  // populate the block caches.
  // On success, common::Status::OK will be returned; also @block will be
  // populated with
  // uncompressed block and its cache handle.
  //
  // REQUIRES: raw_block is heap-allocated. PutDataBlockToCache() will be
  // responsible for releasing its memory if error occurs.
  // @param compression_dict Data for presetting the compression library's
  //    dictionary.
  static common::Status PutDataBlockToCache(
      const common::Slice& block_cache_key,
      const common::Slice& compressed_block_cache_key,
      cache::Cache* block_cache, cache::Cache* block_cache_compressed,
      const common::ReadOptions& read_options,
      const common::ImmutableCFOptions& ioptions, CachableEntry<Block>* block,
      Block* raw_block, uint32_t format_version,
      const common::Slice& compression_dict, size_t read_amp_bytes_per_bit,
      bool is_index = false,
      cache::Cache::Priority pri = cache::Cache::Priority::LOW);

  // Calls (*handle_result)(arg, ...) repeatedly, starting with the entry found
  // after a call to Seek(key), until handle_result returns false.
  // May not make such a call if filter policy says that key is not present.
  friend class TableCache;
  friend class BlockBasedTableBuilder;

  void ReadMeta(const Footer& footer);

  // Create a index reader based on the index type stored in the table.
  // Optionally, user can pass a preloaded meta_index_iter for the index that
  // need to access extra meta blocks for index construction. This parameter
  // helps avoid re-reading meta index block if caller already created one.
  common::Status CreateIndexReader(
      IndexReader** index_reader,
      InternalIterator* preloaded_meta_index_iter = nullptr,
      const int level = -1);

  bool FullFilterKeyMayMatch(const common::ReadOptions& read_options,
                             FilterBlockReader* filter,
                             const common::Slice& user_key,
                             const bool no_io) const;

  // Read the meta block from sst.
  static common::Status ReadMetaBlock(Rep* rep,
                                      std::unique_ptr<Block>* meta_block,
                                      std::unique_ptr<InternalIterator>* iter);

  // Create the filter from the filter block.
  FilterBlockReader* ReadFilter(const BlockHandle& filter_handle,
                                const bool is_a_filter_partition) const;

  static void SetupCacheKeyPrefix(Rep* rep, uint64_t file_size);

  // Generate a cache key prefix from the file
  static void GenerateCachePrefix(cache::Cache* cc,
                                  util::RandomAccessFile* file, char* buffer,
                                  size_t* size);
  static void GenerateCachePrefix(cache::Cache* cc, util::WritableFile* file,
                                  char* buffer, size_t* size);

  // Helper functions for DumpTable()
  common::Status DumpIndexBlock(util::WritableFile* out_file);
  common::Status DumpDataBlocks(util::WritableFile* out_file);
  void DumpKeyValue(const common::Slice& key, const common::Slice& value,
                    util::WritableFile* out_file);

  // No copying allowed
  explicit BlockBasedTable(const TableReader&) = delete;
  void operator=(const TableReader&) = delete;

  friend class PartitionedFilterBlockReader;
  friend class PartitionedFilterBlockTest;
};

// Maitaning state of a two-level iteration on a partitioned index structure
class BlockBasedTable::BlockEntryIteratorState : public TwoLevelIteratorState {
 public:
  BlockEntryIteratorState(BlockBasedTable* table,
                          const common::ReadOptions& read_options,
                          bool skip_filters, bool is_index = false,
                          common::Cleanable* block_cache_cleaner = nullptr);
  InternalIterator* NewSecondaryIterator(
      const common::Slice& index_value, uint64_t* add_blocks = nullptr) override;
  bool PrefixMayMatch(const common::Slice& internal_key) override;

 private:
  // Don't own table_
  BlockBasedTable* table_;
  const common::ReadOptions read_options_;
  bool skip_filters_;
  // true if the 2nd level iterator is on indexes instead of on user data.
  bool is_index_;
  common::Cleanable* block_cache_cleaner_;
  std::set<uint64_t> cleaner_set;
  port::RWMutex cleaner_mu;
};

// CachableEntry represents the entries that *may* be fetched from block cache.
//  field `value` is the item we want to get.
//  field `cache_handle` is the cache handle to the block cache. If the value
//    was not read from cache, `cache_handle` will be nullptr.
template <class TValue>
struct BlockBasedTable::CachableEntry {
  CachableEntry(TValue* _value, cache::Cache::Handle* _cache_handle)
      : value(_value), cache_handle(_cache_handle) {}
  CachableEntry() : CachableEntry(nullptr, nullptr) {}
  void Release(cache::Cache* cache) {
    if (cache_handle) {
      cache->Release(cache_handle);
      value = nullptr;
      cache_handle = nullptr;
    }
  }
  bool IsSet() const { return cache_handle != nullptr; }

  TValue* value = nullptr;
  // if the entry is from the cache, cache_handle will be populated.
  cache::Cache::Handle* cache_handle = nullptr;
};

struct BlockBasedTable::Rep {
  Rep(const common::ImmutableCFOptions& _ioptions,
      const util::EnvOptions& _env_options,
      const BlockBasedTableOptions& _table_opt,
      const db::InternalKeyComparator& _internal_comparator, bool skip_filters)
      : ioptions(_ioptions),
        env_options(_env_options),
        table_options(_table_opt),
        filter_policy(skip_filters ? nullptr : _table_opt.filter_policy.get()),
        internal_comparator(_internal_comparator),
        filter_type(FilterType::kNoFilter),
        whole_key_filtering(_table_opt.whole_key_filtering),
        prefix_filtering(true),
        range_del_handle(BlockHandle::NullBlockHandle()),
        global_seqno(db::kDisableGlobalSequenceNumber) {}

  const common::ImmutableCFOptions& ioptions;
  const util::EnvOptions& env_options;
  const BlockBasedTableOptions& table_options;
  const FilterPolicy* const filter_policy;
  const db::InternalKeyComparator& internal_comparator;
  common::Status status;
  std::unique_ptr<util::RandomAccessFileReader> file;
  char cache_key_prefix[kMaxCacheKeyPrefixSize];
  size_t cache_key_prefix_size = 0;
  char persistent_cache_key_prefix[kMaxCacheKeyPrefixSize];
  size_t persistent_cache_key_prefix_size = 0;
  char compressed_cache_key_prefix[kMaxCacheKeyPrefixSize];
  size_t compressed_cache_key_prefix_size = 0;
  uint64_t dummy_index_reader_offset =
      0;  // ID that is unique for the block cache.
  common::PersistentCacheOptions persistent_cache_options;

  // Footer contains the fixed table information
  Footer footer;
  // index_reader and filter will be populated and used only when
  // options.block_cache is nullptr; otherwise we will get the index block via
  // the block cache.
  std::unique_ptr<IndexReader> index_reader;
  std::unique_ptr<FilterBlockReader> filter;

  enum class FilterType {
    kNoFilter,
    kFullFilter,
    kBlockFilter,
    kPartitionedFilter,
  };
  FilterType filter_type;
  BlockHandle filter_handle;

  std::shared_ptr<const TableProperties> table_properties;
  // Block containing the data for the compression dictionary. We take ownership
  // for the entire block struct, even though we only use its common::Slice
  // member. This
  // is easier because the common::Slice member depends on the continued
  // existence of
  // another member ("allocation").
  std::unique_ptr<const BlockContents> compression_dict_block;
  BlockBasedTableOptions::IndexType index_type;
  bool hash_index_allow_collision;
  bool whole_key_filtering;
  bool prefix_filtering;
  // TODO(kailiu) It is very ugly to use internal key in table, since table
  // module should not be relying on db module. However to make things easier
  // and compatible with existing code, we introduce a wrapper that allows
  // block to extract prefix without knowing if a key is internal or not.
  std::unique_ptr<common::SliceTransform> internal_prefix_transform;

  // only used in level 0 files:
  // when pin_l0_filter_and_index_blocks_in_cache is true, we do use the
  // LRU cache, but we always keep the filter & idndex block's handle checked
  // out here (=we don't call Release()), plus the parsed out objects
  // the LRU cache will never push flush them out, hence they're pinned
  CachableEntry<FilterBlockReader> filter_entry;
  CachableEntry<IndexReader> index_entry;
  // range deletion meta-block is pinned through reader's lifetime when LRU
  // cache is enabled.
  CachableEntry<Block> range_del_entry;
  BlockHandle range_del_handle;

  // If global_seqno is used, all Keys in this file will have the same
  // seqno with value `global_seqno`.
  //
  // A value of kDisableGlobalSequenceNumber means that this feature is disabled
  // and every key have it's own seqno.
  common::SequenceNumber global_seqno;
};

}  // namespace table
}  // namespace xengine
