//  Portions Copyright (c) 2020, Alibaba Group Holding Limited
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//

#include "port/stack_trace.h"
#include "util/testharness.h"
#include "util/testutil.h"

#include "xengine/statistics.h"

using namespace xengine;
using namespace common;
using namespace db;
using namespace util;

namespace xengine {
namespace monitor {

class StatisticsTest : public testing::Test {};

// Sanity check to make sure that contents and order of TickersNameMap
// match Tickers enum
TEST_F(StatisticsTest, Sanity) {
  EXPECT_EQ(static_cast<size_t>(Tickers::TICKER_ENUM_MAX),
            TickersNameMap.size());

  for (uint32_t t = 0; t < Tickers::TICKER_ENUM_MAX; t++) {
    auto pair = TickersNameMap[static_cast<size_t>(t)];
    ASSERT_EQ(pair.first, t) << "Miss match at " << pair.second;
  }
}

}  // namespace monitor
}  // namespace xengine

int main(int argc, char** argv) {
  port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
	xengine::util::test::init_logger(__FILE__);
  return RUN_ALL_TESTS();
}
