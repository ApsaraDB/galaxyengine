/*
 * Portions Copyright (c) 2020, Alibaba Group Holding Limited
 */
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//

#ifndef ROCKSDB_LITE
#pragma once
#include <string>
#include <vector>

#include "xengine/status.h"
#include "xengine/utilities/stackable_db.h"

namespace xengine {
namespace db {
//
// A position in the earth's geoid
//
class GeoPosition {
 public:
  double latitude;
  double longitude;

  explicit GeoPosition(double la = 0, double lo = 0)
      : latitude(la), longitude(lo) {}
};

//
// Description of an object on the Geoid. It is located by a GPS location,
// and is identified by the id. The value associated with this object is
// an opaque string 'value'. Different objects identified by unique id's
// can have the same gps-location associated with them.
//
class GeoObject {
 public:
  GeoPosition position;
  std::string id;
  std::string value;

  GeoObject() {}

  GeoObject(const GeoPosition& pos, const std::string& i,
            const std::string& val)
      : position(pos), id(i), value(val) {}
};

class GeoIterator {
 public:
  GeoIterator() = default;
  virtual ~GeoIterator() {}
  virtual void Next() = 0;
  virtual bool Valid() const = 0;
  virtual const GeoObject& geo_object() = 0;
  virtual common::Status status() const = 0;
};

//
// Stack your DB with GeoDB to be able to get geo-spatial support
//
class GeoDB : public util::StackableDB {
 public:
  // GeoDBOptions have to be the same as the ones used in a previous
  // incarnation of the DB
  //
  // GeoDB owns the pointer `DB* db` now. You should not delete it or
  // use it after the invocation of GeoDB
  // GeoDB(DB* db, const GeoDBOptions& options) : StackableDB(db) {}
  explicit GeoDB(DB* db) : util::StackableDB(db) {}
  virtual ~GeoDB() {}

  // Insert a new object into the location database. The object is
  // uniquely identified by the id. If an object with the same id already
  // exists in the db, then the old one is overwritten by the new
  // object being inserted here.
  virtual common::Status Insert(const GeoObject& object) = 0;

  // Retrieve the value of the object located at the specified GPS
  // location and is identified by the 'id'.
  virtual common::Status GetByPosition(const GeoPosition& pos,
                                       const common::Slice& id,
                                       std::string* value) = 0;

  // Retrieve the value of the object identified by the 'id'. This method
  // could be potentially slower than GetByPosition
  virtual common::Status GetById(const common::Slice& id,
                                 GeoObject* object) = 0;

  // Delete the specified object
  virtual common::Status Remove(const common::Slice& id) = 0;

  // Returns an iterator for the items within a circular radius from the
  // specified gps location. If 'number_of_values' is specified,
  // then the iterator is capped to that number of objects.
  // The radius is specified in 'meters'.
  virtual GeoIterator* SearchRadial(const GeoPosition& pos, double radius,
                                    int number_of_values = INT_MAX) = 0;
};

}  // namespace db
}  // namespace xengine
#endif  // ROCKSDB_LITE
