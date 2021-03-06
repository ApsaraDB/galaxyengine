// Portions Copyright (c) 2020, Alibaba Group Holding Limited
// Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <ctime>
#include "port/sys_time.h"
#include "util/arena.h"
#include "util/autovector.h"
#include "xengine/env.h"
#include "logger/logger.h"

namespace xengine {
namespace util {

// A class to buffer info log entries and flush them in the end.
class LogBuffer {
 public:
  // log_level: the log level for all the logs
  // info_log:  logger to write the logs to
  LogBuffer(const logger::InfoLogLevel log_level);

  // Add a log entry to the buffer. Use default max_log_size.
  // max_log_size indicates maximize log size, including some metadata.
  void AddLogToBuffer(size_t max_log_size, const char* format, va_list ap);

  size_t IsEmpty() const { return logs_.empty(); }

  // Flush all buffered log to the info log.
  void FlushBufferToLog();

 private:
  // One log entry with its timestamp
  struct BufferedLog {
    struct timeval now_tv;  // Timestamp of the log
    char message[1];        // Beginning of log message
  };

  const logger::InfoLogLevel log_level_;
  Arena arena_;
  autovector<BufferedLog*> logs_;
};

// Add log to the LogBuffer for a delayed info logging. It can be used when
// we want to add some logs inside a mutex.
// max_log_size indicates maximize log size, including some metadata.
extern void LogToBuffer(LogBuffer* log_buffer, size_t max_log_size,
                        const char* format, ...);
// Same as previous function, but with default max log size.
extern void LogToBuffer(LogBuffer* log_buffer, const char* format, ...);

}  // namespace util
}  // namespace xengine
