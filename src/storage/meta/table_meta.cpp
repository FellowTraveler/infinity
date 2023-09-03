//
// Created by jinhai on 23-6-23.
//

#include "table_meta.h"
#include "storage/meta/entry/table_entry.h"
#include "common/utility/defer_op.h"
#include "storage/txn/txn_manager.h"

namespace infinity {

class TxnManager;
EntryResult
TableMeta::CreateNewEntry(TableMeta* table_meta,
                          u64 txn_id,
                          TxnTimeStamp begin_ts,
                          TxnManager* txn_mgr,
                          const SharedPtr<TableDef>& table_def) {
    TableEntry* res = nullptr;
    std::unique_lock<RWMutex> rw_locker(table_meta->rw_locker_);
    const String& table_name = *table_def->table_name();
    SharedPtr<String> base_dir = MakeShared<String>(*table_meta->base_dir_ + '/' + *table_meta->table_name_);

    if(table_meta->entry_list_.empty()) {
        // Insert a dummy entry.
        UniquePtr<BaseEntry> dummy_entry = MakeUnique<BaseEntry>(EntryType::kDummy);
        dummy_entry->deleted_ = true;
        table_meta->entry_list_.emplace_back(std::move(dummy_entry));

        // Insert the new table entry
        UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                   table_def->table_name(),
                                                                   table_def->columns(),
                                                                   table_meta,
                                                                   txn_id,
                                                                   begin_ts);
        res = table_entry.get();
        table_meta->entry_list_.emplace_front(std::move(table_entry));

        LOG_TRACE("New table entry is added: {}.", table_name);
        return {res, nullptr};
    } else {
        // Already have a table entry, check if the table entry is valid here.
        BaseEntry* header_base_entry = table_meta->entry_list_.front().get();
        if(header_base_entry->entry_type_ == EntryType::kDummy) {
            // Dummy entry in the header
            UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                       table_def->table_name(),
                                                                       table_def->columns(),
                                                                       table_meta,
                                                                       txn_id,
                                                                       begin_ts);
            res = table_entry.get();
            table_meta->entry_list_.emplace_front(std::move(table_entry));
            return {res, nullptr};
        }

        TableEntry* header_table_entry = (TableEntry*)header_base_entry;
        if(header_table_entry->commit_ts_ < UNCOMMIT_TS) {
            // Committed
            if(begin_ts > header_table_entry->commit_ts_) {
                if(header_table_entry->deleted_) {
                    // No conflict
                    UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                               table_def->table_name(),
                                                                               table_def->columns(),
                                                                               table_meta,
                                                                               txn_id,
                                                                               begin_ts);
                    res = table_entry.get();
                    table_meta->entry_list_.emplace_front(std::move(table_entry));
                    return {res, nullptr};
                } else {
                    // Duplicated table
                    LOG_TRACE("Duplicated table name {}.", table_name)
                    return {nullptr, MakeUnique<String>("Duplicated table.")};
                }
            } else {
                // Write-Write conflict
                LOG_TRACE("Write-write conflict: There is a committed table: {} which is later than current transaction.",
                          table_name)
                return {nullptr, MakeUnique<String>("Write-write conflict: There is a committed database which is later than current transaction.")};
            }
        } else {

            TxnState head_table_entry_state = txn_mgr->GetTxnState(header_table_entry->txn_id_);

            switch(head_table_entry_state) {
                case TxnState::kStarted: {
                    // Started
                    if(header_table_entry->txn_id_ == txn_id) {
                        // Same txn
                        if(header_table_entry->deleted_) {
                            UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                                       table_def->table_name(),
                                                                                       table_def->columns(),
                                                                                       table_meta,
                                                                                       txn_id,
                                                                                       begin_ts);
                            res = table_entry.get();
                            table_meta->entry_list_.emplace_front(std::move(table_entry));
                            return {res, nullptr};
                        } else {
                            LOG_TRACE("Create a duplicated table {}.", table_name)
                            return {nullptr, MakeUnique<String>("Create a duplicated name table.")};
                        }
                    } else {
                        LOG_TRACE("Write-write conflict: There is a uncommitted transaction.")
                        return {nullptr, MakeUnique<String>("Write-write conflict: There is a uncommitted transaction.")};
                    }
                }
                case TxnState::kCommitting:
                case TxnState::kCommitted: {
                    // Committing / Committed, report WW conflict and rollback current txn
                    LOG_TRACE("Write-write conflict: There is a committing/committed table which is later than current transaction.")
                    return {nullptr, MakeUnique<String>("Write-write conflict: There is a committing/committed table which is later than current transaction.")};
                }
                case TxnState::kRollbacking:
                case TxnState::kRollbacked: {
                    // Remove the header entry
                    table_meta->entry_list_.erase(table_meta->entry_list_.begin());

                    // Append new one
                    UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                               table_def->table_name(),
                                                                               table_def->columns(),
                                                                               table_meta,
                                                                               txn_id,
                                                                               begin_ts);
                    res = table_entry.get();
                    table_meta->entry_list_.emplace_front(std::move(table_entry));
                    return {res, nullptr};
                }
                default: {
                    LOG_TRACE("Invalid table entry txn state")
                    return {nullptr, MakeUnique<String>("Invalid table entry txn state.")};
                }
            }
        }
    }
}

