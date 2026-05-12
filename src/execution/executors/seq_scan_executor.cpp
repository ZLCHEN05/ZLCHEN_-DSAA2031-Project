#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  if (table_info_ == nullptr) {
    throw OneBaseException("Table not found in catalog", ExceptionType::CATALOG);
  }
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &predicate = plan_->GetPredicate();
  const auto &schema = table_info_->schema_;

  while (iter_ != end_) {
    Tuple raw_tuple = *iter_;
    *rid = iter_.GetRID();
    ++iter_;

    std::vector<Value> values;
    for (uint32_t i = 0; i < schema.GetColumnCount(); i++) {
      values.push_back(raw_tuple.GetValue(&schema, i));
    }
    *tuple = Tuple(std::move(values));

    if (predicate == nullptr) {
      return true;
    }

    Value result = predicate->Evaluate(tuple, &schema);
    if (!result.IsNull() && result.GetAsBoolean()) {
      return true;
    }
  }

  return false;
}

}  // namespace onebase
