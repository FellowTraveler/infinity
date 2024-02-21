// Copyright(C) 2023 InfiniFlow, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

module;

export module doc_iterator;

import stl;
import memory_pool;
import posting_iterator;
import index_defines;
import segment;
import index_config;
namespace infinity {
export class DocIterator {
public:
    DocIterator() = default;

    virtual ~DocIterator() = default;

    virtual void AddIterator(DocIterator *iter) = 0;

    virtual docid_t Doc() { return doc_id_; }

    // Check if the given doc id is a hit. If it is a hit, the
    // current doc id of this iterator is set to the given id,
    // If it is not a hit, the current doc id is either unchanged,
    // or set to the next hit
    bool Seek(docid_t doc_id) {
        if (doc_id > doc_id_)
            DoSeek(doc_id);
        return doc_id == doc_id_;
    }

    virtual void DoSeek(docid_t doc_id) = 0;

protected:
    docid_t doc_id_;
};
} // namespace infinity
