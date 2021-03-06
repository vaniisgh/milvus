// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include <fiu-control.h>
#include <fiu-local.h>
#include <gtest/gtest.h>

#include <string>
#include <set>
#include <algorithm>

#include "ssdb/utils.h"
#include "db/SnapshotVisitor.h"
#include "db/snapshot/IterateHandler.h"
#include "knowhere/index/vector_index/helpers/IndexParameter.h"

using SegmentVisitor = milvus::engine::SegmentVisitor;

namespace {
milvus::Status
CreateCollection(std::shared_ptr<SSDBImpl> db, const std::string& collection_name, const LSN_TYPE& lsn) {
    CreateCollectionContext context;
    context.lsn = lsn;
    auto collection_schema = std::make_shared<Collection>(collection_name);
    context.collection = collection_schema;
    auto vector_field = std::make_shared<Field>("vector", 0,
                                                milvus::engine::FieldType::VECTOR);
    auto vector_field_element = std::make_shared<FieldElement>(0, 0, "ivfsq8",
                                                               milvus::engine::FieldElementType::FET_INDEX);
    auto int_field = std::make_shared<Field>("int", 0,
                                             milvus::engine::FieldType::INT32);
    context.fields_schema[vector_field] = {vector_field_element};
    context.fields_schema[int_field] = {};

    return db->CreateCollection(context);
}

static constexpr int64_t COLLECTION_DIM = 128;

milvus::Status
CreateCollection2(std::shared_ptr<SSDBImpl> db, const std::string& collection_name, const LSN_TYPE& lsn) {
    CreateCollectionContext context;
    context.lsn = lsn;
    auto collection_schema = std::make_shared<Collection>(collection_name);
    context.collection = collection_schema;

    nlohmann::json params;
    params[milvus::knowhere::meta::DIM] = COLLECTION_DIM;
    auto vector_field = std::make_shared<Field>("vector", 0, milvus::engine::FieldType::VECTOR, params);
    context.fields_schema[vector_field] = {};

    std::unordered_map<std::string, milvus::engine::meta::hybrid::DataType> attr_type = {
        {"field_0", milvus::engine::FieldType::INT32},
        {"field_1", milvus::engine::FieldType::INT64},
        {"field_2", milvus::engine::FieldType::DOUBLE},
    };

    std::vector<std::string> field_names;
    for (auto& pair : attr_type) {
        auto field = std::make_shared<Field>(pair.first, 0, pair.second);
        context.fields_schema[field] = {};
        field_names.push_back(pair.first);
    }

    return db->CreateCollection(context);
}

void
BuildEntities(uint64_t n, uint64_t batch_index, milvus::engine::DataChunkPtr& data_chunk) {
    data_chunk = std::make_shared<milvus::engine::DataChunk>();
    data_chunk->count_ = n;

    milvus::engine::VectorsData vectors;
    vectors.vector_count_ = n;
    vectors.float_data_.clear();
    vectors.float_data_.resize(n * COLLECTION_DIM);
    float* data = vectors.float_data_.data();
    for (uint64_t i = 0; i < n; i++) {
        for (int64_t j = 0; j < COLLECTION_DIM; j++) data[COLLECTION_DIM * i + j] = drand48();
        data[COLLECTION_DIM * i] += i / 2000.;

        vectors.id_array_.push_back(n * batch_index + i);
    }

    milvus::engine::FIXED_FIELD_DATA& raw = data_chunk->fixed_fields_["vector"];
    raw.resize(vectors.float_data_.size() * sizeof(float));
    memcpy(raw.data(), vectors.float_data_.data(), vectors.float_data_.size() * sizeof(float));

    std::vector<int32_t> value_0;
    std::vector<int64_t> value_1;
    std::vector<double> value_2;
    value_0.resize(n);
    value_1.resize(n);
    value_2.resize(n);

    std::default_random_engine e;
    std::uniform_real_distribution<double> u(0, 1);
    for (uint64_t i = 0; i < n; ++i) {
        value_0[i] = i;
        value_1[i] = i + n;
        value_2[i] = u(e);
    }

    {
        milvus::engine::FIXED_FIELD_DATA& raw = data_chunk->fixed_fields_["field_0"];
        raw.resize(value_0.size() * sizeof(int32_t));
        memcpy(raw.data(), value_0.data(), value_0.size() * sizeof(int32_t));
    }

    {
        milvus::engine::FIXED_FIELD_DATA& raw = data_chunk->fixed_fields_["field_1"];
        raw.resize(value_1.size() * sizeof(int64_t));
        memcpy(raw.data(), value_1.data(), value_1.size() * sizeof(int64_t));
    }

    {
        milvus::engine::FIXED_FIELD_DATA& raw = data_chunk->fixed_fields_["field_2"];
        raw.resize(value_2.size() * sizeof(double));
        memcpy(raw.data(), value_2.data(), value_2.size() * sizeof(double));
    }
}
}  // namespace

