/*
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <string>
#include "xengine/options.h"
#include "xengine/status.h"

namespace xengine {
namespace common {
// Try to migrate DB created with old_opts to be use new_opts.
// Multiple column families is not supported.
// It is best-effort. No guarantee to succeed.
// A full compaction may be executed.
Status OptionChangeMigration(std::string dbname, const Options& old_opts,
                             const Options& new_opts);
}  // namespace common
}  // namespace xengine
