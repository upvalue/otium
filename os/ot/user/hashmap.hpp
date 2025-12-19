// hashmap.hpp - Generic hash table with open addressing and linear probing
// Uses ou_malloc/ou_free for freestanding environments

#ifndef OT_USER_HASHMAP_HPP
#define OT_USER_HASHMAP_HPP

#include "ot/common.h"
#include "ot/user/string.hpp"

namespace ou {

// djb2 hash function - simple and effective
inline uint32_t hash_string(const char *str, size_t len) {
  uint32_t hash = 5381;
  for (size_t i = 0; i < len; i++) {
    hash = ((hash << 5) + hash) + str[i]; // hash * 33 + c
  }
  return hash;
}

inline uint32_t hash_string(const char *str) { return hash_string(str, strlen(str)); }

/**
 * Generic open-addressing hash table with linear probing and dynamic growth
 * Template parameters:
 *   V - Value type
 *   INITIAL_CAPACITY - Initial number of slots (must be power of 2 for fast modulo)
 */
template <typename V, size_t INITIAL_CAPACITY = 16> class StringHashMap {
private:
  struct Entry {
    const char *key; // Pointer to key string (not owned)
    size_t key_len;
    V value;
    bool occupied;

    Entry() : key(nullptr), key_len(0), value(), occupied(false) {}
  };

  Entry *table_;
  size_t capacity_;
  size_t count_;

  // Ensure initial capacity is power of 2 for fast modulo
  static_assert((INITIAL_CAPACITY & (INITIAL_CAPACITY - 1)) == 0, "INITIAL_CAPACITY must be a power of 2");

  size_t hash_index(const char *key, size_t len) const { return hash_string(key, len) & (capacity_ - 1); }

  bool keys_equal(const char *k1, size_t len1, const char *k2, size_t len2) const {
    return len1 == len2 && memcmp(k1, k2, len1) == 0;
  }

  void resize(size_t new_capacity) {
    // Allocate new table
    Entry *new_table = (Entry *)ou_malloc(new_capacity * sizeof(Entry));
    if (!new_table) {
      return; // Allocation failed - keep old table
    }

    // Initialize new table
    for (size_t i = 0; i < new_capacity; i++) {
      new (&new_table[i]) Entry();
    }

    // Rehash all entries from old table to new table
    Entry *old_table = table_;
    size_t old_capacity = capacity_;

    table_ = new_table;
    capacity_ = new_capacity;
    count_ = 0;

    // Reinsert all entries
    for (size_t i = 0; i < old_capacity; i++) {
      if (old_table[i].occupied) {
        insert(old_table[i].key, old_table[i].key_len, old_table[i].value);
      }
    }

    // Free old table
    if (old_table) {
      for (size_t i = 0; i < old_capacity; i++) {
        old_table[i].~Entry();
      }
      ou_free(old_table);
    }
  }

public:
  StringHashMap() : table_(nullptr), capacity_(INITIAL_CAPACITY), count_(0) {
    table_ = (Entry *)ou_malloc(capacity_ * sizeof(Entry));
    if (table_) {
      for (size_t i = 0; i < capacity_; i++) {
        new (&table_[i]) Entry();
      }
    }
  }

  ~StringHashMap() {
    if (table_) {
      for (size_t i = 0; i < capacity_; i++) {
        table_[i].~Entry();
      }
      ou_free(table_);
    }
  }

  // Disable copy (could implement if needed)
  StringHashMap(const StringHashMap &) = delete;
  StringHashMap &operator=(const StringHashMap &) = delete;

  size_t size() const { return count_; }

  size_t capacity() const { return capacity_; }

  bool empty() const { return count_ == 0; }

  // Insert or update entry
  // key must remain valid for the lifetime of the hash map (we store pointer, not copy)
  bool insert(const char *key, size_t key_len, const V &value) {
    // Check if we need to grow
    if (count_ >= capacity_ * 3 / 4) {
      // Grow by 2x
      resize(capacity_ * 2);
    }

    size_t idx = hash_index(key, key_len);

    // Linear probe to find empty slot or existing key
    for (size_t i = 0; i < capacity_; i++) {
      size_t probe_idx = (idx + i) & (capacity_ - 1);
      Entry &e = table_[probe_idx];

      if (!e.occupied) {
        // Empty slot - insert here
        e.key = key;
        e.key_len = key_len;
        e.value = value;
        e.occupied = true;
        count_++;
        return true;
      } else if (keys_equal(e.key, e.key_len, key, key_len)) {
        // Key exists - update value
        e.value = value;
        return true;
      }
    }

    // Table full (shouldn't happen after resize)
    return false;
  }

  bool insert(const string &key, const V &value) { return insert(key.c_str(), key.length(), value); }

  bool insert(const char *key, const V &value) { return insert(key, strlen(key), value); }

  // Lookup entry
  V *find(const char *key, size_t key_len) {
    if (!table_) {
      return nullptr;
    }

    size_t idx = hash_index(key, key_len);

    // Linear probe
    for (size_t i = 0; i < capacity_; i++) {
      size_t probe_idx = (idx + i) & (capacity_ - 1);
      Entry &e = table_[probe_idx];

      if (!e.occupied) {
        // Hit empty slot - key not found
        return nullptr;
      } else if (keys_equal(e.key, e.key_len, key, key_len)) {
        // Found it
        return &e.value;
      }
    }

    // Probed entire table - not found
    return nullptr;
  }

  const V *find(const char *key, size_t key_len) const {
    return const_cast<StringHashMap *>(this)->find(key, key_len);
  }

  V *find(const string &key) { return find(key.c_str(), key.length()); }

  const V *find(const string &key) const { return find(key.c_str(), key.length()); }

  V *find(const char *key) { return find(key, strlen(key)); }

  const V *find(const char *key) const { return find(key, strlen(key)); }

  // Remove entry (optional - not needed for command lookup)
  bool remove(const char *key, size_t key_len) {
    if (!table_) {
      return false;
    }

    size_t idx = hash_index(key, key_len);

    for (size_t i = 0; i < capacity_; i++) {
      size_t probe_idx = (idx + i) & (capacity_ - 1);
      Entry &e = table_[probe_idx];

      if (!e.occupied) {
        return false; // Not found
      } else if (keys_equal(e.key, e.key_len, key, key_len)) {
        // Found - mark as unoccupied
        // NOTE: This creates tombstones and can degrade performance
        // For production use, consider rehashing or using a different deletion strategy
        e.occupied = false;
        count_--;
        return true;
      }
    }

    return false;
  }

  void clear() {
    if (!table_) {
      return;
    }
    for (size_t i = 0; i < capacity_; i++) {
      table_[i].occupied = false;
    }
    count_ = 0;
  }
};

} // namespace ou

#endif
