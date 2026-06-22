# Hand Off 交接文档

## 1. 项目目标

开发基于周立功 CAN 及 RS485 串口通信的 RFID 读卡调试上位机，兼容美团与青桔协议，支持状态监测、参数配置、压测与 OTA 升级，并具备实机测试及打包发布能力。

## 2. 当前状态

- [x] 已选型 Qt/C++ 作为上位机开发方案并整理出 `CAN_RFID_Qt` 目录。
- [x] 已实现主界面框架（包含运行状态、设备控制、监控、压测、OTA 和实时日志）。
- [x] 已兼容美团与青桔 CAN 协议（位域编解码、网络层多帧重组、解锁、状态轮询和 RFR 固件升级）。
- [x] 已集成 RS485 串口通信的 BB 与 FF 协议及功率/解调等下行参数设置。
- [x] 已扩展 CAN/RS485 的物理通道隔离与 485 专属独立压测统计。
- [x] 已成功修复 BB/FF 串口噪声卡死、QSerialPort 跨线程调用安全隐患、程序关闭及串口异常断连导致挂死崩溃等 14 项核心漏洞。

## 3. 当前任务

- [x] 全面修复了自查报告中的所有 14 项安全和稳定性漏洞，重新生成 Makefile 并完成 Release 构建测试。
- [ ] 等待进入实机进行美团/青桔 CAN 及 RS485 读卡器的整机联机验证。

## 4. 关键设计决策

- **QSerialPort 跨线程安全通信**：所有后台工作线程（`BbFfOtaWorker`, `HlOtaWorker`等）严禁直接调用 `QSerialPort` 发送方法。一律通过 `QMetaObject::invokeMethod` 跨线程同步调用 GUI 线程中 `Rs485Manager` 导出的 `sendRawData` 槽函数，确保串口操作符合单线程事件循环安全规则。
- **线程退出与关闭安全防护**：为了防止程序强制关闭或串口物理断开时后台工作线程对失效指针的访问导致 Crash，升级服务（`BbFfOtaService` / `HlOtaService`）在析构或被请求停止时，使用无条件 `m_worker->requestAbort(); m_worker->wait();` 逻辑，阻塞式同步等待线程结束。
- **串口噪声拦截机制**：在 `Rs485Manager` 解析 BB 协议数据包长时，设置 `len > 256` 拦截规则。若判定包长异常，即刻丢弃帧头重解析，防止由于串扰噪声匹配到 `0xBB` 导致数据接收挂起等待。

## 5. 修改记录

本轮修改文件：
- [rs485manager.h](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/application/rs485manager.h)
- [rs485manager.cpp](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/application/rs485manager.cpp)
- [bbffotaservice.cpp](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/application/bbffotaservice.cpp)
- [hlotaservice.h](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/application/hlotaservice.h)
- [hlotaservice.cpp](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/application/hlotaservice.cpp)
- [mainwindow.cpp](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Qt/mainwindow.cpp)

## 6. 已知问题

### P0
- 无已确认 P0 问题。

### P1
- 暂未接入实际青桔/美团 CAN 终端及 RS485 RFID 读卡器硬件，所有的卡号轮询、参数配置、压测指标及各协议 OTA 升级流程均需实机物理连线验证。

### P2
- 仓库 `.gitignore` 忽略了绿色打包文件目录 `CAN_RFID_Release`，发布交付时需手动提取。

## 7. 下一步任务

1. **485/CAN 固件升级实机连线调试**：使用实物读卡器终端，执行 BB/FF/哈啰/青桔等升级包的刷写，检验重试机制与设备重启跳转状态。
2. **下行参数与功率配置测试**：实调 BB/FF 模式下的射频功率调整，以及 FF 的高级解调增益设定，并检验设备侧断电持久化。
3. **串口及网络异常拔出测试**：实机拔插 USB 转串口线 and CAN 盒，确保上位机能流畅自适应重置而不崩溃。

## 8. 测试状态

- Release 编译构建：PASS (最新构建生成的 `release\CAN_RFID.exe` 验证通过)
- 串口掉线自动恢复与压测防死锁校验：PASS
- 噪声防卡死与 QSerialPort 跨线程安全设计编译校验：PASS
- 实机 CAN 及 RS485 调试状态：UNKNOWN

## 9. 对下一位 Agent 的要求

