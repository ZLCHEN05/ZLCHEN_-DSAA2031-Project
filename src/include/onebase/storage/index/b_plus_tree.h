#pragma once
#include <functional>
#include <string>
#include <vector>
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/rid.h"
#include "onebase/storage/index/index.h"
#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeIterator;

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator>

template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTree : public Index {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;

 public:
  BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
            int leaf_max_size = 0, int internal_max_size = 0);

  auto IsEmpty() const -> bool;
  auto Insert(const KeyType &key, const ValueType &value) -> bool;
  void Remove(const KeyType &key);
  auto GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool;

  auto GetRootPageId() const -> page_id_t { return root_page_id_; }

  using Iterator = BPlusTreeIterator<KeyType, ValueType, KeyComparator>;
  auto Begin() -> Iterator;
  auto Begin(const KeyType &key) -> Iterator;
  auto End() -> Iterator;

 private:
  void InsertIntoParent(page_id_t parent_page_id, page_id_t old_page_id,
                        const KeyType &key, page_id_t new_page_id);

  template <typename N>
  auto CoalesceOrRedistribute(N *node, WritePageGuard *guard) -> bool;

  template <typename N>
  auto Coalesce(N *neighbor_node, N *node, BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent,
                int index) -> bool;

  template <typename N>
  void Redistribute(N *neighbor_node, N *node, int index, bool is_predecessor);

  auto AdjustRoot(BPlusTreePage *old_root_node) -> bool;

  BufferPoolManager *bpm_;
  KeyComparator comparator_;
  page_id_t root_page_id_{INVALID_PAGE_ID};
  int leaf_max_size_;
  int internal_max_size_;
};

}  // namespace onebase
