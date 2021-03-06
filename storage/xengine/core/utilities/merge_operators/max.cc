//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include <memory>

#include "utilities/merge_operators.h"
#include "xengine/merge_operator.h"
#include "xengine/slice.h"

using namespace xengine;
using namespace common;
using namespace db;
using namespace util;

namespace {  // anonymous namespace

// Merge operator that picks the maximum operand, Comparison is based on
// Slice::compare
class MaxOperator : public MergeOperator {
 public:
  virtual bool FullMergeV2(const MergeOperationInput& merge_in,
                           MergeOperationOutput* merge_out) const override {
    Slice& max = merge_out->existing_operand;
    if (merge_in.existing_value) {
      max = Slice(merge_in.existing_value->data(),
                  merge_in.existing_value->size());
    }

    for (const auto& op : merge_in.operand_list) {
      if (max.compare(op) < 0) {
        max = op;
      }
    }

    return true;
  }

  virtual bool PartialMerge(const Slice& key, const Slice& left_operand,
                            const Slice& right_operand,
                            std::string* new_value) const override {
    if (left_operand.compare(right_operand) >= 0) {
      new_value->assign(left_operand.data(), left_operand.size());
    } else {
      new_value->assign(right_operand.data(), right_operand.size());
    }
    return true;
  }

  virtual bool PartialMergeMulti(const Slice& key,
                                 const std::deque<Slice>& operand_list,
                                 std::string* new_value) const override {
    Slice max;
    for (const auto& operand : operand_list) {
      if (max.compare(operand) < 0) {
        max = operand;
      }
    }

    new_value->assign(max.data(), max.size());
    return true;
  }

  virtual const char* Name() const override { return "MaxOperator"; }
};

}  // end of anonymous namespace

namespace xengine {
namespace util {

std::shared_ptr<MergeOperator> MergeOperators::CreateMaxOperator() {
  return std::make_shared<MaxOperator>();
}
}  //  namespace util
}  //  namespace xengine
