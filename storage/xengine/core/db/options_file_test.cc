// Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE
#include <string>

#include "db/db_impl.h"
#include "db/db_test_util.h"
#include "util/testharness.h"
#include "xengine/options.h"
#include "xengine/table.h"

using namespace xengine;
using namespace common;
using namespace util;
using namespace table;
using namespace cache;
using namespace memtable;
using namespace monitor;
using namespace storage;
namespace xengine {
namespace db {
class OptionsFileTest : public testing::Test {
 public:
  OptionsFileTest() : dbname_(test::TmpDir() + "/options_file_test") {}

  std::string dbname_;
};

namespace {
void UpdateOptionsFiles(DB* db,
                        std::unordered_set<std::string>* filename_history,
                        int* options_files_count) {
  std::vector<std::string> filenames;
  db->GetEnv()->GetChildren(db->GetName(), &filenames);
  uint64_t number;
  FileType type;
  *options_files_count = 0;
  for (auto filename : filenames) {
    if (ParseFileName(filename, &number, &type) && type == kOptionsFile) {
      filename_history->insert(filename);
      (*options_files_count)++;
    }
  }
}

// Verify whether the current Options Files are the latest ones.
void VerifyOptionsFileName(
    DB* db, const std::unordered_set<std::string>& past_filenames) {
  std::vector<std::string> filenames;
  std::unordered_set<std::string> current_filenames;
  db->GetEnv()->GetChildren(db->GetName(), &filenames);
  uint64_t number;
  FileType type;
  for (auto filename : filenames) {
    if (ParseFileName(filename, &number, &type) && type == kOptionsFile) {
      current_filenames.insert(filename);
    }
  }
  for (auto past_filename : past_filenames) {
    if (current_filenames.find(past_filename) != current_filenames.end()) {
      continue;
    }
    for (auto filename : current_filenames) {
      ASSERT_GT(filename, past_filename);
    }
  }
}
}  // namespace

TEST_F(OptionsFileTest, DISABLED_NumberOfOptionsFiles) {
  const int kReopenCount = 20;
  Options opt;
  opt.create_if_missing = true;
  DestroyDB(dbname_, opt);
  std::unordered_set<std::string> filename_history;
  DB* db;
  for (int i = 0; i < kReopenCount; ++i) {
    ASSERT_OK(DB::Open(opt, dbname_, &db));
    int num_options_files = 0;
    UpdateOptionsFiles(db, &filename_history, &num_options_files);
    ASSERT_GT(num_options_files, 0);
    ASSERT_LE(num_options_files, 2);
    // Make sure we always keep the latest option files.
    VerifyOptionsFileName(db, filename_history);
    delete db;
  }
}

TEST_F(OptionsFileTest, OptionsFileName) {
  const uint64_t kOptionsFileNum = 12345;
  uint64_t number;
  FileType type;

  auto options_file_name = OptionsFileName("", kOptionsFileNum);
  ASSERT_TRUE(ParseFileName(options_file_name, &number, &type, nullptr));
  ASSERT_EQ(type, kOptionsFile);
  ASSERT_EQ(number, kOptionsFileNum);

  const uint64_t kTempOptionsFileNum = 54352;
  auto temp_options_file_name = TempOptionsFileName("", kTempOptionsFileNum);
  ASSERT_TRUE(ParseFileName(temp_options_file_name, &number, &type, nullptr));
  ASSERT_NE(temp_options_file_name.find(kTempFileNameSuffix),
            std::string::npos);
  ASSERT_EQ(type, kTempFile);
  ASSERT_EQ(number, kTempOptionsFileNum);
}
}
}  // namespace xengine

int main(int argc, char** argv) {
#if !(defined NDEBUG) || !defined(OS_WIN)
  ::testing::InitGoogleTest(&argc, argv);
	xengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
#else
  return 0;
#endif  // !(defined NDEBUG) || !defined(OS_WIN)
}
#else

#include <cstdio>

int main(int argc, char** argv) {
  printf("Skipped as Options file is not supported in RocksDBLite.\n");
  return 0;
}
#endif  // !ROCKSDB_LITE
