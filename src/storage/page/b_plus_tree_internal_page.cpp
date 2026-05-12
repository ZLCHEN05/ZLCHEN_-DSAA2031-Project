#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeInternalPage<int, page_id_t, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  SetParentPageId(INVALID_PAGE_ID);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  array_[index].first = key;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  array_[index].second = value;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  for (int i = 0; i < GetSize(); i++) {
    if (array_[i].second == value) {
      return i;
    }
  }
  return -1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  int left = 1;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator(key, array_[mid].first)) {
      right = mid;
    } else {
      left = mid + 1;
    }
  }
  return array_[left - 1].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) {
  array_[0].second = old_value;
  array_[1].first = key;
  array_[1].second = new_value;
  SetSize(2);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) -> int {
  int index = ValueIndex(old_value);
  if (index == -1) {
    return GetSize();
  }

  // Shift elements to the right to make room
  int current_size = GetSize();
  for (int i = current_size - 1; i > index; i--) {
    array_[i + 1] = array_[i];
  }

  // Insert the new key-value pair after the old_value
  array_[index + 1].first = key;
  array_[index + 1].second = new_value;
  IncreaseSize(1);

  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  for (int i = index; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() -> ValueType {
  ValueType only_child = array_[0].second;
  SetSize(0);
  return only_child;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int start_index = recipient->GetSize();
  recipient->array_[start_index].first = middle_key;
  recipient->array_[start_index].second = array_[0].second;
  for (int i = 1; i < GetSize(); i++) {
    recipient->array_[start_index + i].first = array_[i].first;
    recipient->array_[start_index + i].second = array_[i].second;
  }
  recipient->SetSize(start_index + GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int mid = GetSize() / 2;
  int move_count = GetSize() - mid;
  
  recipient->array_[0].second = array_[mid].second;
  for (int i = mid + 1; i < GetSize(); i++) {
    recipient->array_[i - mid].first = array_[i].first;
    recipient->array_[i - mid].second = array_[i].second;
  }
  recipient->SetSize(move_count);
  SetSize(mid);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  int recipient_size = recipient->GetSize();
  recipient->array_[recipient_size].first = middle_key;
  recipient->array_[recipient_size].second = array_[0].second;
  recipient->IncreaseSize(1);

  for (int i = 0; i < GetSize() - 1; i++) {
    array_[i].second = array_[i + 1].second;
  }
  for (int i = 1; i < GetSize() - 1; i++) {
    array_[i].first = array_[i + 1].first;
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  for (int i = recipient->GetSize(); i > 0; i--) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0].second = recipient->array_[1].second;
  recipient->array_[1].first = middle_key;
  recipient->array_[1].second = array_[GetSize() - 1].second;
  recipient->IncreaseSize(1);
  IncreaseSize(-1);
}

}  // namespace onebase
