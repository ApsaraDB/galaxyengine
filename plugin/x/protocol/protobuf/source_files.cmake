# Copyright (c) 2017, 2019, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

SET(MYSQLX_PROTOBUF_PROTO_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

SET(MYSQLX_PROTOBUF_PROTO_FILES
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_datatypes.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_connection.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_expect.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_expr.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_crud.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_sql.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_session.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_notice.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_resultset.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_cursor.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/mysqlx_prepare.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/galaxyx.proto"
  "${MYSQLX_PROTOBUF_PROTO_DIR}/galaxyx_sql.proto"
)

