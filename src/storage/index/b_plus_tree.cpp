#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/common/exception.h"
#include "onebase/buffer/page_guard.h"
#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template class BPlusTree<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::AdjustRoot(BPlusTreePage *old_root_node) -> bool {
  if (old_root_node->IsLeafPage()) {
    if (old_root_node->GetSize() == 0) {
      root_page_id_ = INVALID_PAGE_ID;
      return true;
    }
    return false;
  }

  auto internal_root = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(old_root_node);
  if (internal_root->GetSize() == 1) {
    page_id_t new_root_id = internal_root->RemoveAndReturnOnlyChild();
    root_page_id_ = new_root_id;

    auto new_root_guard = bpm_->FetchPageWrite(new_root_id);
    auto new_root = new_root_guard.AsMut<BPlusTreePage>();
    new_root->SetParentPageId(INVALID_PAGE_ID);

    return true;
  }

  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  if (IsEmpty()) {
    page_id_t new_page_id;
    auto guard = bpm_->NewPageGuarded(&new_page_id);
    auto leaf = guard.AsMut<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
    leaf->Init(leaf_max_size_);
    leaf->Insert(key, value, comparator_);
    this->root_page_id_ = new_page_id;
    return true;
  }

  page_id_t page_id = root_page_id_;
  auto guard = bpm_->FetchPageWrite(page_id);

  while (true) {
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      break;
    }
    auto internal = guard.As<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    page_id = internal->Lookup(key, comparator_);
    guard = bpm_->FetchPageWrite(page_id);
  }

  auto leaf = guard.AsMut<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
  page_id_t leaf_page_id = page_id;
  int old_size = leaf->GetSize();
  int new_size = leaf->Insert(key, value, comparator_);

  if (new_size == old_size) {
    return false;
  }

  if (new_size > leaf->GetMaxSize()) {
    page_id_t new_page_id;
    auto new_guard = bpm_->NewPageGuarded(&new_page_id);
    auto new_leaf = new_guard.AsMut<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
    new_leaf->Init(leaf_max_size_);

    leaf->MoveHalfTo(new_leaf);
    new_leaf->SetNextPageId(leaf->GetNextPageId());
    leaf->SetNextPageId(new_page_id);

    KeyType middle_key = new_leaf->KeyAt(0);

    if (leaf->IsRootPage()) {
      page_id_t new_root_id;
      auto root_guard = bpm_->NewPageGuarded(&new_root_id);
      auto new_root = root_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
      new_root->Init(internal_max_size_);
      new_root->PopulateNewRoot(leaf_page_id, middle_key, new_page_id);

      leaf->SetParentPageId(new_root_id);
      new_leaf->SetParentPageId(new_root_id);
      root_page_id_ = new_root_id;
    } else {
      page_id_t parent_page_id = leaf->GetParentPageId();
      new_leaf->SetParentPageId(parent_page_id);

      guard.Drop();
      new_guard.Drop();

      InsertIntoParent(parent_page_id, leaf_page_id, middle_key, new_page_id);
    }
  }

  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::InsertIntoParent(page_id_t parent_page_id, page_id_t old_page_id,
                                       const KeyType &key, page_id_t new_page_id) {
  auto parent_guard = bpm_->FetchPageWrite(parent_page_id);
  auto parent = parent_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();

  int new_size = parent->InsertNodeAfter(old_page_id, key, new_page_id);

  if (new_size > parent->GetMaxSize()) {
    page_id_t new_parent_id;
    auto new_parent_guard = bpm_->NewPageGuarded(&new_parent_id);
    auto new_parent = new_parent_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    new_parent->Init(internal_max_size_);

    int mid = parent->GetSize() / 2;
    KeyType middle_key = parent->KeyAt(mid);

    parent->MoveHalfTo(new_parent, middle_key);

    for (int i = 0; i < new_parent->GetSize(); i++) {
      page_id_t child_page_id = new_parent->ValueAt(i);
      auto child_guard = bpm_->FetchPageWrite(child_page_id);
      auto child = child_guard.AsMut<BPlusTreePage>();
      child->SetParentPageId(new_parent_id);
    }

    if (parent->IsRootPage()) {
      page_id_t new_root_id;
      auto root_guard = bpm_->NewPageGuarded(&new_root_id);
      auto new_root = root_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
      new_root->Init(internal_max_size_);
      new_root->PopulateNewRoot(parent_page_id, middle_key, new_parent_id);

      parent->SetParentPageId(new_root_id);
      new_parent->SetParentPageId(new_root_id);
      root_page_id_ = new_root_id;
    } else {
      page_id_t grandparent_page_id = parent->GetParentPageId();
      new_parent->SetParentPageId(grandparent_page_id);

      parent_guard.Drop();
      new_parent_guard.Drop();

      InsertIntoParent(grandparent_page_id, parent_page_id, middle_key, new_parent_id);
    }
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (IsEmpty()) {
    return;
  }

  page_id_t page_id = root_page_id_;
  auto guard = bpm_->FetchPageWrite(page_id);

  while (true) {
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      break;
    }
    auto internal = guard.As<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    page_id = internal->Lookup(key, comparator_);
    guard = bpm_->FetchPageWrite(page_id);
  }

  auto leaf = guard.AsMut<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
  int old_size = leaf->GetSize();
  int new_size = leaf->RemoveAndDeleteRecord(key, comparator_);

  if (new_size == old_size) {
    return;
  }

  if (new_size < leaf->GetMinSize()) {
    CoalesceOrRedistribute(leaf, &guard);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
template <typename N>
auto BPLUSTREE_TYPE::CoalesceOrRedistribute(N *node, WritePageGuard *guard) -> bool {
  if (node->IsRootPage()) {
    return AdjustRoot(node);
  }

  if (node->GetSize() >= node->GetMinSize()) {
    return false;
  }

  page_id_t node_page_id = guard->GetPageId();
  page_id_t parent_page_id = node->GetParentPageId();
  auto parent_guard = bpm_->FetchPageWrite(parent_page_id);
  auto parent = parent_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();

  int node_index = parent->ValueIndex(node_page_id);
  if (node_index == -1) {
    return false;
  }

  page_id_t sibling_page_id;
  int sibling_index;
  bool is_predecessor;

  if (node_index > 0) {
    sibling_index = node_index - 1;
    sibling_page_id = parent->ValueAt(sibling_index);
    is_predecessor = true;
  } else {
    sibling_index = node_index + 1;
    sibling_page_id = parent->ValueAt(sibling_index);
    is_predecessor = false;
  }

  auto sibling_guard = bpm_->FetchPageWrite(sibling_page_id);
  auto sibling = sibling_guard.AsMut<N>();

  if (node->GetSize() + sibling->GetSize() <= node->GetMaxSize()) {
    if (is_predecessor) {
      int parent_index = sibling_index;
      bool parent_should_delete = Coalesce(sibling, node, parent, parent_index);
      guard->Drop();
      sibling_guard.Drop();
      if (parent_should_delete) {
        return CoalesceOrRedistribute(parent, &parent_guard);
      }
    } else {
      int parent_index = node_index;
      bool parent_should_delete = Coalesce(node, sibling, parent, parent_index);
      guard->Drop();
      sibling_guard.Drop();
      if (parent_should_delete) {
        return CoalesceOrRedistribute(parent, &parent_guard);
      }
    }
  } else {
    Redistribute(sibling, node, node_index, is_predecessor);
  }

  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
template <typename N>
auto BPLUSTREE_TYPE::Coalesce(N *neighbor_node, N *node,
                               BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *parent,
                               int index) -> bool {
  KeyType middle_key = parent->KeyAt(index + 1);

  if (node->IsLeafPage()) {
    auto leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(node);
    auto neighbor_leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(neighbor_node);
    leaf_node->MoveAllTo(neighbor_leaf);
    neighbor_leaf->SetNextPageId(leaf_node->GetNextPageId());
  } else {
    auto internal_node = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(node);
    auto neighbor_internal = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(neighbor_node);
    internal_node->MoveAllTo(neighbor_internal, middle_key);

    for (int i = 0; i < neighbor_internal->GetSize(); i++) {
      page_id_t child_page_id = neighbor_internal->ValueAt(i);
      auto child_guard = bpm_->FetchPageWrite(child_page_id);
      auto child = child_guard.AsMut<BPlusTreePage>();
      child->SetParentPageId(neighbor_internal->GetParentPageId());
    }
  }

  parent->Remove(index + 1);

  return parent->GetSize() < parent->GetMinSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
template <typename N>
void BPLUSTREE_TYPE::Redistribute(N *neighbor_node, N *node, int index, bool is_predecessor) {
  page_id_t parent_page_id = node->GetParentPageId();
  auto parent_guard = bpm_->FetchPageWrite(parent_page_id);
  auto parent = parent_guard.AsMut<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();

  if (node->IsLeafPage()) {
    auto leaf_node = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(node);
    auto neighbor_leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(neighbor_node);

    if (is_predecessor) {
      neighbor_leaf->MoveLastToFrontOf(leaf_node);
      parent->SetKeyAt(index, leaf_node->KeyAt(0));
    } else {
      neighbor_leaf->MoveFirstToEndOf(leaf_node);
      parent->SetKeyAt(index + 1, neighbor_leaf->KeyAt(0));
    }
  } else {
    auto internal_node = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(node);
    auto neighbor_internal = reinterpret_cast<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator> *>(neighbor_node);

    if (is_predecessor) {
      KeyType middle_key = parent->KeyAt(index);
      neighbor_internal->MoveLastToFrontOf(internal_node, middle_key);
      parent->SetKeyAt(index, internal_node->KeyAt(1));

      page_id_t child_page_id = internal_node->ValueAt(0);
      auto child_guard = bpm_->FetchPageWrite(child_page_id);
      auto child = child_guard.AsMut<BPlusTreePage>();
      child->SetParentPageId(internal_node->GetParentPageId());
    } else {
      KeyType middle_key = parent->KeyAt(index + 1);
      neighbor_internal->MoveFirstToEndOf(internal_node, middle_key);
      parent->SetKeyAt(index + 1, neighbor_internal->KeyAt(1));

      page_id_t child_page_id = internal_node->ValueAt(internal_node->GetSize() - 1);
      auto child_guard = bpm_->FetchPageWrite(child_page_id);
      auto child = child_guard.AsMut<BPlusTreePage>();
      child->SetParentPageId(internal_node->GetParentPageId());
    }
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  if (IsEmpty()) {
    return false;
  }

  page_id_t page_id = root_page_id_;
  auto guard = bpm_->FetchPageRead(page_id);

  while (true) {
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      break;
    }
    auto internal = guard.As<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    page_id = internal->Lookup(key, comparator_);
    guard = bpm_->FetchPageRead(page_id);
  }

  auto leaf = guard.As<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
  ValueType value;
  if (leaf->Lookup(key, &value, comparator_)) {
    result->push_back(value);
    return true;
  }
  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  if (IsEmpty()) {
    return End();
  }
  page_id_t page_id = root_page_id_;
  auto guard = bpm_->FetchPageRead(page_id);
  while (true) {
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      break;
    }
    auto internal = guard.As<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    page_id = internal->ValueAt(0);
    guard = bpm_->FetchPageRead(page_id);
  }
  return Iterator(page_id, 0, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  auto guard = bpm_->FetchPageRead(page_id);

  while (true) {
    auto page = guard.As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      break;
    }
    auto internal = guard.As<BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>>();
    page_id = internal->Lookup(key, comparator_);
    guard = bpm_->FetchPageRead(page_id);
  }

  auto leaf = guard.As<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>>();
  int index = leaf->KeyIndex(key, comparator_);
  if (index >= leaf->GetSize()) {
    return End();
  }
  return Iterator(page_id, index, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0, nullptr);
}

}  // namespace onebase
