//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#ifndef MERGE_OPERATORS_H
#define MERGE_OPERATORS_H

#include <stdio.h>
#include <memory>

#include "xengine/merge_operator.h"

namespace xengine {

namespace db {
class MergeOperator;
}

namespace util {

class MergeOperators {
 public:
  static std::shared_ptr<db::MergeOperator> CreatePutOperator();
  static std::shared_ptr<db::MergeOperator> CreateDeprecatedPutOperator();
  static std::shared_ptr<db::MergeOperator> CreateUInt64AddOperator();
  static std::shared_ptr<db::MergeOperator> CreateStringAppendOperator();
  static std::shared_ptr<db::MergeOperator> CreateStringAppendTESTOperator();
  static std::shared_ptr<db::MergeOperator> CreateMaxOperator();

  // Will return a different merge operator depending on the string.
  // TODO: Hook the "name" up to the actual Name() of the MergeOperators?
  static std::shared_ptr<db::MergeOperator> CreateFromStringId(
      const std::string& name) {
    if (name == "put") {
      return CreatePutOperator();
    } else if (name == "put_v1") {
      return CreateDeprecatedPutOperator();
    } else if (name == "uint64add") {
      return CreateUInt64AddOperator();
    } else if (name == "stringappend") {
      return CreateStringAppendOperator();
    } else if (name == "stringappendtest") {
      return CreateStringAppendTESTOperator();
    } else if (name == "max") {
      return CreateMaxOperator();
    } else {
      // Empty or unknown, just return nullptr
      return nullptr;
    }
  }
};

}  //  namespace util
}  //  namespace xengine

#endif