TEST_F(SSDBTest, CollectionTest) {
    LSN_TYPE lsn = 0;
    auto next_lsn = [&]() -> decltype(lsn) {
        return ++lsn;
    };
    std::string c1 = "c1";
    auto status = CreateCollection(db_, c1, next_lsn());
    ASSERT_TRUE(status.ok());

    ScopedSnapshotT ss;
    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());
    ASSERT_TRUE(ss);
    ASSERT_EQ(ss->GetName(), c1);

    bool has;
    status = db_->HasCollection(c1, has);
    ASSERT_TRUE(has);
    ASSERT_TRUE(status.ok());

    ASSERT_EQ(ss->GetCollectionCommit()->GetRowCount(), 0);
    milvus::engine::snapshot::SIZE_TYPE row_cnt = 0;
    status = db_->GetCollectionRowCount(c1, row_cnt);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(row_cnt, 0);

    std::vector<std::string> names;
    status = db_->AllCollections(names);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(names.size(), 1);
    ASSERT_EQ(names[0], c1);

    std::string c1_1 = "c1";
    status = CreateCollection(db_, c1_1, next_lsn());
    ASSERT_FALSE(status.ok());

    std::string c2 = "c2";
    status = CreateCollection(db_, c2, next_lsn());
    ASSERT_TRUE(status.ok());

    status = db_->AllCollections(names);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(names.size(), 2);

    status = db_->DropCollection(c1);
    ASSERT_TRUE(status.ok());

    status = db_->AllCollections(names);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(names.size(), 1);
    ASSERT_EQ(names[0], c2);

    status = db_->DropCollection(c1);
    ASSERT_FALSE(status.ok());
}

TEST_F(SSDBTest, PartitionTest) {
    LSN_TYPE lsn = 0;
    auto next_lsn = [&]() -> decltype(lsn) {
        return ++lsn;
    };
    std::string c1 = "c1";
    auto status = CreateCollection(db_, c1, next_lsn());
    ASSERT_TRUE(status.ok());

    std::vector<std::string> partition_names;
    status = db_->ShowPartitions(c1, partition_names);
    ASSERT_EQ(partition_names.size(), 1);
    ASSERT_EQ(partition_names[0], "_default");

    std::string p1 = "p1";
    std::string c2 = "c2";
    status = db_->CreatePartition(c2, p1);
    ASSERT_FALSE(status.ok());

    status = db_->CreatePartition(c1, p1);
    ASSERT_TRUE(status.ok());

    status = db_->ShowPartitions(c1, partition_names);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(partition_names.size(), 2);

    status = db_->CreatePartition(c1, p1);
    ASSERT_FALSE(status.ok());

    status = db_->DropPartition(c1, "p3");
    ASSERT_FALSE(status.ok());

    status = db_->DropPartition(c1, p1);
    ASSERT_TRUE(status.ok());
    status = db_->ShowPartitions(c1, partition_names);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(partition_names.size(), 1);
}

