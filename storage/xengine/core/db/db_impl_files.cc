// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include "db/db_impl.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#include "db/event_helpers.h"
#include "util/file_util.h"
#include "util/sst_file_manager_impl.h"

using namespace xengine;
using namespace common;
using namespace util;
using namespace monitor;

namespace xengine {
namespace db {
uint64_t DBImpl::FindMinPrepLogReferencedByMemTable() {
  if (!allow_2pc()) {
    return 0;
  }

  uint64_t min_log = 0;

  // we must look through the memtables for two phase transactions
  // that have been committed but not yet flushed
  int ret = Status::kOk;
  int tmp_ret = Status::kOk;

  GlobalContext *global_ctx = nullptr;
  AllSubTable *all_sub_table = nullptr;
  if (nullptr == (global_ctx = versions_->get_global_ctx())) {
    ret = Status::kErrorUnexpected;
    XENGINE_LOG(WARN, "unexpected error, global ctx must not nullptr", K(ret));
  } else if (FAILED(global_ctx->acquire_thread_local_all_sub_table(all_sub_table))) {
    XENGINE_LOG(WARN, "fail to acquire all sub table", K(ret));
  } else if (nullptr == all_sub_table) {
    ret = Status::kErrorUnexpected;
    XENGINE_LOG(WARN, "unexpected error, all sub table must not nullptr", K(ret));
  } else {
    SubTableMap &sub_tables = all_sub_table->sub_table_map_;
    for (auto iter  = sub_tables.begin(); iter != sub_tables.end(); ++iter) {
      if (iter->second->IsDropped()) {
        continue;
      }

      auto log = iter->second->imm()->GetMinLogContainingPrepSection();

      if (log > 0 && (min_log == 0 || log < min_log)) {
        min_log = log;
      }

      log = std::min(iter->second->mem()->GetMinLogContainingPrepSection(), iter->second->mem()->get_temp_min_prep_log());
      if (log > 0 && (min_log == 0 || log < min_log)) {
        min_log = log;
      }
    }
  }

  //there will cover the error code, by design 
  tmp_ret = ret;
  if (nullptr != global_ctx && FAILED(global_ctx->release_thread_local_all_sub_table(all_sub_table))) {
    XENGINE_LOG(WARN, "fail to release all sub table", K(ret), K(tmp_ret));
  }

  return min_log;
}

bool DBImpl::insert_prepare_sequence_into_xid_map(const std::string& xid,
                                                  common::SequenceNumber seq) {
  return xid_map_.emplace(xid, seq).second;
}

bool DBImpl::get_prepare_sequence_from_xid_map(const std::string& xid,
                                               common::SequenceNumber *seq) {
  auto it = xid_map_.find(xid);
  if (it == xid_map_.end()) {
    return false;
  } else {
    *seq = it->second;
    xid_map_.erase(it);
    return true;
  }
}

DBImpl::RecoveredTransaction::~RecoveredTransaction() {
  MOD_DELETE_OBJECT(WriteBatch, batch_);
}

bool DBImpl::insert_recovered_transaction(const std::string& name,
                                          RecoveredTransaction *transaction,
                                          bool mark_log) {
  assert(transaction);
  bool ret = recovered_transactions_.insert(name, transaction);
  if (ret && mark_log) {
    MarkLogAsContainingPrepSection(transaction->prepare_log_num_);
  }
  return ret;
}

bool DBImpl::delete_recovered_transaction(const std::string& name, bool unmark_log) {
  auto it = recovered_transactions_.find(name);
  assert(it != recovered_transactions_.end());
  auto* trx = it->second;
  bool ret = recovered_transactions_.erase(name);
  if (ret && unmark_log) {
    MarkLogAsHavingPrepSectionFlushed(trx->prepare_log_num_);
  }
  if (ret) {
    MOD_DELETE_OBJECT(RecoveredTransaction, trx);
  }
  return ret;
}

void DBImpl::delete_all_recovered_transactions() {
  for(auto entry : recovered_transactions_) {
    MOD_DELETE_OBJECT(RecoveredTransaction, entry.second);
  }
  recovered_transactions_.clear();
}

DBImpl::RecoveredTransaction* DBImpl::get_recovered_transaction(const std::string& name) {
  auto it = recovered_transactions_.find(name);
  if (it == recovered_transactions_.end()) {
    return nullptr;
  } else {
    return it->second;
  }
}

void DBImpl::MarkLogAsHavingPrepSectionFlushed(uint64_t log) {
  assert(log != 0);
  std::lock_guard<std::mutex> lock(not_commited_mutex_);
  auto it = not_commited_section_.find(log);
  assert(it != not_commited_section_.end());
  assert(it->second > 0);
  it->second -= 1;
  if (0 == it->second) {
    not_commited_section_.erase(it);
  }
}

void DBImpl::MarkLogAsContainingPrepSection(uint64_t log) {
  assert(log != 0);
  std::lock_guard<std::mutex> lock(not_commited_mutex_);
  not_commited_section_[log] += 1;
}

uint64_t DBImpl::FindMinLogContainingOutstandingPrep() {
  if (!allow_2pc()) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(not_commited_mutex_);
  return not_commited_section_.empty() ? 0 :
         not_commited_section_.begin()->first;
}

bool DBImpl::in_prep_log_ref_map(uint64_t log_number) {
  if (!allow_2pc()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(not_commited_mutex_);
  auto it = not_commited_section_.find(log_number);
  return it != not_commited_section_.end();
}

uint64_t DBImpl::MinLogNumberToKeep() {
  uint64_t log_number = versions_->MinLogNumber();

  if (allow_2pc()) {
    // if are 2pc we must consider logs containing prepared
    // sections of outstanding transactions.
    //
    // We must check min logs with outstanding prep before we check
    // logs referneces by memtables because a log referenced by the
    // first data structure could transition to the second under us.
    //
    // TODO(horuff): iterating over all column families under db mutex.
    // should find more optimial solution
    auto min_log_in_prep_heap = FindMinLogContainingOutstandingPrep();

    if (min_log_in_prep_heap != 0 && min_log_in_prep_heap < log_number) {
      log_number = min_log_in_prep_heap;
    }

    auto min_log_refed_by_mem = FindMinPrepLogReferencedByMemTable();

    if (min_log_refed_by_mem != 0 && min_log_refed_by_mem < log_number) {
      log_number = min_log_refed_by_mem;
    }
    XENGINE_LOG(INFO, "CK_INFO: min log refed by mem", K(min_log_refed_by_mem), K(min_log_in_prep_heap), K(log_number));
  } else {
    XENGINE_LOG(INFO, "CK_INFO: log number", K(log_number));
  }
  return log_number;
}

// * Returns the list of live files in 'sst_live'
// If it's doing full scan:
// * Returns the list of all files in the filesystem in
// 'full_scan_candidate_files'.
// Otherwise, gets obsolete files from VersionSet.
// no_full_scan = true -- never do the full scan using GetChildren()
// force = false -- don't force the full scan, except every
//  mutable_db_options_.delete_obsolete_files_period_micros
// force = true -- force the full scan
void DBImpl::FindObsoleteFiles(JobContext* job_context, bool force,
                               bool no_full_scan) {
  mutex_.AssertHeld();

  // if deletion is disabled, do nothing
  if (disable_delete_obsolete_files_ > 0) {
    return;
  }

  bool doing_the_full_scan = false;

  // logic for figurint out if we're doing the full scan
  if (no_full_scan) {
    doing_the_full_scan = false;
  } else if (force ||
             mutable_db_options_.delete_obsolete_files_period_micros == 0) {
    doing_the_full_scan = true;
  } else {
    const uint64_t now_micros = env_->NowMicros();
    if ((delete_obsolete_files_last_run_ +
         mutable_db_options_.delete_obsolete_files_period_micros) <
        now_micros) {
      doing_the_full_scan = true;
      delete_obsolete_files_last_run_ = now_micros;
    }
  }

  // don't delete files that might be currently written to from compaction
  // threads
  // Since job_context->min_pending_output is set, until file scan finishes,
  // mutex_ cannot be released. Otherwise, we might see no min_pending_output
  // here but later find newer generated unfinalized files while scannint.
  if (!pending_outputs_.empty()) {
    job_context->min_pending_output = *pending_outputs_.begin();
  } else {
    // delete all of them
    job_context->min_pending_output = std::numeric_limits<uint64_t>::max();
  }

  // Get obsolete files.  This function will also update the list of
  // pending files in VersionSet().
  versions_->GetObsoleteFiles(&job_context->sst_delete_files,
                              &job_context->manifest_delete_files,
                              job_context->min_pending_output);

  // store the current filenum, lognum, etc
  //job_context->manifest_file_number = versions_->purge_manifest_file_number();
  //job_context->pending_manifest_file_number =
  //    versions_->pending_manifest_file_number();
  job_context->log_number = MinLogNumberToKeep();

  //job_context->prev_log_number = versions_->prev_log_number();
  //job_context->checkpoint_file_number_ = versions_->purge_checkpoint_file_number(); 

  if (doing_the_full_scan) {
    for (size_t path_id = 0; path_id < immutable_db_options_.db_paths.size();
         path_id++) {
      // set of all files in the directory. We'll exclude files that are still
      // alive in the subsequent processings.
      std::vector<std::string> files;
      env_->GetChildren(immutable_db_options_.db_paths[path_id].path,
                        &files);  // Ignore errors
      for (std::string file : files) {
        // TODO(icanadi) clean up this mess to avoid having one-off "/" prefixes
        job_context->full_scan_candidate_files.emplace_back(
            "/" + file, static_cast<uint32_t>(path_id));
      }
    }

    // Add log files in wal_dir
    if (immutable_db_options_.wal_dir != dbname_) {
      std::vector<std::string> log_files;
      env_->GetChildren(immutable_db_options_.wal_dir,
                        &log_files);  // Ignore errors
      for (std::string log_file : log_files) {
        job_context->full_scan_candidate_files.emplace_back(log_file, 0);
      }
    }
    // Add info log files in db_log_dir
    if (!immutable_db_options_.db_log_dir.empty() &&
        immutable_db_options_.db_log_dir != dbname_) {
      std::vector<std::string> info_log_files;
      // Ignore errors
      env_->GetChildren(immutable_db_options_.db_log_dir, &info_log_files);
      for (std::string log_file : info_log_files) {
        job_context->full_scan_candidate_files.emplace_back(log_file, 0);
      }
    }
  }

  // logs_ is empty when called during recovery, in which case there can't yet
  // be any tracked obsolete logs
  if (!alive_log_files_.empty() && !logs_.empty()) {
    uint64_t min_log_number = job_context->log_number;
    size_t num_alive_log_files = alive_log_files_.size();
    // find newly obsoleted log files
    while (!alive_log_files_.empty() && alive_log_files_.begin()->number < min_log_number) {
      auto& earliest = *alive_log_files_.begin();
      if (immutable_db_options_.recycle_log_file_num >
          log_recycle_files.size()) {
        __XENGINE_LOG(INFO, "adding log %" PRIu64 " to recycle list\n", earliest.number);
        log_recycle_files.push_back(earliest.number);
      } else {
        job_context->log_delete_files.push_back(earliest.number);
      }
      if (job_context->size_log_to_delete == 0) {
        job_context->prev_total_log_size = total_log_size_;
        job_context->num_alive_log_files = num_alive_log_files;
      }
      job_context->size_log_to_delete += earliest.size;
      total_log_size_ -= earliest.size;
      alive_log_files_.pop_front();
      // Current log should always stay alive since it can't have
      // number < MinLogNumber().
      assert(alive_log_files_.size());
    }
    while (!logs_.empty() && logs_.front().number < min_log_number) {
      auto& log = logs_.front();
      if (log.getting_synced) {
        log_sync_cv_.Wait();
        // logs_ could have changed while we were waiting.
        continue;
      }
      logs_to_free_.push_back(log.ReleaseWriter());
      logs_.pop_front();
    }
    // Current log cannot be obsolete.
    assert(!logs_.empty());
  }

  // We're just cleaning up for DB::Write().
  assert(job_context->logs_to_free.empty());
  job_context->logs_to_free = logs_to_free_;
  job_context->log_recycle_files.assign(log_recycle_files.begin(),
                                        log_recycle_files.end());
  logs_to_free_.clear();
}

namespace {
bool CompareCandidateFile(const JobContext::CandidateFileInfo& first,
                          const JobContext::CandidateFileInfo& second) {
  if (first.file_name > second.file_name) {
    return true;
  } else if (first.file_name < second.file_name) {
    return false;
  } else {
    return (first.path_id > second.path_id);
  }
}
};  // namespace

// Delete obsolete files and log status and information of file deletion
void DBImpl::DeleteObsoleteFileImpl(Status file_deletion_status, int job_id,
                                    const std::string& fname, FileType type,
                                    uint64_t number, uint32_t path_id) {
  if (type == kTableFile) {
    file_deletion_status =
        DeleteSSTFile(&immutable_db_options_, fname, path_id);
  } else {
    file_deletion_status = env_->DeleteFile(fname);
  }
  if (file_deletion_status.ok()) {
    XENGINE_LOG(INFO, "success to delete wal file", K(fname), K(number), K((int32_t)(type)), K(file_deletion_status.ToString()));
  } else if (env_->FileExists(fname).IsNotFound()) {
    __XENGINE_LOG(INFO, "[JOB %d] Tried to delete a non-existing file %s type=%d #%" PRIu64 " -- %s\n",
        job_id, fname.c_str(), type, number, file_deletion_status.ToString().c_str());
  } else {
    __XENGINE_LOG(ERROR, "[JOB %d] Failed to delete %s type=%d #%" PRIu64 " -- %s\n",
        job_id, fname.c_str(), type, number, file_deletion_status.ToString().c_str());
  }
  if (type == kTableFile) {
    EventHelpers::LogAndNotifyTableFileDeletion(
        job_id, number, fname, file_deletion_status, GetName(),
        immutable_db_options_.listeners);
  }
}

// Diffs the files listed in filenames and those that do not
// belong to live files are posibly removed. Also, removes all the
// files in sst_delete_files and log_delete_files.
// It is not necessary to hold the mutex when invoking this method.
void DBImpl::PurgeObsoleteFiles(const JobContext& state, bool schedule_only) {
  // we'd better have sth to delete
  assert(state.HaveSomethingToDelete());

  //manifest file not managed by there now

  // this checks if FindObsoleteFiles() was run before. If not, don't do
  // PurgeObsoleteFiles(). If FindObsoleteFiles() was run, we need to also
  // run PurgeObsoleteFiles(), even if disable_delete_obsolete_files_ is true
//  if (state.manifest_file_number == 0) {
//    return;
//  }

  // Now, convert live list to an unordered map, WITHOUT mutex held;
  // set is slow.
  std::unordered_map<uint64_t, const FileDescriptor*> sst_live_map;
  for (const FileDescriptor& fd : state.sst_live) {
    sst_live_map[fd.GetNumber()] = &fd;
  }
  std::unordered_set<uint64_t> log_recycle_files_set(
      state.log_recycle_files.begin(), state.log_recycle_files.end());

  auto candidate_files = state.full_scan_candidate_files;
  candidate_files.reserve(
      candidate_files.size() + state.sst_delete_files.size() +
      state.log_delete_files.size() + state.manifest_delete_files.size());
  // We may ignore the dbname when generating the file names.
  const char* kDumbDbName = "";
  for (auto file : state.sst_delete_files) {
    candidate_files.emplace_back(
        MakeTableFileName(kDumbDbName, file->fd.GetNumber()),
        file->fd.GetPathId());
    delete file;
  }

  for (auto file_num : state.log_delete_files) {
    if (file_num > 0) {
      candidate_files.emplace_back(LogFileName(kDumbDbName, file_num), 0);
    }
  }
  for (const auto& filename : state.manifest_delete_files) {
    candidate_files.emplace_back(filename, 0);
  }

  // dedup state.candidate_files so we don't try to delete the same
  // file twice
  std::sort(candidate_files.begin(), candidate_files.end(),
            CompareCandidateFile);
  candidate_files.erase(
      std::unique(candidate_files.begin(), candidate_files.end()),
      candidate_files.end());

  if (state.prev_total_log_size > 0) {
    __XENGINE_LOG(INFO,"[JOB %d] Try to delete WAL files size %" PRIu64 ", prev total WAL file size %" PRIu64
        ", number of live WAL files %" ROCKSDB_PRIszt ".\n",
        state.job_id, state.size_log_to_delete,
        state.prev_total_log_size, state.num_alive_log_files);
  }

  std::vector<std::string> old_info_log_files;
  InfoLogPrefix info_log_prefix(!immutable_db_options_.db_log_dir.empty(),
                                dbname_);
  for (const auto& candidate_file : candidate_files) {
    std::string to_delete = candidate_file.file_name;
    uint32_t path_id = candidate_file.path_id;
    uint64_t number;
    FileType type;
    // Ignore file if we cannot recognize it.
    if (!ParseFileName(to_delete, &number, info_log_prefix.prefix, &type)) {
      continue;
    }

    bool keep = true;
    switch (type) {
      case kLogFile:
        keep = ((number >= state.log_number) ||
                (number == state.prev_log_number) ||
                (log_recycle_files_set.find(number) !=
                 log_recycle_files_set.end()));
        break;
      case kDescriptorFile:
        // Keep my manifest file, and any newer incarnations'
        // (can happen during manifest roll)
        //keep = (number >= state.manifest_file_number);
        keep = true; // for problem check
        break;
      case kCheckpointFile:
        //keep = (number >= state.checkpoint_file_number_);
        keep = true; // for problem check
        break;
      case kTempFile:
        // Any temp files that are currently being written to must
        // be recorded in pending_outputs_, which is inserted into "live".
        // Also, SetCurrentFile creates a temp file when writing out new
        // manifest, which is equal to state.pending_manifest_file_number. We
        // should not delete that file
        //
        // TODO(yhchiang): carefully modify the third condition to safely
        //                 remove the temp options files.
        keep = (sst_live_map.find(number) != sst_live_map.end()) ||
               (number == state.pending_manifest_file_number) ||
               (to_delete.find(kOptionsFileNamePrefix) != std::string::npos);
        break;
      case kInfoLogFile:
        keep = true;
        if (number != 0) {
          old_info_log_files.push_back(to_delete);
        }
        break;
      case kCurrentFile:
      case kDBLockFile:
      case kIdentityFile:
      case kMetaDatabase:
      case kOptionsFile:
      case kBlobFile:
      case kTableFile:  // extent space file don't purge
      case kCurrentCheckpointFile:
        keep = true;
        break;
    }

    if (keep) {
      continue;
    }

    std::string fname;
    if (type == kTableFile) {
      // evict from cache
      TableCache::Evict(table_cache_.get(), number);
      fname = TableFileName(immutable_db_options_.db_paths, number, path_id);
    } else {
      fname = ((type == kLogFile) ? immutable_db_options_.wal_dir : dbname_) +
              "/" + to_delete;
    }

#ifndef ROCKSDB_LITE
    if (type == kLogFile && (immutable_db_options_.wal_ttl_seconds > 0 ||
                             immutable_db_options_.wal_size_limit_mb > 0)) {
      wal_manager_.ArchiveWALFile(fname, number);
      continue;
    }
#endif  // !ROCKSDB_LITE

    Status file_deletion_status;
    if (schedule_only) {
      InstrumentedMutexLock guard_lock(&mutex_);
      SchedulePendingPurge(fname, type, number, path_id, state.job_id);
    } else {
      DeleteObsoleteFileImpl(file_deletion_status, state.job_id, fname, type,
                             number, path_id);
    }
  }

  // Delete old info log files.
  size_t old_info_log_file_count = old_info_log_files.size();
  if (old_info_log_file_count != 0 &&
      old_info_log_file_count >= immutable_db_options_.keep_log_file_num) {
    std::sort(old_info_log_files.begin(), old_info_log_files.end());
    size_t end =
        old_info_log_file_count - immutable_db_options_.keep_log_file_num;
    for (unsigned int i = 0; i <= end; i++) {
      std::string& to_delete = old_info_log_files.at(i);
      std::string full_path_to_delete =
          (immutable_db_options_.db_log_dir.empty()
               ? dbname_
               : immutable_db_options_.db_log_dir) +
          "/" + to_delete;
      __XENGINE_LOG(INFO, "[JOB %d] Delete info log file %s\n", state.job_id, full_path_to_delete.c_str());
      Status s = env_->DeleteFile(full_path_to_delete);
      if (!s.ok()) {
        if (env_->FileExists(full_path_to_delete).IsNotFound()) {
          __XENGINE_LOG(INFO, "[JOB %d] Tried to delete non-existing info log file %s FAILED " "-- %s\n",
              state.job_id, to_delete.c_str(), s.ToString().c_str());
        } else {
          __XENGINE_LOG(ERROR, "[JOB %d] Delete info log file %s FAILED -- %s\n",
              state.job_id, to_delete.c_str(), s.ToString().c_str());
        }
      }
    }
  }
#ifndef ROCKSDB_LITE
  wal_manager_.PurgeObsoleteWALFiles();
#endif  // ROCKSDB_LITE
  // LogFlush(immutable_db_options_.info_log);
}

void DBImpl::DeleteObsoleteFiles() {
  mutex_.AssertHeld();
  JobContext job_context(next_job_id_.fetch_add(1));
  FindObsoleteFiles(&job_context, true);

  mutex_.Unlock();
  if (job_context.HaveSomethingToDelete()) {
    PurgeObsoleteFiles(job_context);
  }
  job_context.Clean();
  mutex_.Lock();
}
}
}  // namespace xengine
