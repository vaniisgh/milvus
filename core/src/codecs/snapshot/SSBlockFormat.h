// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "knowhere/common/BinarySet.h"
#include "storage/FSHandler.h"

namespace milvus {
namespace codec {

struct ReadRange {
    ReadRange(int64_t offset, int64_t num_bytes) : offset_(offset), num_bytes_(num_bytes) {
    }
    int64_t offset_;
    int64_t num_bytes_;
};

using ReadRanges = std::vector<ReadRange>;

class SSBlockFormat {
 public:
    SSBlockFormat() = default;

    void
    read(const storage::FSHandlerPtr& fs_ptr, const std::string& file_path, std::vector<uint8_t>& raw);

    void
    read(const storage::FSHandlerPtr& fs_ptr, const std::string& file_path, int64_t offset, int64_t num_bytes,
         std::vector<uint8_t>& raw);

    void
    read(const storage::FSHandlerPtr& fs_ptr, const std::string& file_path, const ReadRanges& read_ranges,
         std::vector<uint8_t>& raw);

    void
    write(const storage::FSHandlerPtr& fs_ptr, const std::string& file_path, const std::vector<uint8_t>& raw);

    // No copy and move
    SSBlockFormat(const SSBlockFormat&) = delete;
    SSBlockFormat(SSBlockFormat&&) = delete;

    SSBlockFormat&
    operator=(const SSBlockFormat&) = delete;
    SSBlockFormat&
    operator=(SSBlockFormat&&) = delete;
};

using SSBlockFormatPtr = std::shared_ptr<SSBlockFormat>;

}  // namespace codec
}  // namespace milvus