TEST_F(SSDBTest, IndexTest) {
    LSN_TYPE lsn = 0;
    auto next_lsn = [&]() -> decltype(lsn) {
        return ++lsn;
    };

    std::string c1 = "c1";
    auto status = CreateCollection(db_, c1, next_lsn());
    ASSERT_TRUE(status.ok());

    std::stringstream p_name;
    auto num = RandomInt(3, 5);
    for (auto i = 0; i < num; ++i) {
        p_name.str("");
        p_name << "partition_" << i;
        status = db_->CreatePartition(c1, p_name.str());
        ASSERT_TRUE(status.ok());
    }

    ScopedSnapshotT ss;
    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());

    SegmentFileContext sf_context;
    SFContextBuilder(sf_context, ss);

    auto new_total = 0;
    auto& partitions = ss->GetResources<Partition>();
    for (auto& kv : partitions) {
        num = RandomInt(2, 5);
        auto row_cnt = 100;
        for (auto i = 0; i < num; ++i) {
            ASSERT_TRUE(CreateSegment(ss, kv.first, next_lsn(), sf_context, row_cnt).ok());
        }
        new_total += num;
    }

    auto field_element_id = ss->GetFieldElementId(sf_context.field_name, sf_context.field_element_name);
    ASSERT_NE(field_element_id, 0);

    auto filter1 = [&](SegmentFile::Ptr segment_file) -> bool {
        if (segment_file->GetFieldElementId() == field_element_id) {
            return true;
        }
        return false;
    };

    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());
    auto sf_collector = std::make_shared<SegmentFileCollector>(ss, filter1);
    sf_collector->Iterate();
    ASSERT_EQ(new_total, sf_collector->segment_files_.size());

    status = db_->DropIndex(c1, sf_context.field_name, sf_context.field_element_name);
    ASSERT_TRUE(status.ok());

    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());
    sf_collector = std::make_shared<SegmentFileCollector>(ss, filter1);
    sf_collector->Iterate();
    ASSERT_EQ(0, sf_collector->segment_files_.size());

    {
        auto& field_elements = ss->GetResources<FieldElement>();
        for (auto& kv : field_elements) {
            ASSERT_NE(kv.second->GetID(), field_element_id);
        }
    }
}

