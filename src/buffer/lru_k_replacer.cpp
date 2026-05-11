#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"
#include <stdexcept>

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  frame_id_t victim_frame = -1;
  size_t max_k_distance = 0;
  size_t earliest_timestamp = SIZE_MAX;
  bool found_inf_frame = false;

  for (auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }

    if (entry.history_.size() < k_) {
      // +infinity frame
      size_t first_access = entry.history_.front();
      if (!found_inf_frame || first_access < earliest_timestamp) {
        victim_frame = fid;
        earliest_timestamp = first_access;
        found_inf_frame = true;
      }
    } else if (!found_inf_frame) {
      // Finite k-distance frame (only consider if no +inf frames found)
      size_t k_distance = current_timestamp_ - entry.history_.front();
      if (victim_frame == -1 || k_distance > max_k_distance) {
        victim_frame = fid;
        max_k_distance = k_distance;
      }
    }
  }

  if (victim_frame == -1) {
    return false;
  }

  *frame_id = victim_frame;
  entries_.erase(victim_frame);
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_);

  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }

  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  auto &entry = it->second;
  if (entry.is_evictable_ && !set_evictable) {
    curr_size_--;
  } else if (!entry.is_evictable_ && set_evictable) {
    curr_size_++;
  }

  entry.is_evictable_ = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard<std::mutex> lock(latch_);

  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_) {
    throw std::runtime_error("Cannot remove non-evictable frame");
  }

  curr_size_--;
  entries_.erase(it);
}

auto LRUKReplacer::Size() const -> size_t {
  return curr_size_;
}

}  // namespace onebase
