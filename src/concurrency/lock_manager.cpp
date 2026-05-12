#include "onebase/concurrency/lock_manager.h"
#include "onebase/common/exception.h"

namespace onebase {

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  // 1. Check transaction state
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }

  // 2. Check if lock already held
  if (txn->IsSharedLocked(rid) || txn->IsExclusiveLocked(rid)) {
    return true;
  }

  // 3. Get or create lock request queue
  auto &queue = lock_table_[rid];

  // 4. Add SHARED request to queue
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::SHARED);

  // 5. Wait until lock can be granted
  queue.cv_.wait(lock, [&]() {
    // Check if transaction was aborted while waiting
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }

    // Check for granted EXCLUSIVE locks
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.lock_mode_ == LockMode::EXCLUSIVE) {
        return false;
      }
    }
    return true;
  });

  // 6. Check if aborted during wait
  if (txn->GetState() == TransactionState::ABORTED) {
    // Remove request from queue
    queue.request_queue_.remove_if([txn](const LockRequest &req) {
      return req.txn_id_ == txn->GetTransactionId() && !req.granted_;
    });
    return false;
  }

  // 7. Mark request as granted
  for (auto &req : queue.request_queue_) {
    if (req.txn_id_ == txn->GetTransactionId() && req.lock_mode_ == LockMode::SHARED) {
      req.granted_ = true;
      break;
    }
  }

  // 8. Add to transaction's shared lock set
  txn->GetSharedLockSet()->insert(rid);

  return true;
}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  // 1. Check transaction state
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }

  // 2. Check if exclusive lock already held
  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }

  // 3. Get or create lock request queue
  auto &queue = lock_table_[rid];

  // 4. Add EXCLUSIVE request to queue
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::EXCLUSIVE);

  // 5. Wait until lock can be granted
  queue.cv_.wait(lock, [&]() {
    // Check if transaction was aborted while waiting
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }

    // Check for any other granted locks
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });

  // 6. Check if aborted during wait
  if (txn->GetState() == TransactionState::ABORTED) {
    // Remove request from queue
    queue.request_queue_.remove_if([txn](const LockRequest &req) {
      return req.txn_id_ == txn->GetTransactionId() && !req.granted_;
    });
    return false;
  }

  // 7. Mark request as granted
  for (auto &req : queue.request_queue_) {
    if (req.txn_id_ == txn->GetTransactionId() && req.lock_mode_ == LockMode::EXCLUSIVE) {
      req.granted_ = true;
      break;
    }
  }

  // 8. Add to transaction's exclusive lock set
  txn->GetExclusiveLockSet()->insert(rid);

  return true;
}

auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  // 1. Check transaction state
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (txn->GetState() == TransactionState::ABORTED) {
    return false;
  }

  // 2. Check if another transaction is already upgrading
  auto &queue = lock_table_[rid];
  if (queue.upgrading_) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }

  // 3. Set upgrading flag
  queue.upgrading_ = true;

  // 4. Find and modify the SHARED request to EXCLUSIVE
  bool found = false;
  for (auto &req : queue.request_queue_) {
    if (req.txn_id_ == txn->GetTransactionId() && req.lock_mode_ == LockMode::SHARED) {
      req.lock_mode_ = LockMode::EXCLUSIVE;
      found = true;
      break;
    }
  }

  if (!found) {
    queue.upgrading_ = false;
    txn->SetState(TransactionState::ABORTED);
    return false;
  }

  // 5. Wait until this transaction is the only holder
  queue.cv_.wait(lock, [&]() {
    // Check if transaction was aborted while waiting
    if (txn->GetState() == TransactionState::ABORTED) {
      return true;
    }

    // Check if any other transaction holds a lock
    for (const auto &req : queue.request_queue_) {
      if (req.granted_ && req.txn_id_ != txn->GetTransactionId()) {
        return false;
      }
    }
    return true;
  });

  // 6. Clear upgrading flag
  queue.upgrading_ = false;

  // 7. Check if aborted during wait
  if (txn->GetState() == TransactionState::ABORTED) {
    // Remove request from queue
    queue.request_queue_.remove_if([txn](const LockRequest &req) {
      return req.txn_id_ == txn->GetTransactionId();
    });
    return false;
  }

  // 8. Mark as granted (already modified to EXCLUSIVE in step 4)
  for (auto &req : queue.request_queue_) {
    if (req.txn_id_ == txn->GetTransactionId()) {
      req.granted_ = true;
      break;
    }
  }

  // 9. Update transaction lock sets
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->insert(rid);

  return true;
}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lock(latch_);

  // 1. Get lock request queue
  auto it = lock_table_.find(rid);
  if (it == lock_table_.end()) {
    return false;
  }

  auto &queue = it->second;

  // 2. Find and remove the transaction's request
  bool found = false;
  for (auto req_it = queue.request_queue_.begin(); req_it != queue.request_queue_.end(); ++req_it) {
    if (req_it->txn_id_ == txn->GetTransactionId()) {
      queue.request_queue_.erase(req_it);
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  // 3. Transition to SHRINKING if in GROWING phase
  if (txn->GetState() == TransactionState::GROWING) {
    txn->SetState(TransactionState::SHRINKING);
  }

  // 4. Update transaction lock sets
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->erase(rid);

  // 5. Notify all waiting transactions
  queue.cv_.notify_all();

  return true;
}

}  // namespace onebase