TEST_F(SSDBTest, VisitorTest) {
    LSN_TYPE lsn = 0;
    auto next_lsn = [&]() -> decltype(lsn) {
        return ++lsn;
    };

    std::string c1 = "c1";
    auto status = CreateCollection(db_, c1, next_lsn());
    ASSERT_TRUE(status.ok());

    std::stringstream p_name;
    auto num = RandomInt(1, 3);
    for (auto i = 0; i < num; ++i) {
        p_name.str("");
        p_name << "partition_" << i;
        status = db_->CreatePartition(c1, p_name.str());
        ASSERT_TRUE(status.ok());
    }

    ScopedSnapshotT ss;
    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());

    SegmentFileContext sf_context;
    SFContextBuilder(sf_context, ss);

    auto new_total = 0;
    auto& partitions = ss->GetResources<Partition>();
    ID_TYPE partition_id;
    for (auto& kv : partitions) {
        num = RandomInt(1, 3);
        auto row_cnt = 100;
        for (auto i = 0; i < num; ++i) {
            ASSERT_TRUE(CreateSegment(ss, kv.first, next_lsn(), sf_context, row_cnt).ok());
        }
        new_total += num;
        partition_id = kv.first;
    }

    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());

    auto executor = [&](const Segment::Ptr& segment, SegmentIterator* handler) -> Status {
        auto visitor = SegmentVisitor::Build(ss, segment->GetID());
        if (!visitor) {
            return Status(milvus::SS_ERROR, "Cannot build segment visitor");
        }
        std::cout << visitor->ToString() << std::endl;
        return Status::OK();
    };

    auto segment_handler = std::make_shared<SegmentIterator>(ss, executor);
    segment_handler->Iterate();
    std::cout << segment_handler->GetStatus().ToString() << std::endl;
    ASSERT_TRUE(segment_handler->GetStatus().ok());

    auto row_cnt = ss->GetCollectionCommit()->GetRowCount();
    auto new_segment_row_cnt = 1024;
    {
        OperationContext context;
        context.lsn = next_lsn();
        context.prev_partition = ss->GetResource<Partition>(partition_id);
        auto op = std::make_shared<NewSegmentOperation>(context, ss);
        SegmentPtr new_seg;
        status = op->CommitNewSegment(new_seg);
        ASSERT_TRUE(status.ok());
        SegmentFilePtr seg_file;
        auto nsf_context = sf_context;
        nsf_context.segment_id = new_seg->GetID();
        nsf_context.partition_id = new_seg->GetPartitionId();
        status = op->CommitNewSegmentFile(nsf_context, seg_file);
        ASSERT_TRUE(status.ok());
        auto ctx = op->GetContext();
        ASSERT_TRUE(ctx.new_segment);
        auto visitor = SegmentVisitor::Build(ss, ctx.new_segment, ctx.new_segment_files);
        ASSERT_TRUE(visitor);
        ASSERT_EQ(visitor->GetSegment(), new_seg);
        ASSERT_FALSE(visitor->GetSegment()->IsActive());

        int file_num = 0;
        auto field_visitors = visitor->GetFieldVisitors();
        for (auto& kv : field_visitors) {
            auto& field_visitor = kv.second;
            auto field_element_visitors = field_visitor->GetElementVistors();
            for (auto& kkvv : field_element_visitors) {
                auto& field_element_visitor = kkvv.second;
                auto file = field_element_visitor->GetFile();
                if (file) {
                    file_num++;
                    ASSERT_FALSE(file->IsActive());
                }
            }
        }
        ASSERT_EQ(file_num, 1);

        std::cout << visitor->ToString() << std::endl;
        status = op->CommitRowCount(new_segment_row_cnt);
        status = op->Push();
        ASSERT_TRUE(status.ok());
    }
    status = Snapshots::GetInstance().GetSnapshot(ss, c1);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(ss->GetCollectionCommit()->GetRowCount(), row_cnt + new_segment_row_cnt);
    std::cout << ss->ToString() << std::endl;
}

TEST_F(SSDBTest, InsertTest) {
    std::string collection_name = "MERGE_TEST";
    auto status = CreateCollection2(db_, collection_name, 0);
    ASSERT_TRUE(status.ok());

    const uint64_t entity_count = 100;
    milvus::engine::DataChunkPtr data_chunk;
    BuildEntities(entity_count, 0, data_chunk);

    status = db_->InsertEntities(collection_name, "", data_chunk);
    ASSERT_TRUE(status.ok());

    status = db_->Flush();
    ASSERT_TRUE(status.ok());

    uint64_t row_count = 0;
    status = db_->GetCollectionRowCount(collection_name, row_count);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(row_count, entity_count);
}

TEST_F(SSDBTest, MergeTest) {
    std::string collection_name = "MERGE_TEST";
    auto status = CreateCollection2(db_, collection_name, 0);
    ASSERT_TRUE(status.ok());

    const uint64_t entity_count = 100;
    milvus::engine::DataChunkPtr data_chunk;
    BuildEntities(entity_count, 0, data_chunk);

    int64_t repeat = 2;
    for (int32_t i = 0; i < repeat; i++) {
        status = db_->InsertEntities(collection_name, "", data_chunk);
        ASSERT_TRUE(status.ok());

        status = db_->Flush();
        ASSERT_TRUE(status.ok());
    }

    sleep(2); // wait to merge

    uint64_t row_count = 0;
    status = db_->GetCollectionRowCount(collection_name, row_count);
    ASSERT_TRUE(status.ok());
    ASSERT_EQ(row_count, entity_count * repeat);
}
