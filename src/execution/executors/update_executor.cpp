#include "onebase/execution/executors/update_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
  has_updated_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  if (table_info == nullptr) {
    throw OneBaseException("Table not found in catalog", ExceptionType::CATALOG);
  }

  auto indexes = catalog->GetTableIndexes(table_info->name_);
  const auto &update_exprs = plan_->GetUpdateExpressions();

  int32_t update_count = 0;
  Tuple old_tuple;
  RID old_rid;

  while (child_executor_->Next(&old_tuple, &old_rid)) {
    std::vector<Value> new_values;
    for (const auto &expr : update_exprs) {
      new_values.push_back(expr->Evaluate(&old_tuple, &table_info->schema_));
    }
    Tuple new_tuple(std::move(new_values));

    for (auto *index_info : indexes) {
      if (index_info->SupportsPointLookup()) {
        uint32_t key_attr = index_info->GetLookupAttr();
        Value old_key = old_tuple.GetValue(&table_info->schema_, key_attr);
        Value new_key = new_tuple.GetValue(&table_info->schema_, key_attr);

        if (!old_key.IsNull() && old_key.GetTypeId() == TypeId::INTEGER) {
          index_info->RemoveEntry(old_key.GetAsInteger(), old_rid);
        }

        if (!new_key.IsNull() && new_key.GetTypeId() == TypeId::INTEGER) {
          index_info->InsertEntry(new_key.GetAsInteger(), old_rid);
        }
      }
    }

    table_info->table_->UpdateTuple(old_rid, new_tuple);
    update_count++;
  }

  std::vector<Value> result_values;
  result_values.emplace_back(TypeId::INTEGER, update_count);
  *tuple = Tuple(std::move(result_values));
  *rid = RID();

  has_updated_ = true;
  return true;
}

}  // namespace onebase