EntryResult
TableMeta::DropNewEntry(TableMeta* table_meta,
                        u64 txn_id,
                        TxnTimeStamp begin_ts,
                        TxnManager* txn_mgr,
                        const String& table_name) {
    TableEntry* res = nullptr;
    std::unique_lock<RWMutex> rw_locker(table_meta->rw_locker_);
    if(table_meta->entry_list_.empty()) {
        LOG_TRACE("Empty table entry list.")
        return {nullptr, MakeUnique<String>("Empty table entry list.")};
    }

    BaseEntry* header_base_entry = table_meta->entry_list_.front().get();
    if(header_base_entry->entry_type_ == EntryType::kDummy) {
//            rw_locker_.unlock();
        LOG_TRACE("No valid table entry.")
        return {nullptr, MakeUnique<String>("No valid table entry.")};
    }

//    SharedPtr<String> entry_dir = MakeShared<String>(*current_dir_ + "/txn_" + std::to_string(txn_id));

    TableEntry* header_table_entry = (TableEntry*)header_base_entry;
    if(header_table_entry->commit_ts_ < UNCOMMIT_TS) {
        // Committed
        if(begin_ts > header_table_entry->commit_ts_) {
            // No conflict
            if(header_table_entry->deleted_) {
                LOG_TRACE("Table is dropped before.")
                return {nullptr, MakeUnique<String>("Table is dropped before.")};
            }

            Vector<SharedPtr<ColumnDef>> dummy_columns;

            SharedPtr<String> base_dir = MakeShared<String>(*table_meta->base_dir_ + '/' + *table_meta->table_name_);
            UniquePtr<TableEntry> table_entry = MakeUnique<TableEntry>(base_dir,
                                                                       table_meta->table_name_,
                                                                       dummy_columns,
                                                                       table_meta,
                                                                       txn_id,
                                                                       begin_ts);
            res = table_entry.get();
            res->deleted_ = true;
            table_meta->entry_list_.emplace_front(std::move(table_entry));

            return {res, nullptr};
        } else {
            // Write-Write conflict
            LOG_TRACE("Write-write conflict: There is a committed database which is later than current transaction.");
            return {nullptr, MakeUnique<String>("Write-write conflict: There is a committed database which is later than current transaction.")};
        }
    } else {
        // Uncommitted, check if the same txn
        if(txn_id == header_table_entry->txn_id_) {
            // Same txn, remove the header table entry
            res = header_table_entry;
            table_meta->entry_list_.erase(table_meta->entry_list_.begin());

            return {res, nullptr};
        } else {
            // Not same txn, issue WW conflict
            LOG_TRACE("Write-write conflict: There is another uncommitted table entry.")
            return {nullptr, MakeUnique<String>("Write-write conflict: There is another uncommitted table entry.")};
        }
    }
}

