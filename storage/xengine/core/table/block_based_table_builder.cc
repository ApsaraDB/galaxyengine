// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/block_based_table_builder.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "db/dbformat.h"

#include "xengine/cache.h"
#include "xengine/comparator.h"
#include "xengine/env.h"
#include "xengine/filter_policy.h"
#include "xengine/flush_block_policy.h"
#include "xengine/merge_operator.h"
#include "xengine/table.h"

#include "table/block.h"
#include "table/block_based_filter_block.h"
#include "table/block_based_table_factory.h"
#include "table/block_based_table_reader.h"
#include "table/block_builder.h"
#include "table/filter_block.h"
#include "table/format.h"
#include "table/full_filter_block.h"
#include "table/meta_blocks.h"
#include "table/table_builder.h"

#include "memory/base_malloc.h"
#include "memory/mod_info.h"
#include "util/coding.h"
#include "util/compression.h"
#include "util/crc32c.h"
#include "util/stop_watch.h"
#include "util/string_util.h"
#include "util/xxhash.h"

#include "table/index_builder.h"
#include "table/partitioned_filter_block.h"

using namespace xengine;
using namespace common;
using namespace db;
using namespace util;
using namespace monitor;
using namespace cache;

namespace xengine {
namespace table {

extern const std::string kHashIndexPrefixesBlock;
extern const std::string kHashIndexPrefixesMetadataBlock;

typedef BlockBasedTableOptions::IndexType IndexType;

// Without anonymous namespace here, we fail the warning -Wmissing-prototypes
namespace {

// Create a filter block builder based on its type.
FilterBlockBuilder* CreateFilterBlockBuilder(
    const ImmutableCFOptions& opt, const BlockBasedTableOptions& table_opt,
    PartitionedIndexBuilder* const p_index_builder) {
  if (table_opt.filter_policy == nullptr) return nullptr;

  FilterBitsBuilder* filter_bits_builder =
      table_opt.filter_policy->GetFilterBitsBuilder();
  if (filter_bits_builder == nullptr) {
    return new BlockBasedFilterBlockBuilder(opt.prefix_extractor, table_opt);
  } else {
    if (table_opt.partition_filters) {
      assert(p_index_builder != nullptr);
      return new PartitionedFilterBlockBuilder(
          opt.prefix_extractor, table_opt.whole_key_filtering,
          filter_bits_builder, table_opt.index_block_restart_interval,
          p_index_builder);
    } else {
      return new FullFilterBlockBuilder(opt.prefix_extractor,
                                        table_opt.whole_key_filtering,
                                        filter_bits_builder);
    }
  }
}

bool GoodCompressionRatio(size_t compressed_size, size_t raw_size) {
  // Check to see if compressed less than 12.5%
  return compressed_size < raw_size - (raw_size / 8u);
}

}  // namespace

// format_version is the block format as defined in include/xengine/table.h
Slice CompressBlock(const Slice& raw,
                    const CompressionOptions& compression_options,
                    CompressionType* type, uint32_t format_version,
                    const Slice& compression_dict,
                    std::string* compressed_output) {
  if (*type == kNoCompression) {
    return raw;
  }

  // Will return compressed block contents if (1) the compression method is
  // supported in this platform and (2) the compression rate is "good enough".
  switch (*type) {
    case kSnappyCompression:
      if (Snappy_Compress(compression_options, raw.data(), raw.size(),
                          compressed_output) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;  // fall back to no compression.
    case kZlibCompression:
      if (Zlib_Compress(
              compression_options,
              GetCompressFormatForVersion(kZlibCompression, format_version),
              raw.data(), raw.size(), compressed_output, compression_dict) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;  // fall back to no compression.
    case kBZip2Compression:
      if (BZip2_Compress(
              compression_options,
              GetCompressFormatForVersion(kBZip2Compression, format_version),
              raw.data(), raw.size(), compressed_output) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;  // fall back to no compression.
    case kLZ4Compression:
      if (LZ4_Compress(
              compression_options,
              GetCompressFormatForVersion(kLZ4Compression, format_version),
              raw.data(), raw.size(), compressed_output, compression_dict) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;  // fall back to no compression.
    case kLZ4HCCompression:
      if (LZ4HC_Compress(
              compression_options,
              GetCompressFormatForVersion(kLZ4HCCompression, format_version),
              raw.data(), raw.size(), compressed_output, compression_dict) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;  // fall back to no compression.
    case kXpressCompression:
      if (XPRESS_Compress(raw.data(), raw.size(), compressed_output) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;
    case kZSTD:
    case kZSTDNotFinalCompression:
      if (ZSTD_Compress(compression_options, raw.data(), raw.size(),
                        compressed_output, compression_dict) &&
          GoodCompressionRatio(compressed_output->size(), raw.size())) {
        return *compressed_output;
      }
      break;     // fall back to no compression.
    default: {}  // Do not recognize this compression type
  }

  // Compression method is not supported, or not good compression ratio, so just
  // fall back to uncompressed form.
  *type = kNoCompression;
  return raw;
}

// kBlockBasedTableMagicNumber was picked by running
//    echo rocksdb.table.block_based | sha1sum
// and taking the leading 64 bits.
// Please note that kBlockBasedTableMagicNumber may also be accessed by other
// .cc files
// for that reason we declare it extern in the header but to get the space
// allocated
// it must be not extern in one place.
const uint64_t kBlockBasedTableMagicNumber = 0x88e241b785f4cff7ull;
// We also support reading and writing legacy block based table format (for
// backwards compatibility)
const uint64_t kLegacyBlockBasedTableMagicNumber = 0xdb4775248b80fb57ull;

// A collector that collects properties of interest to block-based table.
// For now this class looks heavy-weight since we only write one additional
// property.
// But in the foreseeable future, we will add more and more properties that are
// specific to block-based table.
class BlockBasedTableBuilder::BlockBasedTablePropertiesCollector
    : public IntTblPropCollector {
 public:
  explicit BlockBasedTablePropertiesCollector(
      BlockBasedTableOptions::IndexType index_type, bool whole_key_filtering,
      bool prefix_filtering)
      : index_type_(index_type),
        whole_key_filtering_(whole_key_filtering),
        prefix_filtering_(prefix_filtering) {}

  virtual Status InternalAdd(const Slice& key, const Slice& value,
                             uint64_t file_size) override {
    // Intentionally left blank. Have no interest in collecting stats for
    // individual key/value pairs.
    return Status::OK();
  }

  virtual Status Finish(UserCollectedProperties* properties) override {
    std::string val;
    PutFixed32(&val, static_cast<uint32_t>(index_type_));
    properties->insert({BlockBasedTablePropertyNames::kIndexType, val});
    properties->insert({BlockBasedTablePropertyNames::kWholeKeyFiltering,
                        whole_key_filtering_ ? kPropTrue : kPropFalse});
    properties->insert({BlockBasedTablePropertyNames::kPrefixFiltering,
                        prefix_filtering_ ? kPropTrue : kPropFalse});
    return Status::OK();
  }

  // The name of the properties collector can be used for debugging purpose.
  virtual const char* Name() const override {
    return "BlockBasedTablePropertiesCollector";
  }

  virtual UserCollectedProperties GetReadableProperties() const override {
    // Intentionally left blank.
    return UserCollectedProperties();
  }

 private:
  BlockBasedTableOptions::IndexType index_type_;
  bool whole_key_filtering_;
  bool prefix_filtering_;
};

struct BlockBasedTableBuilder::Rep {
  const ImmutableCFOptions ioptions;
  const BlockBasedTableOptions table_options;
  const InternalKeyComparator& internal_comparator;
  WritableFileWriter* file;
  uint64_t offset = 0;
  Status status;
  BlockBuilder data_block;
  BlockBuilder range_del_block;

  InternalKeySliceTransform internal_prefix_transform;
  std::unique_ptr<IndexBuilder> index_builder;

  std::string last_key;
  std::string first_key;
  const CompressionType compression_type;
  const CompressionOptions compression_opts;
  // Data for presetting the compression library's dictionary, or nullptr.
  const std::string* compression_dict;
  TableProperties props;

  bool closed = false;  // Either Finish() or Abandon() has been called.
  std::unique_ptr<FilterBlockBuilder> filter_builder;
  char compressed_cache_key_prefix[BlockBasedTable::kMaxCacheKeyPrefixSize];
  size_t compressed_cache_key_prefix_size;

  BlockHandle pending_handle;  // Handle to add to index block

  std::string compressed_output;
  std::unique_ptr<FlushBlockPolicy> flush_block_policy;
  uint32_t column_family_id;
  const std::string& column_family_name;

  std::vector<std::unique_ptr<IntTblPropCollector>> table_properties_collectors;

  Rep(const ImmutableCFOptions& _ioptions,
      const BlockBasedTableOptions& table_opt,
      const InternalKeyComparator& icomparator,
      const std::vector<std::unique_ptr<IntTblPropCollectorFactory>>*
          int_tbl_prop_collector_factories,
      uint32_t _column_family_id, WritableFileWriter* f,
      const CompressionType _compression_type,
      const CompressionOptions& _compression_opts,
      const std::string* _compression_dict, const bool skip_filters,
      const std::string& _column_family_name)
      : ioptions(_ioptions),
        table_options(table_opt),
        internal_comparator(icomparator),
        file(f),
        data_block(table_options.block_restart_interval,
                   table_options.use_delta_encoding),
        range_del_block(1),  // TODO(andrewkr): restart_interval unnecessary
        internal_prefix_transform(_ioptions.prefix_extractor),
        compression_type(_compression_type),
        compression_opts(_compression_opts),
        compression_dict(_compression_dict),
        flush_block_policy(
            table_options.flush_block_policy_factory->NewFlushBlockPolicy(
                table_options, data_block)),
        column_family_id(_column_family_id),
        column_family_name(_column_family_name) {
    PartitionedIndexBuilder* p_index_builder = nullptr;
    if (table_options.index_type ==
        BlockBasedTableOptions::kTwoLevelIndexSearch) {
      p_index_builder = PartitionedIndexBuilder::CreateIndexBuilder(
          &internal_comparator, table_options);
      index_builder.reset(p_index_builder);
    } else {
      index_builder.reset(IndexBuilder::CreateIndexBuilder(
          table_options.index_type, &internal_comparator,
          &this->internal_prefix_transform, table_options));
    }
    if (skip_filters) {
      filter_builder = nullptr;
    } else {
      filter_builder.reset(
          CreateFilterBlockBuilder(_ioptions, table_options, p_index_builder));
    }

    for (auto& collector_factories : *int_tbl_prop_collector_factories) {
      table_properties_collectors.emplace_back(
          collector_factories->CreateIntTblPropCollector(column_family_id));
    }
    table_properties_collectors.emplace_back(
        new BlockBasedTablePropertiesCollector(
            table_options.index_type, table_options.whole_key_filtering,
            _ioptions.prefix_extractor != nullptr));
  }
};

BlockBasedTableBuilder::BlockBasedTableBuilder(
    const ImmutableCFOptions& ioptions,
    const BlockBasedTableOptions& table_options,
    const InternalKeyComparator& internal_comparator,
    const std::vector<std::unique_ptr<IntTblPropCollectorFactory>>*
        int_tbl_prop_collector_factories,
    uint32_t column_family_id, WritableFileWriter* file,
    const CompressionType compression_type,
    const CompressionOptions& compression_opts,
    const std::string* compression_dict, const bool skip_filters,
    const std::string& column_family_name) {
  BlockBasedTableOptions sanitized_table_options(table_options);
  if (sanitized_table_options.format_version == 0 &&
      sanitized_table_options.checksum != kCRC32c) {
    __XENGINE_LOG(INFO, "Silently converting format_version to 1 because checksum is non-default");
    // silently convert format_version to 1 to keep consistent with current
    // behavior
    sanitized_table_options.format_version = 1;
  }

  rep_ = new Rep(ioptions, sanitized_table_options, internal_comparator,
                 int_tbl_prop_collector_factories, column_family_id, file,
                 compression_type, compression_opts, compression_dict,
                 skip_filters, column_family_name);

  if (rep_->filter_builder != nullptr) {
    rep_->filter_builder->StartBlock(0);
  }
  if (table_options.block_cache_compressed.get() != nullptr) {
    BlockBasedTable::GenerateCachePrefix(
        table_options.block_cache_compressed.get(), file->writable_file(),
        &rep_->compressed_cache_key_prefix[0],
        &rep_->compressed_cache_key_prefix_size);
  }
}

BlockBasedTableBuilder::~BlockBasedTableBuilder() {
  assert(rep_->closed);  // Catch errors where caller forgot to call Finish()
  delete rep_;
}

int BlockBasedTableBuilder::Add(const Slice& key, const Slice& value) {
  Rep* r = rep_;
  assert(!r->closed);
  if (!ok()) return status().code();
  ValueType value_type = ExtractValueType(key);
  if (IsValueType(value_type)) {
    if (r->props.num_entries > 0) {
      assert(r->internal_comparator.Compare(key, Slice(r->last_key)) > 0);
    } else if (r->props.num_entries == 0) {
      r->first_key.assign(key.data(), key.size());
    }

    auto should_flush = r->flush_block_policy->Update(key, value);
    if (should_flush) {
      assert(!r->data_block.empty());
      Flush();

      // Add item to index block.
      // We do not emit the index entry for a block until we have seen the
      // first key for the next data block.  This allows us to use shorter
      // keys in the index block.  For example, consider a block boundary
      // between the keys "the quick brown fox" and "the who".  We can use
      // "the r" as the key for the index block entry since it is >= all
      // entries in the first block and < all entries in subsequent
      // blocks.
      if (ok()) {
        r->index_builder->AddIndexEntry(&r->last_key, &key, r->pending_handle);
        r->first_key.assign(key.data(), key.size());
      }
    }

    // Note: PartitionedFilterBlockBuilder requires key being added to filter
    // builder after being added to index builder.
    if (r->filter_builder != nullptr) {
      r->filter_builder->Add(ExtractUserKey(key));
    }

    r->last_key.assign(key.data(), key.size());
    r->data_block.Add(key, value);
    r->props.num_entries++;
    r->props.raw_key_size += key.size();
    r->props.raw_value_size += value.size();

    r->index_builder->OnKeyAdded(key);
    NotifyCollectTableCollectorsOnAdd(key, value, r->offset,
                                      r->table_properties_collectors);

  } else if (value_type == kTypeRangeDeletion) {
    // TODO(wanning&andrewkr) add num_tomestone to table properties
    r->range_del_block.Add(key, value);
    ++r->props.num_entries;
    r->props.raw_key_size += key.size();
    r->props.raw_value_size += value.size();
    NotifyCollectTableCollectorsOnAdd(key, value, r->offset,
                                      r->table_properties_collectors);
  } else {
    assert(false);
  }

  return Status::kOk;
}

void BlockBasedTableBuilder::Flush() {
  Rep* r = rep_;
  assert(!r->closed);
  if (!ok()) return;
  if (r->data_block.empty()) return;
  WriteBlock(&r->data_block, &r->pending_handle, true /* is_data_block */);
  if (r->filter_builder != nullptr) {
    r->filter_builder->StartBlock(r->offset);
  }
  r->props.data_size = r->offset;
  ++r->props.num_data_blocks;
}

void BlockBasedTableBuilder::WriteBlock(BlockBuilder* block,
                                        BlockHandle* handle,
                                        bool is_data_block) {
  WriteBlock(block->Finish(), handle, is_data_block);
  block->Reset();
}

void BlockBasedTableBuilder::WriteBlock(const Slice& raw_block_contents,
                                        BlockHandle* handle,
                                        bool is_data_block) {
  // File format contains a sequence of blocks where each block has:
  //    block_data: uint8[n]
  //    type: uint8
  //    crc: uint32
  assert(ok());
  Rep* r = rep_;

  auto type = r->compression_type;
  Slice block_contents;
  bool abort_compression = false;

  StopWatchNano timer(
      r->ioptions.env,
      ShouldReportDetailedTime(r->ioptions.env, r->ioptions.statistics));

  if (raw_block_contents.size() < kCompressionSizeLimit) {
    Slice compression_dict;
    if (is_data_block && r->compression_dict && r->compression_dict->size()) {
      compression_dict = *r->compression_dict;
    }

    block_contents = CompressBlock(raw_block_contents, r->compression_opts,
                                   &type, r->table_options.format_version,
                                   compression_dict, &r->compressed_output);

    // Some of the compression algorithms are known to be unreliable. If
    // the verify_compression flag is set then try to de-compress the
    // compressed data and compare to the input.
    if (type != kNoCompression && r->table_options.verify_compression) {
      // Retrieve the uncompressed contents into a new buffer
      BlockContents contents;
      Status stat = UncompressBlockContentsForCompressionType(
          block_contents.data(), block_contents.size(), &contents,
          r->table_options.format_version, compression_dict, type);

      if (stat.ok()) {
        bool compressed_ok = contents.data.compare(raw_block_contents) == 0;
        if (!compressed_ok) {
          // The result of the compression was invalid. abort.
          abort_compression = true;
          __XENGINE_LOG(ERROR, "Decompressed block did not match raw block");
          r->status =
              Status::Corruption("Decompressed block did not match raw block");
        }
      } else {
        // Decompression reported an error. abort.
        r->status = Status::Corruption("Could not decompress");
        abort_compression = true;
      }
    }
  } else {
    // Block is too big to be compressed.
    abort_compression = true;
  }

  // Abort compression if the block is too big, or did not pass
  // verification.
  if (abort_compression) {
    type = kNoCompression;
    block_contents = raw_block_contents;
  }

  WriteRawBlock(block_contents, type, handle);
  r->compressed_output.clear();
}

void BlockBasedTableBuilder::WriteRawBlock(const Slice& block_contents,
                                           CompressionType type,
                                           BlockHandle* handle) {
  Rep* r = rep_;  
  handle->set_offset(r->offset);
  handle->set_size(block_contents.size());
  r->status = r->file->Append(block_contents);
  if (r->status.ok()) {
    char trailer[kBlockTrailerSize];
    trailer[0] = type;
    char* trailer_without_type = trailer + 1;
    switch (r->table_options.checksum) {
      case kNoChecksum:
        // we don't support no checksum yet
        assert(false);
      // intentional fallthrough
      case kCRC32c: {
        auto crc = crc32c::Value(block_contents.data(), block_contents.size());
        crc = crc32c::Extend(crc, trailer, 1);  // Extend to cover block type
        EncodeFixed32(trailer_without_type, crc32c::Mask(crc));
        break;
      }
      case kxxHash: {
        void* xxh = XXH32_init(0);
        XXH32_update(xxh, block_contents.data(),
                     static_cast<uint32_t>(block_contents.size()));
        XXH32_update(xxh, trailer, 1);  // Extend  to cover block type
        EncodeFixed32(trailer_without_type, XXH32_digest(xxh));
        break;
      }
    }

    r->status = r->file->Append(Slice(trailer, kBlockTrailerSize));
    if (r->status.ok()) {
      r->status = InsertBlockInCache(block_contents, type, handle);
    }
    if (r->status.ok()) {
      r->offset += block_contents.size() + kBlockTrailerSize;
    }
  }
}

Status BlockBasedTableBuilder::status() const { return rep_->status; }

static void DeleteCachedBlock(const Slice& key, void* value) {
  Block* block = reinterpret_cast<Block*>(value);
  delete block;
}

//
// Make a copy of the block contents and insert into compressed block cache
//
Status BlockBasedTableBuilder::InsertBlockInCache(const Slice& block_contents,
                                                  const CompressionType type,
                                                  const BlockHandle* handle) {
  Rep* r = rep_;

  Cache* block_cache_compressed = r->table_options.block_cache_compressed.get();

  if (type != kNoCompression && block_cache_compressed != nullptr) {
    size_t size = block_contents.size();

    unique_ptr<char[], memory::ptr_delete<char>> ubuf;

    char* buf = static_cast<char*>(memory::base_malloc(size + 1));
    if (nullptr == buf) {
      __XENGINE_LOG(ERROR, "base malloc memory failed, size = %d", size + 1);
      return Status::MemoryLimit();
    } else {
      ubuf.reset(buf);

      memcpy(ubuf.get(), block_contents.data(), size);
      ubuf[size] = type;
      void* ptr = ubuf.get();
      BlockContents results(std::move(ubuf), size, true, type);

      Block* block =
          new Block(std::move(results), kDisableGlobalSequenceNumber);

      // make cache key by appending the file offset to the cache prefix id
      char* end = EncodeVarint64(
          r->compressed_cache_key_prefix + r->compressed_cache_key_prefix_size,
          handle->offset());
      Slice key(r->compressed_cache_key_prefix,
                static_cast<size_t>(end - r->compressed_cache_key_prefix));

      // Insert into compressed block cache.
      block_cache_compressed->Insert(key, block, block->usable_size(),
                                     &DeleteCachedBlock, nullptr,
                                     Cache::Priority::LOW, ptr);

      // Invalidate OS cache.
      r->file->InvalidateCache(static_cast<size_t>(r->offset), size);
    }
  }
  return Status::OK();
}

int BlockBasedTableBuilder::Finish() {
  Rep* r = rep_;
  bool empty_data_block = r->data_block.empty();
  Flush();
  assert(!r->closed);
  r->closed = true;

  // To make sure properties block is able to keep the accurate size of index
  // block, we will finish writing all index entries here and flush them
  // to storage after metaindex block is written.
  if (ok() && !empty_data_block) {
    r->index_builder->AddIndexEntry(
        &r->last_key, nullptr /* no next data block */, r->pending_handle);
  }

  r->first_key.clear();
  BlockHandle filter_block_handle, metaindex_block_handle, index_block_handle,
      compression_dict_block_handle, range_del_block_handle;
  // Write filter block
  if (ok() && r->filter_builder != nullptr) {
    Status s = Status::Incomplete();
    while (s.IsIncomplete()) {
      Slice filter_content = r->filter_builder->Finish(filter_block_handle, &s);
      assert(s.ok() || s.IsIncomplete());
      r->props.filter_size += filter_content.size();
      WriteRawBlock(filter_content, kNoCompression, &filter_block_handle);
    }
  }

  IndexBuilder::IndexBlocks index_blocks;
  auto index_builder_status = r->index_builder->Finish(&index_blocks);
  if (index_builder_status.IsIncomplete()) {
    // We we have more than one index partition then meta_blocks are not
    // supported for the index. Currently meta_blocks are used only by
    // HashIndexBuilder which is not multi-partition.
    assert(index_blocks.meta_blocks.empty());
  } else if (!index_builder_status.ok()) {
    return index_builder_status.code();
  }

  // Write meta blocks and metaindex block with the following order.
  //    1. [meta block: filter]
  //    2. [meta block: properties]
  //    3. [meta block: compression dictionary]
  //    4. [meta block: range deletion tombstone]
  //    5. [metaindex block]
  // write meta blocks
  MetaIndexBuilder meta_index_builder;
  for (const auto& item : index_blocks.meta_blocks) {
    BlockHandle block_handle;
    WriteBlock(item.second, &block_handle, false /* is_data_block */);
    meta_index_builder.Add(item.first, block_handle);
  }

  if (ok()) {
    if (r->filter_builder != nullptr) {
      // Add mapping from "<filter_block_prefix>.Name" to location
      // of filter data.
      std::string key;
      if (r->filter_builder->IsBlockBased()) {
        key = BlockBasedTable::kFilterBlockPrefix;
      } else {
        key = r->table_options.partition_filters
                  ? BlockBasedTable::kPartitionedFilterBlockPrefix
                  : BlockBasedTable::kFullFilterBlockPrefix;
      }
      key.append(r->table_options.filter_policy->Name());
      meta_index_builder.Add(key, filter_block_handle);
    }

    // Write properties and compression dictionary blocks.
    {
      PropertyBlockBuilder property_block_builder;
      r->props.column_family_id = r->column_family_id;
      r->props.column_family_name = r->column_family_name;
      r->props.filter_policy_name = r->table_options.filter_policy != nullptr
                                        ? r->table_options.filter_policy->Name()
                                        : "";
      r->props.index_size =
          r->index_builder->EstimatedSize() + kBlockTrailerSize;
      r->props.comparator_name = r->ioptions.user_comparator != nullptr
                                     ? r->ioptions.user_comparator->Name()
                                     : "nullptr";
      r->props.merge_operator_name = r->ioptions.merge_operator != nullptr
                                         ? r->ioptions.merge_operator->Name()
                                         : "nullptr";
      r->props.compression_name = CompressionTypeToString(r->compression_type);
      r->props.prefix_extractor_name =
          r->ioptions.prefix_extractor != nullptr
              ? r->ioptions.prefix_extractor->Name()
              : "nullptr";

      std::string property_collectors_names = "[";
      property_collectors_names = "[";
      for (size_t i = 0;
           i < r->ioptions.table_properties_collector_factories.size(); ++i) {
        if (i != 0) {
          property_collectors_names += ",";
        }
        property_collectors_names +=
            r->ioptions.table_properties_collector_factories[i]->Name();
      }
      property_collectors_names += "]";
      r->props.property_collectors_names = property_collectors_names;

      // Add basic properties
      property_block_builder.AddTableProperty(r->props);

      // Add use collected properties
      NotifyCollectTableCollectorsOnFinish(r->table_properties_collectors,
                                           &property_block_builder);

      BlockHandle properties_block_handle;
      WriteRawBlock(property_block_builder.Finish(), kNoCompression,
                    &properties_block_handle);
      meta_index_builder.Add(kPropertiesBlock, properties_block_handle);

      // Write compression dictionary block
      if (r->compression_dict && r->compression_dict->size()) {
        WriteRawBlock(*r->compression_dict, kNoCompression,
                      &compression_dict_block_handle);
        meta_index_builder.Add(kCompressionDictBlock,
                               compression_dict_block_handle);
      }
    }  // end of properties/compression dictionary block writing

    if (ok() && !r->range_del_block.empty()) {
      WriteRawBlock(r->range_del_block.Finish(), kNoCompression,
                    &range_del_block_handle);
      meta_index_builder.Add(kRangeDelBlock, range_del_block_handle);
    }  // range deletion tombstone meta block
  }    // meta blocks

  // Write index block
  if (ok()) {
    // flush the meta index block
    WriteRawBlock(meta_index_builder.Finish(), kNoCompression,
                  &metaindex_block_handle);

    const bool is_data_block = true;
    WriteBlock(index_blocks.index_block_contents, &index_block_handle,
               !is_data_block);
    // If there are more index partitions, finish them and write them out
    Status& s = index_builder_status;
    while (s.IsIncomplete()) {
      s = r->index_builder->Finish(&index_blocks, index_block_handle);
      if (!s.ok() && !s.IsIncomplete()) {
        return s.code();
      }
      WriteBlock(index_blocks.index_block_contents, &index_block_handle,
                 !is_data_block);
      // The last index_block_handle will be for the partition index block
    }
  }

  // Write footer
  if (ok()) {
    // No need to write out new footer if we're using default checksum.
    // We're writing legacy magic number because we want old versions of RocksDB
    // be able to read files generated with new release (just in case if
    // somebody wants to roll back after an upgrade)
    // TODO(icanadi) at some point in the future, when we're absolutely sure
    // nobody will roll back to RocksDB 2.x versions, retire the legacy magic
    // number and always write new table files with new magic number
    bool legacy = (r->table_options.format_version == 0);
    // this is guaranteed by BlockBasedTableBuilder's constructor
    assert(r->table_options.checksum == kCRC32c ||
           r->table_options.format_version != 0);
    Footer footer(legacy ? kLegacyBlockBasedTableMagicNumber
                         : kBlockBasedTableMagicNumber,
                  r->table_options.format_version);
    footer.set_metaindex_handle(metaindex_block_handle);
    footer.set_index_handle(index_block_handle);
    footer.set_checksum(r->table_options.checksum);
    std::string footer_encoding;
    footer.EncodeTo(&footer_encoding);
    r->status = r->file->Append(footer_encoding);
    if (r->status.ok()) {
      r->offset += footer_encoding.size();
    }
  }

  return r->status.code();
}

int BlockBasedTableBuilder::Abandon() {
  Rep* r = rep_;
  assert(!r->closed);
  r->closed = true;
  return common::Status::kOk;
}

uint64_t BlockBasedTableBuilder::NumEntries() const {
  return rep_->props.num_entries;
}

uint64_t BlockBasedTableBuilder::FileSize() const { return rep_->offset; }

bool BlockBasedTableBuilder::NeedCompact() const {
  for (const auto& collector : rep_->table_properties_collectors) {
    if (collector->NeedCompact()) {
      return true;
    }
  }
  return false;
}

TableProperties BlockBasedTableBuilder::GetTableProperties() const {
  TableProperties ret = rep_->props;
  for (const auto& collector : rep_->table_properties_collectors) {
    for (const auto& prop : collector->GetReadableProperties()) {
      ret.readable_properties.insert(prop);
    }
    collector->Finish(&ret.user_collected_properties);
  }
  return ret;
}

const std::string BlockBasedTable::kFilterBlockPrefix = "filter.";
const std::string BlockBasedTable::kFullFilterBlockPrefix = "fullfilter.";
const std::string BlockBasedTable::kPartitionedFilterBlockPrefix =
    "partitionedfilter.";
}  // namespace table
}  // namespace xengine
