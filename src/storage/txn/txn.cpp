//
// Created by jinhai on 23-6-4.
//

#include "txn.h"
#include "main/logger.h"
#include "common/utility/infinity_assert.h"
#include "common/utility/defer_op.h"
#include "storage/txn/txn_manager.h"

namespace infinity {

UniquePtr<String>
Txn::GetTableEntry(const String& db_name, const String& table_name, TableEntry*& table_entry) {
    if(db_name_.empty()) {
        db_name_ = db_name;
    } else {
        if(db_name_ != db_name) {
            String err_msg = fmt::format("Attempt to insert data into another database and table {}/{}", db_name, table_name);
            LOG_ERROR(err_msg);
            return MakeUnique<String>(err_msg);
        }
    }

    auto table_iter = txn_table_entries_.find(table_name);
    if(table_iter == txn_table_entries_.end()) {
        // not found the table in cached entries
        EntryResult entry_result = this->GetTableByName(db_name, table_name);
        if(entry_result.entry_ == nullptr) {
            return std::move(entry_result.err_);
        }

        table_entry = (TableEntry*)entry_result.entry_;
        txn_table_entries_[table_name] = table_entry;
    } else {
        table_entry = (TableEntry*)table_iter->second;
    }

    return nullptr;
}

UniquePtr<String>
Txn::Append(const String& db_name, const String& table_name, const SharedPtr<DataBlock>& input_block) {
    TableEntry* table_entry{nullptr};
    UniquePtr<String> err_msg = GetTableEntry(db_name, table_name, table_entry);
    if(err_msg != nullptr) {
        return err_msg;
    }

    TxnTableStore* table_store {nullptr};
    if(txn_tables_store_.find(table_name) == txn_tables_store_.end()) {
        txn_tables_store_[table_name] = MakeUnique<TxnTableStore>(table_name, table_entry, this);
    }
    table_store = txn_tables_store_[table_name].get();

    return table_store->Append(input_block);
}

UniquePtr<String>
Txn::Delete(const String& db_name, const String& table_name, const Vector<RowID>& row_ids) {
    return nullptr;
}

UniquePtr<MetaTableState>
Txn::GetTableMeta(const String& db_name, const String& table_name, const Vector<ColumnID>& columns) {
    UniquePtr<MetaTableState> meta_table_state = MakeUnique<MetaTableState>();
    auto txn_table_iter = txn_tables_store_.find(table_name);
    if(txn_table_iter != txn_tables_store_.end()) {
        Vector<SharedPtr<DataBlock>>& blocks = txn_table_iter->second->blocks_;
        meta_table_state->local_blocks_.reserve(blocks.size());
        for(const auto& data_block: blocks) {
            MetaDataBlockState data_block_state;
            data_block_state.data_block_ = data_block.get();
            for(const auto& column_id: columns) {
                MetaColumnVectorState column_vector_state;
                column_vector_state.column_vector_ = data_block->column_vectors[column_id].get();
                data_block_state.column_vector_map_[column_id] = column_vector_state;
            }
            meta_table_state->local_blocks_.emplace_back(data_block_state);
        }
    }

    TableEntry* table_entry{nullptr};
    UniquePtr<String> err_msg = GetTableEntry(db_name, table_name, table_entry);
    if(err_msg != nullptr) {
        StorageError(*err_msg);
    }

    u64 max_segment_id =  TableEntry::GetMaxSegmentID(table_entry);
    for(u64 segment_id = 0; segment_id < max_segment_id; ++ segment_id) {
        SegmentEntry* segment_entry_ptr = TableEntry::GetSegmentByID(table_entry, segment_id);

        MetaSegmentState segment_state;
        segment_state.segment_entry_ = segment_entry_ptr;
        for(const auto& column_id: columns) {
            MetaColumnDataState column_data_state;
            column_data_state.column_data_ = SegmentEntry::GetColumnDataByID(segment_entry_ptr, column_id);
            segment_state.column_data_map_[column_id] = column_data_state;
        }
        meta_table_state->segment_map_[segment_id] = segment_state;
    }

    return meta_table_state;
}

SharedPtr<GetState>
Txn::InitializeGet(GetParam) {
    return nullptr;
}

void
Txn::TableGet() {

}

void
Txn::IndexGet() {

}

SharedPtr<ScanState>
Txn::InitializeScan(const String& db_name, const String& table_name, const Vector<ColumnID>& column_ids) {

    SharedPtr<ScanState> scan_state{};
    auto txn_table_iter = txn_tables_store_.find(table_name);
    if(txn_table_iter != txn_tables_store_.end()) {
        scan_state->txn_table_store_ = txn_table_iter->second.get();
        scan_state->table_entry_ = (void*)(txn_table_iter->second->table_entry_);
        scan_state->scan_location_ = ScanLocation::kLocal;
    } else {
        TableEntry* table_entry{nullptr};
        UniquePtr<String> err_msg = GetTableEntry(db_name, table_name, table_entry);
        if(err_msg != nullptr) {
            StorageError(*err_msg);
        }
        scan_state->table_entry_ = (void*)table_entry;
        scan_state->scan_location_ = ScanLocation::kGlobal;
    }
    scan_state->columns_ = column_ids;

    return scan_state;
}

void
Txn::Scan(ScanState* scan_state, SharedPtr<DataBlock>& output_block) {

    if(scan_state->scan_location_ == ScanLocation::kLocal) {
        // Scan local storage
        TxnTableStore* local_table_store = (TxnTableStore* )scan_state->txn_table_store_;
        if(scan_state->filter_ptr_ == nullptr) {
            // No filter
//            if(scan_state->)
        } else {
            // With filter
        }
//        local_table_store->blocks_
    }

    // Scan global storage
}

void
Txn::TableScan(ScanState* scan_state, SharedPtr<DataBlock>& output_block) {
    NotImplementError("Not implemented")
}

void
Txn::IndexScan(ScanState* scan_state, SharedPtr<DataBlock>& output_block) {
    NotImplementError("Not implemented")
}

void
Txn::AnnScan(ScanState* scan_state, SharedPtr<DataBlock>& output_block) {
    NotImplementError("Not implemented")
}

UniquePtr<String>
Txn::CompleteScan(const String& db_name, const String& table_name) {
    return nullptr;
}

BufferManager*
Txn::GetBufferMgr() const {
    return this->txn_mgr_->GetBufferMgr();
}

EntryResult
Txn::CreateDatabase(const String& db_name) {

    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    EntryResult res = NewCatalog::CreateDatabase(catalog_, db_name, this->txn_id_, begin_ts, txn_mgr_);
    if(res.entry_ == nullptr) {
        return res;
    }

    if(res.entry_->entry_type_ != EntryType::kDatabase) {
        return {nullptr, MakeUnique<String>("Invalid database type")};
    }

    auto* db_entry = static_cast<DBEntry*>(res.entry_);
    txn_dbs_.insert(db_entry);
    db_names_.insert(db_name);
    return res;
}

EntryResult
Txn::DropDatabase(const String& db_name) {

    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    EntryResult res = NewCatalog::DropDatabase(catalog_, db_name, txn_id_, begin_ts, txn_mgr_);

    if(res.entry_ == nullptr) {
        return res;
    }

    DBEntry* dropped_db_entry = static_cast<DBEntry*>(res.entry_);

    if(txn_dbs_.contains(dropped_db_entry)) {
        txn_dbs_.erase(dropped_db_entry);
    } else {
        txn_dbs_.insert(dropped_db_entry);
    }

    if(db_names_.contains(db_name)) {
        db_names_.erase(db_name);
    } else {
        db_names_.insert(db_name);
    }

    return res;
}

EntryResult
Txn::GetDatabase(const String& db_name) {
    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    return NewCatalog::GetDatabase(catalog_, db_name, this->txn_id_, begin_ts);
}


EntryResult
Txn::CreateTable(const String& db_name, const SharedPtr<TableDef>& table_def) {
    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    EntryResult db_entry_result =  NewCatalog::GetDatabase(catalog_, db_name, this->txn_id_, begin_ts);
    if(db_entry_result.entry_ == nullptr) {
        // Error
        return db_entry_result;
    }

    DBEntry* db_entry = (DBEntry*)db_entry_result.entry_;

    EntryResult res = DBEntry::CreateTable(db_entry, table_def, txn_id_, begin_ts, txn_mgr_);
    if(res.entry_ == nullptr) {
        return res;
    }

    if(res.entry_->entry_type_ != EntryType::kTable) {
        return {nullptr, MakeUnique<String>("Invalid database type")};
    }

    auto* table_entry = static_cast<TableEntry*>(res.entry_);
    txn_tables_.insert(table_entry);
    table_names_.insert(*table_def->table_name());
    return res;
}

EntryResult
Txn::DropTableByName(const String& db_name, const String& table_name) {
    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    EntryResult db_entry_result = NewCatalog::GetDatabase(catalog_, db_name, this->txn_id_, begin_ts);
    if(db_entry_result.entry_ == nullptr) {
        // Error
        return db_entry_result;
    }

    DBEntry* db_entry = (DBEntry*)db_entry_result.entry_;

    EntryResult res = DBEntry::DropTable(db_entry, table_name, txn_id_, begin_ts, txn_mgr_);

    if(res.entry_ == nullptr) {
        return res;
    }

    TableEntry* dropped_table_entry = static_cast<TableEntry*>(res.entry_);

    if(txn_tables_.contains(dropped_table_entry)) {
        txn_tables_.erase(dropped_table_entry);
    } else {
        txn_tables_.insert(dropped_table_entry);
    }

    if(table_names_.contains(table_name)) {
        table_names_.erase(table_name);
    } else {
        table_names_.insert(table_name);
    }

    return res;
}

EntryResult
Txn::GetTableByName(const String& db_name, const String& table_name) {
    TxnState txn_state = txn_context_.GetTxnState();

    if(txn_state != TxnState::kStarted) {
        LOG_TRACE("Transaction isn't started.")
        return {nullptr, MakeUnique<String>("Transaction isn't started.")};
    }

    TxnTimeStamp begin_ts = txn_context_.GetBeginTS();

    EntryResult db_entry_result = NewCatalog::GetDatabase(catalog_, db_name, this->txn_id_, begin_ts);
    if(db_entry_result.entry_ == nullptr) {
        // Error
        return db_entry_result;
    }

    DBEntry* db_entry = (DBEntry*)db_entry_result.entry_;

    return DBEntry::GetTable(db_entry, table_name, txn_id_, begin_ts);
}

void
Txn::BeginTxn() {
    txn_context_.BeginCommit(txn_mgr_->GetTimestamp());
}

void
Txn::BeginTxn(TxnTimeStamp begin_ts) {
    txn_context_.BeginCommit(begin_ts);
}

void
Txn::CommitTxn() {
    this->CommitTxn(infinity::TxnManager::GetTimestamp());
}

void
Txn::CommitTxn(TxnTimeStamp commit_ts) {
    txn_context_.SetTxnCommitting(commit_ts);

    {
        // prepare to commit txn local data into table
        for(const auto& name_table_pair: txn_tables_store_) {
            TxnTableStore* table_local_store = name_table_pair.second.get();
            table_local_store->PrepareCommit();
        }
    }

    txn_context_.SetTxnCommitted();

    // Commit databases in catalog
    for(auto* db_entry: txn_dbs_) {
        db_entry->Commit(commit_ts);
    }

    // Commit tables in catalog
    for(auto* table_entry: txn_tables_) {
        table_entry->Commit(commit_ts);
    }

    // Commit the prepared data
    for(const auto& name_table_pair: txn_tables_store_) {
        TxnTableStore* table_local_store = name_table_pair.second.get();
        table_local_store->Commit();
    }

    // Flush to disk
    // Reset the LSN of WAL
}

void
Txn::RollbackTxn(TxnTimeStamp abort_ts) {

    txn_context_.SetTxnRollbacking(abort_ts);

    for(const auto& base_entry: txn_tables_) {
        TableEntry* table_entry = (TableEntry*)(base_entry);
        TableMeta* table_meta = TableEntry::GetTableMeta(table_entry);
        DBEntry* db_entry = TableEntry::GetDBEntry(table_entry);
        DBEntry::RemoveTableEntry(db_entry, *table_meta->table_name_, txn_id_, txn_mgr_);
    }

    for(const auto& db_name: db_names_) {
        NewCatalog::RemoveDBEntry(catalog_, db_name, this->txn_id_, txn_mgr_);
    }

    // Rollback the prepared data
    for(const auto& name_table_pair: txn_tables_store_) {
        TxnTableStore* table_local_store = name_table_pair.second.get();
        table_local_store->Rollback();
    }

    {
        txn_context_.SetTxnRollbacked();
    }
}

}