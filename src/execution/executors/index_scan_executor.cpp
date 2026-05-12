#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  if (table_info_ == nullptr) {
    throw OneBaseException("Table not found in catalog", ExceptionType::CATALOG);
  }

  index_info_ = GetExecutorContext()->GetCatalog()->GetIndex(plan_->GetIndexOid());
  if (index_info_ == nullptr) {
    throw OneBaseException("Index not found in catalog", ExceptionType::CATALOG);
  }

  matching_rids_.clear();
  cursor_ = 0;

  if (index_info_->SupportsPointLookup()) {
    const auto &lookup_key = plan_->GetLookupKey();
    if (lookup_key != nullptr) {
      Value key_value = lookup_key->Evaluate(nullptr, nullptr);
      if (!key_value.IsNull() && key_value.GetTypeId() == TypeId::INTEGER) {
        int32_t key = key_value.GetAsInteger();
        const auto *rids = index_info_->LookupInteger(key);
        if (rids != nullptr) {
          matching_rids_ = *rids;
        }
      }
    }
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  const auto &predicate = plan_->GetPredicate();
  const auto &schema = table_info_->schema_;

  while (cursor_ < matching_rids_.size()) {
    RID current_rid = matching_rids_[cursor_];
    cursor_++;

    Tuple raw_tuple = table_info_->table_->GetTuple(current_rid);
    *rid = current_rid;

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
