// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2012 Facebook.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROCKSDB_LITE

#include "xengine/convenience.h"

#include "db/db_impl.h"

using namespace xengine;
using namespace db;

namespace xengine {
namespace common {

void CancelAllBackgroundWork(DB* db, bool wait) {
  (dynamic_cast<DBImpl*>(db))->CancelAllBackgroundWork(wait);
}

Status DeleteFilesInRange(DB* db, ColumnFamilyHandle* column_family,
                          const Slice* begin, const Slice* end) {
  return (dynamic_cast<DBImpl*>(db))
      ->DeleteFilesInRange(column_family, begin, end);
}

}  // namespace common
}  // namespace xengine

#endif  // ROCKSDB_LITE
