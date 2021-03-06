// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE

#include <string>

#include "utilities/compaction_filters/remove_emptyvalue_compactionfilter.h"
#include "xengine/slice.h"

using namespace xengine::common;

namespace xengine {
namespace util {

const char* RemoveEmptyValueCompactionFilter::Name() const {
  return "RemoveEmptyValueCompactionFilter";
}

bool RemoveEmptyValueCompactionFilter::Filter(int level, const Slice& key,
                                              const Slice& existing_value,
                                              std::string* new_value,
                                              bool* value_changed) const {
  // remove kv pairs that have empty values
  return existing_value.empty();
}

}  //  namespace util
}  //  namespace xengine
#endif  // !ROCKSDB_LITE
