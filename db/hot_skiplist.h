// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_DB_HOTSKIPLIST_H_
#define STORAGE_LEVELDB_DB_HOTSKIPLIST_H_

// Thread safety
// -------------
//
// Writes require external synchronization, most likely a mutex.
// Reads require a guarantee that the HotSkipList will not be destroyed
// while the read is in progress.  Apart from that, reads progress
// without any internal locking or synchronization.
//
// Invariants:
//
// (1) Allocated nodes are never deleted until the HotSkipList is
// destroyed.  This is trivially guaranteed by the code since we
// never delete any skip list nodes.
//
// (2) The contents of a Node except for the next/prev pointers are
// immutable after the Node has been linked into the HotSkipList.
// Only Insert() modifies the list, and it is careful to initialize
// a node and use release-stores to publish the nodes in one or
// more lists.
//
// ... prev vs. next pointer ordering ...

#include <atomic>
#include <cassert>
#include <cstdlib>

#include "util/arena.h"
#include "util/random.h"

namespace leveldb {
  
// Implementation details follow

template <typename Key>
class HotSkipList {
 public:
  struct Node;
  // Create a new HotSkipList object that will use "cmp" for comparing keys,
  // and will allocate memory using "*arena".  Objects allocated in the arena
  // must remain allocated for the lifetime of the HotSkipList object.
  HotSkipList();

  HotSkipList(const HotSkipList&) = delete;
  HotSkipList& operator=(const HotSkipList&) = delete;

  // Insert key into the list.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  Node* Insert(const Key key, Key value, Key tag);

  // Returns true iff an entry that compares equal to key is in the list.
  bool Contains(const Key key) const;

  // Iteration over the contents of a skip list
  class Iterator {
   public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(const HotSkipList* list);

    // Returns true iff the iterator is positioned at a valid node.
    bool Valid() const;

    // Returns the key at the current position.
    // REQUIRES: Valid()
    const Key key() const;

    // Advances to the next position.
    // REQUIRES: Valid()
    void Next();

    // Advances to the previous position.
    // REQUIRES: Valid()
    void Prev();

    // Advance to the first entry with a key >= target
    void Seek(const Key target);

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToFirst();

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToLast();

   private:
    const HotSkipList* list_;
    Node* node_;

    // Intentionally copyable
  };

 public:
  enum { kMaxHeight = 12 };

  inline int GetMaxHeight() const {
    return max_height_.load(std::memory_order_relaxed);
  }

  inline int Compare(const Key a, const Key b) const {
    return memcmp(a->data(), b->data(), a->size());
  }

  Node* NewNode(const Key key, Key value, Key tag, int height);

  int RandomHeight();
  bool Equal(const Key a, const Key b) const { return (Compare(a, b) == 0); }

  // Return true if key is greater than the data stored in "n"
  bool KeyIsAfterNode(const Key key, Node* n) const;

  // Return the earliest node that comes at or after key.
  // Return nullptr if there is no such node.
  //
  // If prev is non-null, fills prev[level] with pointer to previous
  // node at "level" for every level in [0..max_height_-1].
  Node* FindGreaterOrEqual(const Key key, Node** prev) const;

  // Return the latest node with a key < key.
  // Return head_ if there is no such node.
  Node* FindLessThan(const Key key) const;

  // Return the last node in the list.
  // Return head_ if list is empty.
  Node* FindLast() const;

  // Immutable after construction
  Node* const head_;

  // Modified only by Insert().  Read racily by readers, but stale
  // values are ok.
  std::atomic<int> max_height_;  // Height of the entire list

  // Read/written only by Insert().
  Random rnd_;
  int duplicate_cnt=0;
};

template <typename Key>
struct HotSkipList<Key>::Node {
  explicit Node(const Key k, const Key v, const Key t, int height) : key(k), val_(v), tag_(t) {
      next_ = (std::atomic<Node*>*)malloc(sizeof(std::atomic<Node*>)*(height));
  }

  Key const key;
  Key val_;
  Key tag_;

  // Accessors/mutators for links.  Wrapped in methods so we can
  // add the appropriate barriers as necessary.
  Node* Next(int n) {
    assert(n >= 0);
    // Use an 'acquire load' so that we observe a fully initialized
    // version of the returned Node.
    return next_[n].load(std::memory_order_acquire);
  }
  void SetNext(int n, Node* x) {
    assert(n >= 0);
    // Use a 'release store' so that anybody who reads through this
    // pointer observes a fully initialized version of the inserted node.
    next_[n].store(x, std::memory_order_release);
  }

  // No-barrier variants that can be safely used in a few locations.
  Node* NoBarrier_Next(int n) {
    assert(n >= 0);
    return next_[n].load(std::memory_order_relaxed);
  }
  void NoBarrier_SetNext(int n, Node* x) {
    assert(n >= 0);
    next_[n].store(x, std::memory_order_relaxed);
  }

