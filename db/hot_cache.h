#ifndef STORAGE_LEVELDB_DB_HOTCACHE_H_
#define STORAGE_LEVELDB_DB_HOTCACHE_H_

#include "db/memtable.h"
#include "db/dbformat.h"
#include "db/memtable.h"
#include "hot_skiplist.h"
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "util/coding.h"
#include "db/skiplist.h"
#include "leveldb/db.h"
#include <unordered_map>
#include <functional>

namespace leveldb {
// have to consider unordered_map type and data type: node vs array
// for hash_map key_type: const char*(Slice *), not std::string
// typedef robin_hood::unordered_map<const Slice&, HotNode*, hash_func, equal_func> HotHash;
typedef HotSkipList<Slice*> HotTable;
typedef HotSkipList<Slice*>::Node HotNode;
typedef std::unordered_map<std::string, HotNode*> HotHash;

class HotCache {
 public:
    HotCache() {
        hot_table_ = new HotTable();
        hash_map_ = new HotHash();
        //hot_lru = new HotLRU();
    };

    void InsertFromCompaction(const Slice& key, const Slice& value);
    bool UpdateIfExist(SequenceNumber s, ValueType type, const Slice& key, const Slice& value);
    void PrintCacheInfo() const;
 private:
    ~HotCache();

    long bytes_ = 0;
    long put_cnt_ = 0;
    long hit_cnt_ = 0;
    
    HotTable* hot_table_;
    HotHash* hash_map_;
    // HotLRU hot_lru;
};

// for hash_map key_type: const char*(Slice *), not std::string
struct equal_func {
    bool operator() (Slice const &a, Slice const &b) const {
        if (a.size() == b.size()){
          return (memcmp(a.data(), b.data(), a.size()) == 0);
        } else {
          return false;
        }
    }
};

struct hash_func {
    size_t operator() (Slice const &s) const {
        return std::hash<const char *>{}(s.data());
    }
};

}
#endif  // STORAGE_LEVELDB_DB_HOTCACHE_H_