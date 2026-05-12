#include "onebase/execution/executors/insert_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  has_inserted_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_inserted_) {
    return false;
  }

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  if (table_info == nullptr) {
    throw OneBaseException("Table not found in catalog", ExceptionType::CATALOG);
  }

  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t insert_count = 0;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto opt_rid = table_info->table_->InsertTuple(child_tuple);
    if (!opt_rid.has_value()) {
      throw OneBaseException("Failed to insert tuple into table", ExceptionType::EXECUTION);
    }

    RID new_rid = opt_rid.value();

    for (auto *index_info : indexes) {
      if (index_info->SupportsPointLookup()) {
        uint32_t key_attr = index_info->GetLookupAttr();
        Value key_value = child_tuple.GetValue(&table_info->schema_, key_attr);
        if (!key_value.IsNull() && key_value.GetTypeId() == TypeId::INTEGER) {
          index_info->InsertEntry(key_value.GetAsInteger(), new_rid);
        }
      }
    }

    insert_count++;
  }

  std::vector<Value> result_values;
  result_values.emplace_back(TypeId::INTEGER, insert_count);
  *tuple = Tuple(std::move(result_values));
  *rid = RID();

  has_inserted_ = true;
  return true;
}

}  // namespace onebase
