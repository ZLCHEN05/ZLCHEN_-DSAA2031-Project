#include "onebase/execution/executors/sort_executor.h"
#include <algorithm>
#include "onebase/common/exception.h"

namespace onebase {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() {
  child_executor_->Init();
  sorted_tuples_.clear();
  cursor_ = 0;

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    sorted_tuples_.push_back(child_tuple);
  }

  const auto &order_bys = plan_->GetOrderBys();
  const auto &child_schema = child_executor_->GetOutputSchema();

  std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
            [&order_bys, &child_schema](const Tuple &a, const Tuple &b) {
              for (const auto &[is_ascending, expr] : order_bys) {
                Value val_a = expr->Evaluate(&a, &child_schema);
                Value val_b = expr->Evaluate(&b, &child_schema);

                Value cmp_result = val_a.CompareLessThan(val_b);
                if (!cmp_result.IsNull() && cmp_result.GetAsBoolean()) {
                  return is_ascending;
                }

                cmp_result = val_b.CompareLessThan(val_a);
                if (!cmp_result.IsNull() && cmp_result.GetAsBoolean()) {
                  return !is_ascending;
                }
              }
              return false;
            });
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= sorted_tuples_.size()) {
    return false;
  }

  *tuple = sorted_tuples_[cursor_];
  *rid = RID();
  cursor_++;
  return true;
}

}  // namespace onebase
