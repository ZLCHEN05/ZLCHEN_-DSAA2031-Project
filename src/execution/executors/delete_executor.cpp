#include "onebase/execution/executors/delete_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  has_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_deleted_) {
    return false;
  }

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  if (table_info == nullptr) {
    throw OneBaseException("Table not found in catalog", ExceptionType::CATALOG);
  }

  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t delete_count = 0;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    for (auto *index_info : indexes) {
      if (index_info->SupportsPointLookup()) {
        uint32_t key_attr = index_info->GetLookupAttr();
        Value key_value = child_tuple.GetValue(&table_info->schema_, key_attr);
        if (!key_value.IsNull() && key_value.GetTypeId() == TypeId::INTEGER) {
          index_info->RemoveEntry(key_value.GetAsInteger(), child_rid);
        }
      }
    }

    table_info->table_->DeleteTuple(child_rid);
    delete_count++;
  }

  std::vector<Value> result_values;
  result_values.emplace_back(TypeId::INTEGER, delete_count);
  *tuple = Tuple(std::move(result_values));
  *rid = RID();

  has_deleted_ = true;
  return true;
}

}  // namespace onebase
