// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "xengine/write_batch_base.h"

#include <string>

#include "xengine/slice.h"
#include "xengine/status.h"

using namespace xengine;
using namespace common;

namespace xengine {
namespace db {

// Simple implementation of SlicePart variants of Put().  Child classes
// can override these method with more performant solutions if they choose.
Status WriteBatchBase::Put(ColumnFamilyHandle* column_family,
                           const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  return Put(column_family, key_slice, value_slice);
}

Status WriteBatchBase::Put(const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  return Put(key_slice, value_slice);
}

Status WriteBatchBase::Delete(ColumnFamilyHandle* column_family,
                              const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  return Delete(column_family, key_slice);
}

Status WriteBatchBase::Delete(const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  return Delete(key_slice);
}

Status WriteBatchBase::SingleDelete(ColumnFamilyHandle* column_family,
                                    const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  return SingleDelete(column_family, key_slice);
}

Status WriteBatchBase::SingleDelete(const SliceParts& key) {
  std::string key_buf;
  Slice key_slice(key, &key_buf);
  return SingleDelete(key_slice);
}

Status WriteBatchBase::DeleteRange(ColumnFamilyHandle* column_family,
                                   const SliceParts& begin_key,
                                   const SliceParts& end_key) {
  std::string begin_key_buf, end_key_buf;
  Slice begin_key_slice(begin_key, &begin_key_buf);
  Slice end_key_slice(end_key, &end_key_buf);
  return DeleteRange(column_family, begin_key_slice, end_key_slice);
}

Status WriteBatchBase::DeleteRange(const SliceParts& begin_key,
                                   const SliceParts& end_key) {
  std::string begin_key_buf, end_key_buf;
  Slice begin_key_slice(begin_key, &begin_key_buf);
  Slice end_key_slice(end_key, &end_key_buf);
  return DeleteRange(begin_key_slice, end_key_slice);
}

Status WriteBatchBase::Merge(ColumnFamilyHandle* column_family,
                             const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  return Merge(column_family, key_slice, value_slice);
}

Status WriteBatchBase::Merge(const SliceParts& key, const SliceParts& value) {
  std::string key_buf, value_buf;
  Slice key_slice(key, &key_buf);
  Slice value_slice(value, &value_buf);

  return Merge(key_slice, value_slice);
}
}  // namespace db
}  // namespace xengine
