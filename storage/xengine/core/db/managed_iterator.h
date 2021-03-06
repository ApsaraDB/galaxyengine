// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
#pragma once

#ifndef ROCKSDB_LITE

#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "db/column_family.h"
#include "util/arena.h"
#include "xengine/db.h"
#include "xengine/iterator.h"
#include "xengine/options.h"

namespace xengine {
namespace db {

class DBImpl;
struct SuperVersion;
class ColumnFamilyData;

/**
 * ManagedIterator is a special type of iterator that supports freeing the
 * underlying iterator and still being able to access the current key/value
 * pair.  This is done by copying the key/value pair so that clients can
 * continue to access the data without getting a SIGSEGV.
 * The underlying iterator can be freed manually through the  call to
 * ReleaseIter or automatically (as needed on space pressure or age.)
 * The iterator is recreated using the saved original arguments.
 */
class ManagedIterator : public Iterator {
 public:
  ManagedIterator(DBImpl* db, const common::ReadOptions& read_options,
                  ColumnFamilyData* cfd);
  virtual ~ManagedIterator();

  virtual void SeekToLast() override;
  virtual void Prev() override;
  virtual bool Valid() const override;
  void SeekToFirst() override;
  virtual void Seek(const common::Slice& target) override;
  virtual void SeekForPrev(const common::Slice& target) override;
  virtual void Next() override;
  virtual common::Slice key() const override;
  virtual common::Slice value() const override;
  virtual common::Status status() const override;
  void ReleaseIter(bool only_old);
  void SetDropOld(bool only_old) {
    only_drop_old_ = read_options_.tailing || only_old;
  }

 private:
  void RebuildIterator();
  void UpdateCurrent();
  void SeekInternal(const common::Slice& user_key, bool seek_to_first);
  bool NeedToRebuild();
  void Lock();
  bool TryLock();
  void UnLock();
  DBImpl* const db_;
  common::ReadOptions read_options_;
  ColumnFamilyData* const cfd_;
  ColumnFamilyHandleInternal cfh_;

  uint64_t svnum_;
  std::unique_ptr<Iterator> mutable_iter_;
  // internal iterator status
  common::Status status_;
  bool valid_;

  IterKey cached_key_;
  IterKey cached_value_;

  bool only_drop_old_ = true;
  bool snapshot_created_;
  bool release_supported_;
  std::mutex in_use_;  // is managed iterator in use
};
}
}  // namespace xengine
#endif  // !ROCKSDB_LITE
