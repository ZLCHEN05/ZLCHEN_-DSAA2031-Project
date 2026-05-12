#include "onebase/execution/executors/hash_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                    std::unique_ptr<AbstractExecutor> left_executor,
                                    std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  hash_table_.clear();
  has_right_tuple_ = false;
  current_bucket_ = nullptr;
  bucket_cursor_ = 0;

  const auto &left_key_expr = plan_->GetLeftKeyExpression();
  const auto &left_schema = left_executor_->GetOutputSchema();

  Tuple left_tuple;
  RID left_rid;
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    Value key_value = left_key_expr->Evaluate(&left_tuple, &left_schema);
    std::string key = key_value.ToString();
    hash_table_[key].push_back(left_tuple);
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &right_key_expr = plan_->GetRightKeyExpression();
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  while (true) {
    if (current_bucket_ != nullptr && bucket_cursor_ < current_bucket_->size()) {
      const Tuple &left_tuple = (*current_bucket_)[bucket_cursor_];
      bucket_cursor_++;

      std::vector<Value> values;
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
        values.push_back(left_tuple.GetValue(&left_schema, i));
      }
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
        values.push_back(right_tuple_.GetValue(&right_schema, i));
      }
      *tuple = Tuple(std::move(values));
      *rid = RID();
      return true;
    }

    if (!right_executor_->Next(&right_tuple_, &right_rid_)) {
      return false;
    }

    Value key_value = right_key_expr->Evaluate(&right_tuple_, &right_schema);
    std::string key = key_value.ToString();

    auto it = hash_table_.find(key);
    if (it != hash_table_.end()) {
      current_bucket_ = &it->second;
      bucket_cursor_ = 0;
    } else {
      current_bucket_ = nullptr;
    }
  }
}

}  // namespace onebase