 private:
  // Array of length equal to the node height.  next_[0] is lowest level link.
  std::atomic<Node*>* next_;
};

template <typename Key>
typename HotSkipList<Key>::Node* HotSkipList<Key>::NewNode(
    const Key key, Key value, Key tag, int height) {
  Node* node = new Node(key, value, tag, height);
  return node;
}

template <typename Key>
inline HotSkipList<Key>::Iterator::Iterator(const HotSkipList* list) {
  list_ = list;
  node_ = nullptr;
}

template <typename Key>
inline bool HotSkipList<Key>::Iterator::Valid() const {
  return node_ != nullptr;
}

template <typename Key>
inline const Key HotSkipList<Key>::Iterator::key() const {
  assert(Valid());
  return node_->key;
}

template <typename Key>
inline void HotSkipList<Key>::Iterator::Next() {
  assert(Valid());
  node_ = node_->Next(0);
}

template <typename Key>
inline void HotSkipList<Key>::Iterator::Prev() {
  // Instead of using explicit "prev" links, we just search for the
  // last node that falls before key.
  assert(Valid());
  node_ = list_->FindLessThan(node_->key);
  if (node_ == list_->head_) {
    node_ = nullptr;
  }
}

template <typename Key>
inline void HotSkipList<Key>::Iterator::Seek(const Key target) {
  node_ = list_->FindGreaterOrEqual(target, nullptr);
}

template <typename Key>
inline void HotSkipList<Key>::Iterator::SeekToFirst() {
  node_ = list_->head_->Next(0);
}

template <typename Key>
inline void HotSkipList<Key>::Iterator::SeekToLast() {
  node_ = list_->FindLast();
  if (node_ == list_->head_) {
    node_ = nullptr;
  }
}

template <typename Key>
int HotSkipList<Key>::RandomHeight() {
  // Increase height with probability 1 in kBranching
  static const unsigned int kBranching = 4;
  int height = 1;
  while (height < kMaxHeight && rnd_.OneIn(kBranching)) {
    height++;
  }
  assert(height > 0);
  assert(height <= kMaxHeight);
  return height;
}

template <typename Key>
bool HotSkipList<Key>::KeyIsAfterNode(const Key key, Node* n) const {
  // null n is considered infinite
  return (n != nullptr) && (Compare(n->key, key) < 0);
}

template <typename Key>
typename HotSkipList<Key>::Node*
HotSkipList<Key>::FindGreaterOrEqual(const Key key,
                                              Node** prev) const {
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  while (true) {
    Node* next = x->Next(level);
    if (KeyIsAfterNode(key, next)) {
      // Keep searching in this list
      x = next;
    } else {
      if (prev != nullptr) prev[level] = x;
      if (level == 0) {
        return next;
      } else {
        // Switch to next list
        level--;
      }
    }
  }
}

template <typename Key>
typename HotSkipList<Key>::Node*
HotSkipList<Key>::FindLessThan(const Key key) const {
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  while (true) {
    assert(x == head_ || Compare(x->key, key) < 0);
    Node* next = x->Next(level);
    if (next == nullptr || Compare(next->key, key) >= 0) {
      if (level == 0) {
        return x;
      } else {
        // Switch to next list
        level--;
      }
    } else {
      x = next;
    }
  }
}

template <typename Key>
typename HotSkipList<Key>::Node* HotSkipList<Key>::FindLast()
    const {
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  while (true) {
    Node* next = x->Next(level);
    if (next == nullptr) {
      if (level == 0) {
        return x;
      } else {
        // Switch to next list
        level--;
      }
    } else {
      x = next;
    }
  }
}

template <typename Key>
HotSkipList<Key>::HotSkipList()
    : head_(NewNode(Key(), Key(), Key(), kMaxHeight)),
      max_height_(1),
      rnd_(0xdeadbeef) {
  for (int i = 0; i < kMaxHeight; i++) {
    head_->SetNext(i, nullptr);
  }
}

template <typename Key>
typename HotSkipList<Key>::Node* HotSkipList<Key>::Insert(const Key key, Key value, Key tag) {
  // TODO(opt): We can use a barrier-free variant of FindGreaterOrEqual()
  // here since Insert() is externally synchronized.
  Node* prev[kMaxHeight];
  Node* x = FindGreaterOrEqual(key, prev);

  // Our data structure does not allow duplicate insertion
  if (!(x == nullptr || !Equal(key, x->key)))
    return nullptr;
    // printf("HotSkiplist duplicat_cnt: %d\n", duplicate_cnt++);
    
  // assert(x == nullptr || !Equal(key, x->key));

  int height = RandomHeight();
  if (height > GetMaxHeight()) {
    for (int i = GetMaxHeight(); i < height; i++) {
      prev[i] = head_;
    }
    // It is ok to mutate max_height_ without any synchronization
    // with concurrent readers.  A concurrent reader that observes
    // the new value of max_height_ will see either the old value of
    // new level pointers from head_ (nullptr), or a new value set in
    // the loop below.  In the former case the reader will
    // immediately drop to the next level since nullptr sorts after all
    // keys.  In the latter case the reader will use the new node.
    max_height_.store(height, std::memory_order_relaxed);
  }

  x = NewNode(key, value, tag, height);
  for (int i = 0; i < height; i++) {
    // NoBarrier_SetNext() suffices since we will add a barrier when
    // we publish a pointer to "x" in prev[i].
    x->NoBarrier_SetNext(i, prev[i]->NoBarrier_Next(i));
    prev[i]->SetNext(i, x);
  }
  return x;
}

template <typename Key>
bool HotSkipList<Key>::Contains(const Key key) const {
  Node* x = FindGreaterOrEqual(key, nullptr);
  if (x != nullptr && Equal(key, x->key)) {
    return true;
  } else {
    return false;
  }
}
}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_HotSkipList_H_