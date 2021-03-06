//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "util/log_buffer.h"
#include "port/port.h"
#include "port/sys_time.h"

using namespace xengine::port;

namespace xengine {
namespace util {

LogBuffer::LogBuffer(const logger::InfoLogLevel log_level)
    : log_level_(log_level),
      arena_(Arena::kMinBlockSize, 0, memory::ModId::kLogBuffer) {}

void LogBuffer::AddLogToBuffer(size_t max_log_size, const char* format,
                               va_list ap) {
  if (!LOG.need_print_mod(LOG_MOD(XENGINE), log_level_)) {
    // Skip the level because of its level.
    return;
  }

  char* alloc_mem = arena_.AllocateAligned(max_log_size);
  BufferedLog* buffered_log = new (alloc_mem) BufferedLog();
  char* p = buffered_log->message;
  char* limit = alloc_mem + max_log_size - 1;

  // store the time
  gettimeofday(&(buffered_log->now_tv), nullptr);

  // Print the message
  if (p < limit) {
    va_list backup_ap;
    va_copy(backup_ap, ap);
    auto n = vsnprintf(p, limit - p, format, backup_ap);
#ifndef OS_WIN
    // MS reports -1 when the buffer is too short
    assert(n >= 0);
#endif
    if (n > 0) {
      p += n;
    } else {
      p = limit;
    }
    va_end(backup_ap);
  }

  if (p > limit) {
    p = limit;
  }

  // Add '\0' to the end
  *p = '\0';

  logs_.push_back(buffered_log);
}

void LogBuffer::FlushBufferToLog() {
  for (BufferedLog* log : logs_) {
    const time_t seconds = log->now_tv.tv_sec;
    struct tm t;
    localtime_r(&seconds, &t);
    if (LOG.need_print_mod(LOG_MOD(XENGINE), log_level_)) {
      LOG.print_log_fmt(
          "[XENGINE]", log_level_, FILE_NAME, FUNCTION_NAME, LINE_NUM,
          "(Original Log Time %04d/%02d/%02d-%02d:%02d:%02d.%06d) %s",
          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
          t.tm_sec, static_cast<int>(log->now_tv.tv_usec), log->message);
    }
  }
  logs_.clear();
}

void LogToBuffer(LogBuffer* log_buffer, size_t max_log_size, const char* format,
                 ...) {
  if (log_buffer != nullptr) {
    va_list ap;
    va_start(ap, format);
    log_buffer->AddLogToBuffer(max_log_size, format, ap);
    va_end(ap);
  }
}

void LogToBuffer(LogBuffer* log_buffer, const char* format, ...) {
  const size_t kDefaultMaxLogSize = 512;
  if (log_buffer != nullptr) {
    va_list ap;
    va_start(ap, format);
    log_buffer->AddLogToBuffer(kDefaultMaxLogSize, format, ap);
    va_end(ap);
  }
}

}  // namespace util
}  // namespace xengine
