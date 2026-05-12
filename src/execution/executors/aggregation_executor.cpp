#include "onebase/execution/executors/aggregation_executor.h"
#include "onebase/common/exception.h"
#include <unordered_map>

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  const auto &group_bys = plan_->GetGroupBys();
  const auto &aggregates = plan_->GetAggregates();
  const auto &agg_types = plan_->GetAggregateTypes();
  const auto &child_schema = child_executor_->GetOutputSchema();

  struct AggregateState {
    int32_t count{0};
    Value sum;
    Value min;
    Value max;
    bool has_value{false};
  };

  std::unordered_map<std::string, std::vector<AggregateState>> agg_map;

  Tuple child_tuple;
  RID child_rid;
  bool has_input = false;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    has_input = true;

    std::string group_key;
    if (!group_bys.empty()) {
      for (const auto &expr : group_bys) {
        Value val = expr->Evaluate(&child_tuple, &child_schema);
        group_key += val.ToString() + "|";
      }
    } else {
      group_key = "__no_group__";
    }

    if (agg_map.find(group_key) == agg_map.end()) {
      agg_map[group_key].resize(agg_types.size());
    }

    auto &states = agg_map[group_key];

    for (size_t i = 0; i < agg_types.size(); i++) {
      auto &state = states[i];
      const auto &agg_type = agg_types[i];

      if (agg_type == AggregationType::CountStarAggregate) {
        state.count++;
      } else {
        Value val = aggregates[i]->Evaluate(&child_tuple, &child_schema);

        if (agg_type == AggregationType::CountAggregate) {
          if (!val.IsNull()) {
            state.count++;
          }
        } else if (agg_type == AggregationType::SumAggregate) {
          if (!val.IsNull()) {
            if (!state.has_value) {
              state.sum = val;
              state.has_value = true;
            } else {
              state.sum = state.sum.Add(val);
            }
          }
        } else if (agg_type == AggregationType::MinAggregate) {
          if (!val.IsNull()) {
            if (!state.has_value) {
              state.min = val;
              state.has_value = true;
            } else {
              Value cmp = val.CompareLessThan(state.min);
              if (!cmp.IsNull() && cmp.GetAsBoolean()) {
                state.min = val;
              }
            }
          }
        } else if (agg_type == AggregationType::MaxAggregate) {
          if (!val.IsNull()) {
            if (!state.has_value) {
              state.max = val;
              state.has_value = true;
            } else {
              Value cmp = val.CompareGreaterThan(state.max);
              if (!cmp.IsNull() && cmp.GetAsBoolean()) {
                state.max = val;
              }
            }
          }
        }
      }
    }
  }

  if (!has_input && group_bys.empty()) {
    std::vector<Value> values;
    for (size_t i = 0; i < agg_types.size(); i++) {
      const auto &agg_type = agg_types[i];
      if (agg_type == AggregationType::CountStarAggregate || agg_type == AggregationType::CountAggregate) {
        values.emplace_back(TypeId::INTEGER, 0);
      } else {
        values.emplace_back(TypeId::INTEGER);
      }
    }
    result_tuples_.emplace_back(std::move(values));
  } else {
    for (const auto &[group_key, states] : agg_map) {
      std::vector<Value> values;

      if (!group_bys.empty()) {
        size_t pos = 0;
        for (size_t i = 0; i < group_bys.size(); i++) {
          size_t next_pos = group_key.find('|', pos);
          std::string val_str = group_key.substr(pos, next_pos - pos);
          pos = next_pos + 1;

          if (val_str == "NULL") {
            values.emplace_back(TypeId::INTEGER);
          } else {
            try {
              int32_t int_val = std::stoi(val_str);
              values.emplace_back(TypeId::INTEGER, int_val);
            } catch (...) {
              values.emplace_back(TypeId::VARCHAR, val_str);
            }
          }
        }
      }

      for (size_t i = 0; i < agg_types.size(); i++) {
        const auto &state = states[i];
        const auto &agg_type = agg_types[i];

        if (agg_type == AggregationType::CountStarAggregate || agg_type == AggregationType::CountAggregate) {
          values.emplace_back(TypeId::INTEGER, state.count);
        } else if (agg_type == AggregationType::SumAggregate) {
          if (state.has_value) {
            values.push_back(state.sum);
          } else {
            values.emplace_back(TypeId::INTEGER);
          }
        } else if (agg_type == AggregationType::MinAggregate) {
          if (state.has_value) {
            values.push_back(state.min);
          } else {
            values.emplace_back(TypeId::INTEGER);
          }
        } else if (agg_type == AggregationType::MaxAggregate) {
          if (state.has_value) {
            values.push_back(state.max);
          } else {
            values.emplace_back(TypeId::INTEGER);
          }
        }
      }

      result_tuples_.emplace_back(std::move(values));
    }
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }

  *tuple = result_tuples_[cursor_];
  *rid = RID();
  cursor_++;
  return true;
}

}  // namespace onebase
