//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <iostream>

#include "util/coding.h"
#include "xengine/db.h"
#include "xengine/db_dump_tool.h"
#include "xengine/env.h"

using namespace xengine::common;
using namespace xengine::db;
using namespace xengine::util;
using namespace xengine::memory;

namespace xengine {
namespace tools {

bool DbDumpTool::Run(const DumpOptions& dump_options, Options options) {
  DB* dbptr;
  Status status;
  std::unique_ptr<WritableFile, ptr_destruct_delete<WritableFile>> dumpfile_ptr;
  WritableFile *dumpfile = nullptr;
  char hostname[1024];
  int64_t timesec;
  std::string abspath;
  char json[4096];

  static const char* magicstr = "ROCKDUMP";
  static const char versionstr[8] = {0, 0, 0, 0, 0, 0, 0, 1};

  Env* env = Env::Default();

  // Open the database
  options.create_if_missing = false;
  status = DB::OpenForReadOnly(options, dump_options.db_path, &dbptr);
  if (!status.ok()) {
    std::cerr << "Unable to open database '" << dump_options.db_path
              << "' for reading: " << status.ToString() << std::endl;
    return false;
  }

  const std::unique_ptr<DB> db(dbptr);

  status =
      env->NewWritableFile(dump_options.dump_location, dumpfile, EnvOptions());
  dumpfile_ptr.reset(dumpfile);
  if (!status.ok()) {
    std::cerr << "Unable to open dump file '" << dump_options.dump_location
              << "' for writing: " << status.ToString() << std::endl;
    return false;
  }

  Slice magicslice(magicstr, 8);
  status = dumpfile->Append(magicslice);
  if (!status.ok()) {
    std::cerr << "Append failed: " << status.ToString() << std::endl;
    return false;
  }

  Slice versionslice(versionstr, 8);
  status = dumpfile->Append(versionslice);
  if (!status.ok()) {
    std::cerr << "Append failed: " << status.ToString() << std::endl;
    return false;
  }

  if (dump_options.anonymous) {
    snprintf(json, sizeof(json), "{}");
  } else {
    status = env->GetHostName(hostname, sizeof(hostname));
    status = env->GetCurrentTime(&timesec);
    status = env->GetAbsolutePath(dump_options.db_path, &abspath);
    snprintf(json, sizeof(json),
             "{ \"database-path\": \"%s\", \"hostname\": \"%s\", "
             "\"creation-time\": %" PRIi64 " }",
             abspath.c_str(), hostname, timesec);
  }

  Slice infoslice(json, strlen(json));
  char infosize[4];
  EncodeFixed32(infosize, (uint32_t)infoslice.size());
  Slice infosizeslice(infosize, 4);
  status = dumpfile->Append(infosizeslice);
  if (!status.ok()) {
    std::cerr << "Append failed: " << status.ToString() << std::endl;
    return false;
  }
  status = dumpfile->Append(infoslice);
  if (!status.ok()) {
    std::cerr << "Append failed: " << status.ToString() << std::endl;
    return false;
  }

  const std::unique_ptr<Iterator> it(db->NewIterator(ReadOptions()));
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    char keysize[4];
    EncodeFixed32(keysize, (uint32_t)it->key().size());
    Slice keysizeslice(keysize, 4);
    status = dumpfile->Append(keysizeslice);
    if (!status.ok()) {
      std::cerr << "Append failed: " << status.ToString() << std::endl;
      return false;
    }
    status = dumpfile->Append(it->key());
    if (!status.ok()) {
      std::cerr << "Append failed: " << status.ToString() << std::endl;
      return false;
    }

    char valsize[4];
    EncodeFixed32(valsize, (uint32_t)it->value().size());
    Slice valsizeslice(valsize, 4);
    status = dumpfile->Append(valsizeslice);
    if (!status.ok()) {
      std::cerr << "Append failed: " << status.ToString() << std::endl;
      return false;
    }
    status = dumpfile->Append(it->value());
    if (!status.ok()) {
      std::cerr << "Append failed: " << status.ToString() << std::endl;
      return false;
    }
  }
  if (!it->status().ok()) {
    std::cerr << "Database iteration failed: " << status.ToString()
              << std::endl;
    return false;
  }
  return true;
}

bool DbUndumpTool::Run(const UndumpOptions& undump_options, Options options) {
  DB* dbptr;
  Status status;
  Env* env;
  std::unique_ptr<SequentialFile, ptr_destruct_delete<SequentialFile>> dumpfile_ptr;
  SequentialFile *dumpfile = nullptr;
  Slice slice;
  char scratch8[8];

  static const char* magicstr = "ROCKDUMP";
  static const char versionstr[8] = {0, 0, 0, 0, 0, 0, 0, 1};

  env = Env::Default();

  status = env->NewSequentialFile(undump_options.dump_location, dumpfile,
                                  EnvOptions());
  dumpfile_ptr.reset(dumpfile);
  if (!status.ok()) {
    std::cerr << "Unable to open dump file '" << undump_options.dump_location
              << "' for reading: " << status.ToString() << std::endl;
    return false;
  }

  status = dumpfile->Read(8, &slice, scratch8);
  if (!status.ok() || slice.size() != 8 ||
      memcmp(slice.data(), magicstr, 8) != 0) {
    std::cerr << "File '" << undump_options.dump_location
              << "' is not a recognizable dump file." << std::endl;
    return false;
  }

  status = dumpfile->Read(8, &slice, scratch8);
  if (!status.ok() || slice.size() != 8 ||
      memcmp(slice.data(), versionstr, 8) != 0) {
    std::cerr << "File '" << undump_options.dump_location
              << "' version not recognized." << std::endl;
    return false;
  }

  status = dumpfile->Read(4, &slice, scratch8);
  if (!status.ok() || slice.size() != 4) {
    std::cerr << "Unable to read info blob size." << std::endl;
    return false;
  }
  uint32_t infosize = DecodeFixed32(slice.data());
  status = dumpfile->Skip(infosize);
  if (!status.ok()) {
    std::cerr << "Unable to skip info blob: " << status.ToString() << std::endl;
    return false;
  }

  options.create_if_missing = true;
  status = DB::Open(options, undump_options.db_path, &dbptr);
  if (!status.ok()) {
    std::cerr << "Unable to open database '" << undump_options.db_path
              << "' for writing: " << status.ToString() << std::endl;
    return false;
  }

  const std::unique_ptr<DB> db(dbptr);

  uint32_t last_keysize = 64;
  size_t last_valsize = 1 << 20;
  std::unique_ptr<char[]> keyscratch(new char[last_keysize]);
  std::unique_ptr<char[]> valscratch(new char[last_valsize]);

  while (1) {
    uint32_t keysize, valsize;
    Slice keyslice;
    Slice valslice;

    status = dumpfile->Read(4, &slice, scratch8);
    if (!status.ok() || slice.size() != 4) break;
    keysize = DecodeFixed32(slice.data());
    if (keysize > last_keysize) {
      while (keysize > last_keysize) last_keysize *= 2;
      keyscratch = std::unique_ptr<char[]>(new char[last_keysize]);
    }

    status = dumpfile->Read(keysize, &keyslice, keyscratch.get());
    if (!status.ok() || keyslice.size() != keysize) {
      std::cerr << "Key read failure: "
                << (status.ok() ? "insufficient data" : status.ToString())
                << std::endl;
      return false;
    }

    status = dumpfile->Read(4, &slice, scratch8);
    if (!status.ok() || slice.size() != 4) {
      std::cerr << "Unable to read value size: "
                << (status.ok() ? "insufficient data" : status.ToString())
                << std::endl;
      return false;
    }
    valsize = DecodeFixed32(slice.data());
    if (valsize > last_valsize) {
      while (valsize > last_valsize) last_valsize *= 2;
      valscratch = std::unique_ptr<char[]>(new char[last_valsize]);
    }

    status = dumpfile->Read(valsize, &valslice, valscratch.get());
    if (!status.ok() || valslice.size() != valsize) {
      std::cerr << "Unable to read value: "
                << (status.ok() ? "insufficient data" : status.ToString())
                << std::endl;
      return false;
    }

    status = db->Put(WriteOptions(), keyslice, valslice);
    if (!status.ok()) {
      fprintf(stderr, "Unable to write database entry\n");
      return false;
    }
  }

  if (undump_options.compact_db) {
    status = db->CompactRange(CompactRangeOptions(), nullptr, nullptr);
    if (!status.ok()) {
      fprintf(stderr,
              "Unable to compact the database after loading the dumped file\n");
      return false;
    }
  }
  return true;
}
}  // namespace tools
}  // namespace xengine
#endif  // ROCKSDB_LITE
