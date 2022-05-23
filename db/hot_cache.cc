#include "db/hot_cache.h"
#include "db/memtable.h"
#include "db/dbformat.h"
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "util/coding.h"

#include "db/hot_skiplist.h"
#include "leveldb/db.h"
#include "util/arena.h"
#include <unordered_map>


namespace leveldb {
const inline long GB = 1024*1024*1024;

bool HotCache::UpdateIfExist(SequenceNumber s, ValueType type, const Slice& key, const Slice& value){
  auto it = hash_map_->find(key.ToString());
  put_cnt_++;
  if(it != hash_map_->end()){
    hit_cnt_++;
    HotNode* node = it->second;
    const size_t value_size = value.size();
    EncodeFixed64((char*)node->tag_->data(), (s << 8) | type);
    
    if (type == kTypeDeletion){
      node->val_ = nullptr;
    } else if (value_size == node->val_->size()) {
      std::memcpy((char*)node->val_->data(), value.data(), value_size);
    } else {
      bytes_ += value_size - node->val_->size();
      free((char*)node->val_->data());
      delete (node->val_);

      char* nval_data = (char*)malloc(value_size);
      std::memcpy(nval_data, value.data(), value_size);
      it->second->val_= new Slice(nval_data, value_size);  
    }
    return true;
  }
  return false;
}

void HotCache::InsertFromCompaction(const Slice& key, const Slice& value){
  // key_buf format: [klength - userkey - tag]
  // klength = VarintLength(key.size())
  // userkey = key.data()[:-8]
  // tag = key.data()[-8:]

  // val_buf format: [vlength - userval]
  // vlength = VarintLength(value.size())

  // dbformat.h - ParseInternalKey
  // uint8_t c = num & 0xff;
  // const uint64_t sequence = num >> 8;

  const size_t ikey_size = key.size() - 8;
  const size_t val_size = value.size();
  
  char* ikey_data = (char*)malloc(ikey_size);
  char* val_data = (char*)malloc(val_size);
  char* tag_data = (char*)malloc(8);
  
  std::memcpy(ikey_data, key.data(), ikey_size);
  std::memcpy(val_data, value.data(), val_size);
  std::memcpy(tag_data, key.data() + ikey_size, 8);
  
  Slice* ikey = new Slice(ikey_data, ikey_size);
  Slice* val = new Slice(val_data, val_size);
  Slice* tag = new Slice(tag_data, 8);

  bytes_ += ikey_size + val_size + 8;
  
  HotNode* node = hot_table_->Insert(ikey, val, tag);

  if (node != nullptr)
    // for hash_map key_type: const char*(Slice *), not std::string
    hash_map_->insert({ikey->ToString(), node});
    // lru->insert(ikey_data, node);
}

  void HotCache::PrintCacheInfo() const {
    printf("Cache Size: %.3llfGB\n", (double long)bytes_/(double long)GB);
    printf("Hit Ratio: %.3llf% (%ld/%ld)\n", ((double long)hit_cnt_/(double long)put_cnt_), hit_cnt_, put_cnt_);
  };
}