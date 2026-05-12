#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include "onebase/execution/executors/abstract_executor.h"
#include "onebase/execution/plans/plan_nodes.h"

namespace onebase {

class HashJoinExecutor : public AbstractExecutor {
 public:
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> left_executor,
                   std::unique_ptr<AbstractExecutor> right_executor);
  void Init() override;
  auto Next(Tuple *tuple, RID *rid) -> bool override;
  auto GetOutputSchema() const -> const Schema & override { return plan_->GetOutputSchema(); }

 private:
  const HashJoinPlanNode *plan_;
  std::unique_ptr<AbstractExecutor> left_executor_;
  std::unique_ptr<AbstractExecutor> right_executor_;
  std::unordered_map<std::string, std::vector<Tuple>> hash_table_;
  Tuple right_tuple_;
  RID right_rid_;
  std::vector<Tuple> *current_bucket_{nullptr};
  size_t bucket_cursor_{0};
  bool has_right_tuple_{false};
};

}  // namespace onebase