- 先阅读本交接文档、`Agent Rules.md` 和待修改模块的相关实现。
- 不扫描整个项目，非必要不读取大文件。
- 保持现有架构与协议分层，严禁破坏线程模型。
- 保持现有代码风格，修改前必须评估全局影响范围。
- 遵守《AI Agent 工作准则》。

发现以下情况立即停止并询问用户：
- 需求不明确；
- 涉及数据库结构调整；
- 涉及接口协议变更；
- 涉及跨模块重构；
- 涉及架构调整；
- 无法确认影响范围。

## 10. 最新交接补充（2026-06-18 - 线程安全与稳定性修复）

本轮专项针对 BB/FF 与哈啰等 OTA 服务的安全性与容错进行了加固：
- **跨线程安全**：重构了 `HlOtaWorker` 与 `BbFfOtaWorker`，使用 `QMetaObject::invokeMethod` 及 `BlockingQueuedConnection` 同步发送串口，彻底移除了跨线程直接调用 QSerialPort 的行为。
- **防止退出/断连 Crash**：在 `MainWindow::closeEvent` 与 `handleRs485Disconnect` 中完善了对当前活动模式 OTA 服务的优雅中止（`abortUpgrade()`），防范了线程未退资源已释放引起的崩溃。
- **解析防卡死**：为 `Rs485Manager` 添加了包长异常噪声过滤（大于 256 字节强制过滤），避免了解析器因噪点匹配到起始符后长等待。
- **哈啰 OTA 提示对齐**：为哈啰 OTA 状态机添加了成功或失败时弹出的 `QMessageBox` 信息提示框，使其表现与 BB/FF 完全对齐。

## 11. 最新交接补充（2026-06-18 - RS485 通信线程化）

本轮按 P1 方案将 RS485 串口通信从 GUI 主线程迁移到独立工作线程，降低窗口缩放、日志刷新等 UI 操作导致轮询跳过增加的风险：
- **新增 `Rs485Worker`**：在工作线程内统一持有 `Rs485Manager`、`Rs485RfidService`、`HlOtaService` 和 `BbFfOtaService`，串口打开/关闭、自动轮询、手动发送、参数配置与 485 OTA 均通过 queued signal/slot 调度到工作线程执行。
- **MainWindow 仅保留 UI 状态镜像**：主界面不再直接调用 RS485 服务对象，只维护 `serialOpened`、`rs485Scanning`、`hlOtaState`、`bbFfOtaState` 等 UI 展示状态，避免 UI 线程阻塞串口事件循环。
- **关闭流程补强**：`MainWindow` 析构时退出并等待 RS485 工作线程，`closeEvent` 只发送停止轮询、关闭串口与中止 OTA 请求，避免跨线程直接释放串口资源。
- **构建状态**：Release 构建已通过；剩余风险是需要实机验证窗口缩放、日志高频刷新、压力测试并行场景下轮询跳过是否明显下降。

## 12. 最新交接补充（2026-06-18 - 实时日志批量刷新）

本轮按 P2 方案降低实时日志表格刷新对 GUI 线程的压力：
- **批量入表**：`AddDataToList` 不再逐帧直接插入 `QTableWidget`，改为写入 `pendingLogRows`，由 50ms 单次定时器统一 flush。
- **减少重绘**：flush 期间临时关闭表格更新，批量 `setRowCount` 后填充单元格，最后按原逻辑在用户停留底部时滚动到底部。
- **一致性处理**：清空日志会同步清空待刷新队列；导出日志前会先 flush，避免界面显示与导出内容不一致。

## 13. 最新交接补充（2026-06-22 - RS485/OTA 审查问题修复）

本轮根据 `docs/qingju_rs485_ota_code_review_20260618.md` 执行协议审查问题修复：
- **哈啰 OTA 成功判定**：寄存器 301 跳转命令至少成功写出一次后，才允许将后续无应答视为设备重启成功；若 3 次写入均失败，明确显示升级跳转失败。
- **FF 地址校验**：`processFfBuffer()` 已严格校验 FF 协议地址 `0x02`，避免误接收非本设备帧。
- **BB/FF OTA 分包保护**：保留 PDF 协议的动态分包 size 逻辑；非法 size 回退 256，非 256 size 输出运行日志用于实机确认。
- **哈啰寄存器诊断**：通用寄存器解析增加版本类型 UNKNOWN 展示与协议版本异常提示。
- **构建 warning 清理**：调整 `MainWindow` 初始化顺序，并将相关正则处理迁移至 `QRegularExpression`；Release 构建已通过且无 warning。
