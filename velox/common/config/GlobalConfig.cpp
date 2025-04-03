/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/common/config/GlobalConfig.h"
#include "velox/flag_definitions/flags.h"

namespace facebook::velox::config {

GlobalConfiguration& globalConfig() {
  static GlobalConfiguration config;
  /// This translation is being done to support backwards compatibility.
  /// This will be removed soon.
  config.memoryNumSharedLeafPools = FLAGS_velox_memory_num_shared_leaf_pools;
  config.memoryLeakCheckEnabled = FLAGS_velox_memory_leak_check_enabled;
  config.memoryPoolDebugEnabled = FLAGS_velox_memory_pool_debug_enabled;
  config.enableMemoryUsageTrackInDefaultMemoryPool =
      FLAGS_velox_enable_memory_usage_track_in_default_memory_pool;
  config.timeAllocations = FLAGS_velox_time_allocations;
  config.memoryUseHugepages = FLAGS_velox_memory_use_hugepages;
  config.suppressMemoryCapacityExceedingErrorMessage =
      FLAGS_velox_suppress_memory_capacity_exceeding_error_message;
  config.useSsdODirect = FLAGS_velox_ssd_odirect;
  config.verifySsdWrite = FLAGS_velox_ssd_verify_write;
  config.memoryPoolCapacityTransferAcrossTasks =
      FLAGS_velox_memory_pool_capacity_transfer_across_tasks;
  config.exceptionSystemStacktraceEnabled =
      FLAGS_velox_exception_system_stacktrace_enabled;
  config.exceptionSystemStacktraceRateLimitMs =
      FLAGS_velox_exception_system_stacktrace_rate_limit_ms;
  config.exceptionUserStacktraceEnabled =
      FLAGS_velox_exception_user_stacktrace_enabled;
  config.exceptionUserStacktraceRateLimitMs =
      FLAGS_velox_exception_user_stacktrace_rate_limit_ms;
  config.forceEvalSimplified = FLAGS_force_eval_simplified;
  config.experimentalSaveInputOnFatalSignal =
      FLAGS_velox_experimental_save_input_on_fatal_signal;
  config.saveInputOnExpressionAnyFailurePath =
      FLAGS_velox_save_input_on_expression_any_failure_path;
  config.saveInputOnExpressionSystemFailurePath =
      FLAGS_velox_save_input_on_expression_system_failure_path;
  config.wsVRLoad = FLAGS_wsVRLoad;
  config.useAvx2 = FLAGS_avx2;
  config.useBmi2 = FLAGS_bmi2;
  return config;
}

} // namespace facebook::velox::config