void
TableMeta::DeleteNewEntry(TableMeta* table_meta,
                          u64 txn_id,
                          TxnManager* txn_mgr) {
    std::unique_lock<RWMutex> rw_locker(table_meta->rw_locker_);
    if(table_meta->entry_list_.empty()) {
        LOG_TRACE("Empty table entry list.")
        return ;
    }

    auto removed_iter = std::remove_if(table_meta->entry_list_.begin(),
                                       table_meta->entry_list_.end(),
                                       [&](UniquePtr<BaseEntry>& entry)->bool {
        return entry->txn_id_ == txn_id;
    });

    table_meta->entry_list_.erase(removed_iter, table_meta->entry_list_.end());
}

EntryResult
TableMeta::GetEntry(TableMeta* table_meta,
                    u64 txn_id,
                    TxnTimeStamp begin_ts) {
    std::shared_lock<RWMutex> r_locker(table_meta->rw_locker_);
    if(table_meta->entry_list_.empty()) {
        LOG_TRACE("Empty table entry list.")
        return {nullptr, MakeUnique<String>("Empty table entry list.")};
    }


    for(const auto& table_entry: table_meta->entry_list_) {
        if(table_entry->entry_type_ == EntryType::kDummy) {
            LOG_TRACE("No valid table entry.")
            return {nullptr, MakeUnique<String>("No valid table entry.")};
        }

        if(table_entry->commit_ts_ < UNCOMMIT_TS) {
            // committed
            if(begin_ts > table_entry->commit_ts_) {
                if(table_entry->deleted_) {
                    LOG_TRACE("Table is dropped.")
                    return {nullptr, MakeUnique<String>("Table is dropped.")};
                } else {
                    return {table_entry.get(), nullptr};
                }
            }
        } else {
            // not committed
            if(txn_id == table_entry->txn_id_) {
                // same txn
                return {table_entry.get(), nullptr};
            }
        }
    }
    LOG_TRACE("No table entry found.")
    return {nullptr, MakeUnique<String>("No table entry found.")};
}

SharedPtr<String>
TableMeta::ToString(TableMeta* table_meta) {
    std::shared_lock<RWMutex> r_locker(table_meta->rw_locker_);
    SharedPtr<String> res = MakeShared<String>(fmt::format("TableMeta, base dir: {}, table name: {}, entry count: ",
                                                           *table_meta->base_dir_,
                                                           *table_meta->table_name_,
                                                           table_meta->entry_list_.size()));
    return res;
}

nlohmann::json
TableMeta::Serialize(const TableMeta* table_meta) {
    nlohmann::json json_res;

    json_res["base_dir"] = *table_meta->base_dir_;
    json_res["table_name"] = *table_meta->table_name_;

    for(const auto& entry: table_meta->entry_list_) {
        if(entry->entry_type_ == EntryType::kTable) {
            json_res["entries"].emplace_back(TableEntry::Serialize((TableEntry*)entry.get()));
        } else if(entry->entry_type_ == EntryType::kDummy) {
            LOG_TRACE("Skip dummy type entry during serialize table {} meta", *table_meta->table_name_);
        } else {
            StorageError("Unexpected entry type");
        }
    }

    return json_res;
}

UniquePtr<TableMeta>
TableMeta::Deserialize(const nlohmann::json& table_meta_json, DBEntry* db_entry, BufferManager* buffer_mgr) {
    nlohmann::json json_res;

    SharedPtr<String> base_dir = MakeShared<String>(table_meta_json["base_dir"]);
    SharedPtr<String> table_name = MakeShared<String>(table_meta_json["table_name"]);

    UniquePtr<TableMeta> res = MakeUnique<TableMeta>(base_dir, table_name, db_entry);
    UniquePtr<BaseEntry> dummy_entry = MakeUnique<BaseEntry>(EntryType::kDummy);
    dummy_entry->deleted_ = true;
    res->entry_list_.emplace_back(std::move(dummy_entry));
    if(table_meta_json.contains("entries")) {
        for(const auto& table_entry_json: table_meta_json["entries"]) {
            UniquePtr<TableEntry> table_entry = TableEntry::Deserialize(table_entry_json, res.get(), buffer_mgr);
            res->entry_list_.emplace_back(std::move(table_entry));
        }
    }

    return res;
}

}