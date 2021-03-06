//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#if 0  // GCOV
#include <string>

#include "util/event_logger.h"
#include "util/testharness.h"

namespace xengine {
namespace util {

class EventLoggerTest : public testing::Test {};

class StringLogger : public Logger {
 public:
  using Logger::Logv;
  virtual void Logv(const char* format, va_list ap) override {
    vsnprintf(buffer_, sizeof(buffer_), format, ap);
  }
  char* buffer() { return buffer_; }

 private:
  char buffer_[1000];
};

TEST_F(EventLoggerTest, SimpleTest) {
  StringLogger logger;
  EventLogger event_logger(&logger);
  event_logger.Log() << "id" << 5 << "event"
                     << "just_testing";
  std::string output(logger.buffer());
  ASSERT_TRUE(output.find("\"event\": \"just_testing\"") != std::string::npos);
  ASSERT_TRUE(output.find("\"id\": 5") != std::string::npos);
  ASSERT_TRUE(output.find("\"time_micros\"") != std::string::npos);
}

}  // namespace util
}  // namespace xengine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
	xengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}
#endif  // GCOV
