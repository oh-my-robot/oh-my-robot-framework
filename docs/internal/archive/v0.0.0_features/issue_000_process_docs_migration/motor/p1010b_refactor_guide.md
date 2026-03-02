## 目标与成功标准

  1. 目标：把 enable/disable/set_active_report/active_query/write/read/save/set_target/... 的重复组帧、匹配、后处理逻辑收敛到单一执行管线。
  2. 成功标准：P1010B.c 中不再存在按命令复制的发送-等待-取结果模板代码；ISR 匹配逻辑从大 switch 收敛到表驱动分发。
  3. 质量标准：编译通过，samples 完成迁移，文档与实现一致。

  ## 公开接口/类型改动（破坏性）

  1. 删除旧命令式 API（p1010b_enable、p1010b_disable、p1010b_set_mode、p1010b_set_target、p1010b_read_parameter 等）。
  2. 新增统一请求入口：
      - OmRet_e p1010b_submit(P1010BDriver_s *driver, const P1010BRequest_s *request);
      - OmRet_e p1010b_request_sync(P1010BDriver_s *driver, const P1010BRequest_s *request, P1010BResponse_s *response);
  3. 新增命令枚举 P1010BCommand_e，覆盖 0x32~0x40 全部命令语义。
  4. 新增请求对象 P1010BRequest_s：
      - command、timeoutMs、flags(sync/async)、payload union（按命令类型承载参数）。
  5. 新增响应对象 P1010BResponse_s：
      - timestampMs、result、response union（如 active_query[4]、read_param_value、ack_state_cmd 等）。

  ## 内部架构范式

  1. 建立静态描述符表 P1010BCommandDescriptor_s[]（每条命令一行）：
      - command
      - requestCanId
      - ackBaseId（异步命令可为 NONE）
      - defaultMode(sync/async)
      - stateGuardFn
      - encodeFn
      - ackMatchFn
      - decodeAckFn
      - postCommitFn

2. 建立单一执行器：
      - __p1010b_execute_request(driver, request, response, waitMode)
      - 内部统一完成：参数检查 -> 状态守卫 -> 编码 -> 发送 ->（可选）等待 -> 结果提交。
  3. ISR 统一应答分发：
      - 由 ackBaseId + driver.pendingCommand 在描述符表中匹配 ackMatchFn。
      - 匹配后由 decodeAckFn 填充 driver.pendingResponse，再 completion_done。
  4. 保持“单实例同一时刻最多一个未决同步事务”约束。
  5. 异步命令（0x32/0x33/0x40）也走描述符表，但不占用同步事务槽。

  ## 命令映射规范（统一口径）

  1. 同步命令：0x34/0x35/0x36/0x37/0x38/0x39，必须有 ackBaseId 与 ackMatchFn。
  2. 异步命令：0x32/0x33/0x40，ackBaseId = NONE，执行器直接返回发送结果。
  3. faultState 变化判定沿用当前规则：仅 calibrated/faultCode/alarmCode 参与签名，statusBits 为保留位不参与故障回调触发。

  ## 迁移范围

  1. 头文件：oh-my-robot/lib/drivers/include/drivers/motor/vendors/direct_drive/P1010B.h
  2. 实现：oh-my-robot/lib/drivers/src/motor/vendors/direct_drive/P1010B.c
  3. 样例：
      - oh-my-robot/samples/motor/p1010b/main.c
  4. 文档：
      - oh-my-robot/lib/drivers/include/drivers/motor/vendors/direct_drive/doc/参考手册.md
      - oh-my-robot/lib/drivers/include/drivers/motor/vendors/direct_drive/doc/顶层设计指南.md
      - oh-my-robot/docs/internal/archive/v0.0.0_features/issue_000_process_docs_migration/motor/p1010b_work_progress_report.md

## 实施步骤

  1. 定义新公共类型（Command/Request/Response）并移除旧 API 声明。
  2. 实现描述符表和统一执行器，接入同步事务上下文。
  3. 改造 ISR 应答匹配为描述符分发。
  4. 删除旧命令式实现函数。
  5. 迁移 sample 到新 API。
  6. 更新文档中的 API 列表、接入顺序、时序语义。
  7. 运行构建与最小回归验证。

  ## 测试用例与场景

  1. 构建：
      - xmake build robot_project 必须通过。
  2. 同步事务：
      - 0x34/0x35/0x36/0x37/0x38/0x39 请求-应答闭环成功。
      - 超时路径返回 OM_ERROR_TIMEOUT。
      - 并发第二个同步请求返回 OM_ERROR_BUSY。
  3. 异步事务：
      - 0x32/0x33 周期给定持续可发。
      - 0x40 发送后本地状态按既有语义更新。
  4. 状态守卫：
      - Disabled/Enabled/FaultLocked 守卫行为与现有一致。
  5. 回调行为：
      - 反馈/故障/在线/读参回调触发时机不回归。
      - 保留位 statusBits 变化不触发故障回调。

## 风险与控制

  1. 风险：破坏性 API 变更影响上层调用。
      - 控制：同一提交内完成 sample 与文档全量迁移，避免半迁移状态。
  2. 风险：表驱动引入匹配错误。
      - 控制：每条描述符必须具备独立 ackMatchFn 单测场景（至少通过联调样例验证）。
  3. 风险：调试可读性下降。
      - 控制：Request/Response 字段命名保持语义化，不使用裸字节数组暴露给应用层。

  ## 验收标准

  1. 旧 API 不再出现在 P1010B.h。
  2. 新双入口 API 可覆盖全部 0x32~0x40 功能。
  3. 两个 sample 可编译并使用新 API 正常运行路径。
  4. 文档与实现完全一致（无过期接口描述）。

  ## 假设与默认值

  1. 仍只支持单驱动实例单未决同步事务。
  2. 本次不引入新的队列机制，保持 ISR 直解直路由模型。
  3. 不新增 RS485 运行实现，仅保持 CAN 首版语义。

