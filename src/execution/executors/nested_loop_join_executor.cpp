#include "onebase/execution/executors/nested_loop_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx,
                                                const NestedLoopJoinPlanNode *plan,
                                                std::unique_ptr<AbstractExecutor> left_executor,
                                                std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  has_left_tuple_ = false;
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &predicate = plan_->GetPredicate();
  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  while (true) {
    if (!has_left_tuple_) {
      if (!left_executor_->Next(&left_tuple_, &left_rid_)) {
        return false;
      }
      has_left_tuple_ = true;
      right_executor_->Init();
    }

    Tuple right_tuple;
    RID right_rid;

    if (right_executor_->Next(&right_tuple, &right_rid)) {
      if (predicate == nullptr) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
          values.push_back(left_tuple_.GetValue(&left_schema, i));
        }
        for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
          values.push_back(right_tuple.GetValue(&right_schema, i));
        }
        *tuple = Tuple(std::move(values));
        *rid = RID();
        return true;
      }

      Value result = predicate->EvaluateJoin(&left_tuple_, &left_schema, &right_tuple, &right_schema);
      if (!result.IsNull() && result.GetAsBoolean()) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
          values.push_back(left_tuple_.GetValue(&left_schema, i));
        }
        for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
          values.push_back(right_tuple.GetValue(&right_schema, i));
        }
        *tuple = Tuple(std::move(values));
        *rid = RID();
        return true;
      }
    } else {
      has_left_tuple_ = false;
    }
  }
}

}  // namespace onebase
