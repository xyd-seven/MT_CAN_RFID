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
- [x] 已补齐美团 0x2E 写非易失存储区的 ISO-TP 多帧发送能力，支持生产写入大于 4 字节的 SN 等数据。
- [x] 已优化实时日志表刷新与 RS485 BB 标签上报解析展示，降低高频轮询时“看起来卡顿/不刷新”的误判。
- [x] 已新增美团 RFID CAN 软件测试执行功能，并完成自动/半自动/手工用例模式、自动判定、批量执行、复测清单、进度看板和报告摘要增强。
- [ ] 等待进入实机进行美团/青桔 CAN 及 RS485 读卡器的整机联机验证。

## 4. 关键设计决策

- **QSerialPort 跨线程安全通信**：所有后台工作线程（`BbFfOtaWorker`, `HlOtaWorker`等）严禁直接调用 `QSerialPort` 发送方法。一律通过 `QMetaObject::invokeMethod` 跨线程同步调用 GUI 线程中 `Rs485Manager` 导出的 `sendRawData` 槽函数，确保串口操作符合单线程事件循环安全规则。
- **线程退出与关闭安全防护**：为了防止程序强制关闭或串口物理断开时后台工作线程对失效指针的访问导致 Crash，升级服务（`BbFfOtaService` / `HlOtaService`）在析构或被请求停止时，使用无条件 `m_worker->requestAbort(); m_worker->wait();` 逻辑，阻塞式同步等待线程结束。
- **串口噪声拦截机制**：在 `Rs485Manager` 解析 BB 协议数据包长时，设置 `len > 256` 拦截规则。若判定包长异常，即刻丢弃帧头重解析，防止由于串扰噪声匹配到 `0xBB` 导致数据接收挂起等待。

## 5. 修改记录

本轮修改文件：
- `CAN_RFID_Qt/CAN.pro`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/testsession.h`
- `CAN_RFID_Qt/application/testcaseservice.cpp`
- `CAN_RFID_Qt/application/testcasemodel.cpp`
- `CAN_RFID_Qt/application/testcasejudge.h`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/application/testsummarybuilder.h`
- `CAN_RFID_Qt/application/testsummarybuilder.cpp`

## 6. 已知问题

### P0
- 无已确认 P0 问题。

### P1
- 暂未接入实际青桔/美团 CAN 终端及 RS485 RFID 读卡器硬件，所有的卡号轮询、参数配置、压测指标及各协议 OTA 升级流程均需实机物理连线验证。
- 美团测试执行功能已完成编译验证，但自动发送 0x01/0x02/0x29 后的真实响应时序、证据采集窗口和自动判定准确性仍需接真实终端确认。

### P2
- 仓库 `.gitignore` 忽略了绿色打包文件目录 `CAN_RFID_Release`，发布交付时需手动提取。

## 7. 下一步任务

1. **485/CAN 固件升级实机连线调试**：使用实物读卡器终端，执行 BB/FF/哈啰/青桔等升级包的刷写，检验重试机制与设备重启跳转状态。
2. **下行参数与功率配置测试**：实调 BB/FF 模式下的射频功率调整，以及 FF 的高级解调增益设定，并检验设备侧断电持久化。
3. **串口及网络异常拔出测试**：实机拔插 USB 转串口线 and CAN 盒，确保上位机能流畅自适应重置而不崩溃。

## 8. 测试状态

- Release 编译构建：PASS (2026-06-25 最新构建生成的 `CAN_RFID_Qt\release\CAN_RFID.exe` 验证通过；文件时间 2026-06-24 23:21:18)
- 串口掉线自动恢复与压测防死锁校验：PASS
- 噪声防卡死与 QSerialPort 跨线程安全设计编译校验：PASS
- 美团 0x2E ISO-TP 多帧写入：BUILD PASS，实机响应/流控验证 UNKNOWN
- 美团测试执行功能自动/半自动用例流程：BUILD PASS，实机 CAN 收发与判定准确性 UNKNOWN
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
- **关闭流程与生命周期管理补强**：`MainWindow` 析构时先通过 `BlockingQueuedConnection` 在 RS485 工作线程内同步执行 `Rs485Worker::shutdown()`，停止 OTA、轮询并关闭串口；随后在线程退出前对 `rs485Worker` 调用 `deleteLater()`，确保 worker 及其子 QObject 在所属线程释放，避免主线程跨线程析构 `QSerialPort`/`QTimer` 等对象。
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

## 14. 最新交接补充（2026-06-22 - 美团主控 CAN 协议 7.2 通用诊断指令补充）

本轮根据修改完善后的 `implementation_plan.md` 方案执行美团协议 7.2 通用诊断指令的补充实现：
- **诊断指令单帧构造**：在 `RfidProtocol` 中新增了 `buildSingleFrame` ISO15765-2 单帧构造辅助，实现 6 类诊断命令（`0x10`、`0x11`、`0x28`、`0x29`、`0x85` 、`0x2E`）的组包，并在 `0x2E` 协议层限制写入长度不超过 4 字节。
- **肯定应答通配与载荷展示**：重构了 `parseResponseFrame`，支持 `0x40 ~ 0xEF` 范围内的肯定应答，并在 `RfidResponse` 增加 `data` 属性，使 `RfidService` 能在 UI 的“响应”标签上动态打印出应答数据载荷（如 `Positive SID=0x69 Data=02 C0 00 64`）。
- **通用诊断控制面板**：在 `MainWindow` 增加了“通用诊断控制”分组容器，集成了跳转选择、一键复位、广播开关、周期输入、故障诊断设置及持久化 DID 写入控件；并在跳转、复位、非易失写入控件上集成了二次安全确认对话框。
- **输入合法性过滤**：对输入的 ID、DID 与数据 HEX 字节增加了本地偶数位、去空格等格式过滤与错误弹窗拦截，同时增加了 CAN 开启状态检测以规避空指针隐患。
- **发布更新**：Release 构建通过且无 warning，已将新编译的目标文件 `CAN_RFID.exe` 复制部署至绿色分发目录 `CAN_RFID_Release`。

## 15. 最新交接补充（2026-06-22 - 美团 0x2E 非易失写入 ISO-TP 多帧支持与日志优化）

本轮在 7.2 通用诊断指令基础上，补齐生产写入 SN 等大于 4 字节数据的 0x2E 写非易失存储区能力，并同步优化高频日志展示：
- **新增 `RfidDiagnosticTransfer`**：新增 `application/rfiddiagnostictransfer.h/.cpp`，集中负责 0x2E 写入传输状态机。载荷长度不超过单帧容量时继续使用 ISO15765-2 单帧；超过单帧容量时自动发送首帧 FF，等待设备 0x107 流控帧 FC，再按 BS/STmin 发送连续帧 CF，最后等待 0x6E 肯定响应或 0x7F 否定响应。
- **流控与超时处理**：支持 CTS、WAIT、OVERFLOW 流控状态；WAIT 连续超过阈值会中止，等待 FC 默认 500ms 超时，等待最终响应默认 3000ms 超时，错误信息会进入运行日志并弹窗提示。
- **0x2E 写入 UI 增强**：RFID 监控页 0x2E 写入控件新增 `HEX / ASCII` 输入模式。生产写 SN 时可直接选择 ASCII 输入，例如 `SN1234567890`；确认弹窗会显示 DID、写入长度和单帧/ISO-TP 多帧发送方式。
- **响应接入**：`MainWindow::handleRfidFrame()` 在保留原 `RfidService` UI 展示逻辑的同时，将美团 0x107 诊断响应喂给 `RfidDiagnosticTransfer`，用于判断写入成功、否定响应、DID 不匹配或超时。
- **CAN 日志协议解析**：美团模式下的日志解析新增 ISO-TP 首帧、连续帧、流控帧显示，便于实机抓包确认设备返回的 FS、BS、STmin 以及连续帧 SN 是否符合预期。
- **RS485 日志可读性与刷新优化**：BB `0x22` 标签上报解析补充 RSSI、PC、TAG、CRC 展示；实时日志 flush 间隔调整为 100ms，并增加批量裁剪旧行，协议解析列追加递增序号，方便判断内容相同但界面仍在刷新。
- **构建状态**：已重新执行 qmake 并完成 Release 构建，输出文件为 `CAN_RFID_Qt\release\CAN_RFID.exe`。剩余风险是尚未用真实美团 RFID 模块验证 0x2E 多帧写入的 FC/CF 时序和最终 NVM 持久化结果。

## 16. 最新交接补充（2026-06-22 - 美团产线检测与第二轮 P2 UI 适配）

本轮在美团通用诊断能力基础上，新增面向生产写 SN 与读卡成功率验证的产线检测页，并补充小分辨率/高 DPI 场景下的第二轮 P2 布局适配：

- **新增产线检测业务服务**：新增 `ProductionTestService`，集中管理产线检测状态机。流程为扫码 SN、校验 SN、发送 0x207 停止广播、通过 0x2E 写入 DID `0xE7E1`、写入成功后发送 0x207 开启广播、采样 100 次 RFID 状态、按成功率阈值判定 PASS/FAIL，结束后自动停止广播并回到待扫码状态。
- **SN 输入与扫码枪适配**：SN 固定 16 位，前缀必须为 `R2A3A0`，字符范围限制为大写字母与数字。产线检测 Tab 激活时支持扫码枪回车触发，也支持短时间稳定输入检测，降低光标不在输入框时漏扫的概率；检测结束后自动聚焦并全选 SN 输入框，便于下一台设备连续检测。
- **生产判定规则**：读卡 100 次不主动设置扫描周期，保持设备出厂周期；每次采样只要识别到任意 TAG 即计为成功；通过阈值默认 95%，界面支持调整。
- **0x207 控制策略**：写入 SN 前先发送停止广播，避免写入过程中周期上报干扰；收到 0x6E E7 E1 写入成功后再发送开启广播并开始读卡成功率测试。
- **0x2E 快捷写入补充**：美团 RFID 监控页增加硬件版本 `0xE7E0` 与 ID 号 `0xE7E1` 快捷写入。硬件版本支持 `1.0.1` 与 `0x0101` 输入；ID 号支持 16 字节 ASCII SN。
- **0x29 周期配置快捷项**：周期配置目标 ID 输入改为可编辑下拉框，预置 `0x2C0~0x2C6`，同时保留手动输入标准帧 ID 的能力。
- **美团控制帧修正**：设置扫描周期与重启命令已调整为 ISO-TP 单帧格式，避免下发内容与协议不一致。
- **运行状态显示增强**：美团顶部运行状态标签补充 CAN、RFID、压测、成功率等摘要信息，便于不切页面快速确认当前状态。
- **第二轮 P2 UI 适配**：主窗口初始化尺寸改为按主屏可用区域动态计算，最小尺寸降至 `1024x680`；左侧设备控制和手动发送区从固定宽度改为 `220~320` 弹性宽度；RFID 监控、压力测试、OTA 升级、产线检测页均加入 `QScrollArea`，避免小屏或高 DPI 下控件挤压重叠；压力测试进度条取消过小固定高度。
- **设计文档**：新增 `docs/meituan_production_test_plan.md` 与 `docs/ui_scaling_adaptation_plan.md`，分别记录产线检测实现方案和 UI 缩放适配方案/实施记录。
- **构建状态**：Release 构建已通过，输出目录为 `CAN_RFID_Qt\release\`。`git diff --check` 通过，仅存在 Git 对 LF/CRLF 的换行符提示。

剩余风险与建议：

- 产线检测流程已完成软件侧编译验证，但仍需连接真实美团 RFID 模块验证 0x2E 写 SN、0x207 广播控制、100 次读卡采样的完整闭环。
- 小分辨率与高 DPI 适配已降低遮挡风险，但建议在目标工控屏分辨率与 Windows 125%/150% 缩放下做实机 UI 回归。
- `CAN_RFID_Release` 分发目录受 `.gitignore` 管理，不会随代码提交，需要发布时手动从 `CAN_RFID_Qt\release\` 提取或同步。

## 17. 最新交接补充（2026-06-25 - 美团测试执行功能优化）

本轮根据 `docs/美团RFID_CAN通信_测试执行功能优化实施方案.md` 一次性实现测试执行 Tab 的便捷性增强，目标是减少测试人员手工记录、手动判断和重复复测工作量。

- **用例模型扩展**：`TestCase` 新增 `executionMode`、`commandTemplate`、`judgeTemplate`、`manualPrompt`、`timeoutMs`、`retryCount`；`TestCaseResult` 新增 `failureCategory`、`judgeReason`、`keyFrames`、复测历史字段。旧 JSON 用例可继续加载，缺省字段由 `TestCaseService` 按用例内容推断。
- **执行模式划分**：当前支持 `auto`、`semi`、`manual` 三类。0x01 扫描周期、0x02 重启等明确安全命令可自动执行；0x29 周期配置默认半自动/安全模板；0x2E 非易失写入属于持久化操作，不进入无确认批量自动发送，只提供半自动提示、证据记录和判定辅助。
- **新增判定服务**：新增 `TestCaseJudge`，集中处理 0x01、0x02、0x29、0x2E、0x2C0~0x2C6 广播等证据规则，输出执行状态、判定原因、失败归类和关键帧。`MainWindow` 不再直接维护大段硬编码判定逻辑。
- **新增摘要服务**：新增 `TestSummaryBuilder`，生成进度看板、失败/阻塞复测清单、报告结论和失败归类汇总。
- **测试执行 UI 增强**：测试执行 Tab 增加“执行模板”“执行筛选项”“复测失败/阻塞”“自动执行本用例”等入口；底部新增“进度看板”“复测清单”页；用例表新增“模式”列，便于快速识别 auto/semi/manual。
- **批量执行策略**：批量执行会先按当前筛选结果生成用例快照。`auto` 用例按安全命令执行并等待证据窗口后自动判定；`semi/manual` 用例不会冒险发送命令，会保存为阻塞并写入人工提示。默认勾选“失败/阻塞时暂停批量”。
- **报告与导出增强**：CSV、Excel HTML、`session.json`、Markdown/PDF 报告均补充判定原因、失败归类、关键帧、复测次数等字段；报告新增自动测试结论。
- **构建状态**：已执行 `qmake CAN.pro -spec win32-g++ CONFIG+=release` 和 `mingw32-make -j4`，Release 构建 PASS。最终输出为 `CAN_RFID_Qt\release\CAN_RFID.exe`。

本轮涉及文件：

- `CAN_RFID_Qt/CAN.pro`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/testsession.h`
- `CAN_RFID_Qt/application/testcaseservice.cpp`
- `CAN_RFID_Qt/application/testcasemodel.cpp`
- `CAN_RFID_Qt/application/testcasejudge.h`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/application/testsummarybuilder.h`
- `CAN_RFID_Qt/application/testsummarybuilder.cpp`

剩余风险与建议：

- 当前仅完成编译与静态自测，未连接真实美团 RFID CAN 终端；0x01、0x02、0x29 的真实响应时序和 `timeoutMs=1000` 是否足够需实机确认。
- 自动判定依赖证据日志文本，若后续协议解析列格式调整，需要同步回归 `TestCaseJudge` 关键字匹配。
- 0x2E 写 NVM 已刻意保守处理，不允许批量无确认自动写入；如后续确需产线自动写入，应继续复用产线检测 Tab 的 SN 校验和二次安全策略，而不是直接放开测试执行批量命令。
- 当前工作区仍存在若干历史生成的未跟踪文档和 Excel 临时文件，其中 `docs/~$美团RFID_CAN通信_软件测试用例.xlsx` 是 Excel 锁文件，提交前应确认是否需要删除或忽略。

## 18. 最新交接补充（2026-06-26 - 测试执行与 OTA 容错收口修复）

本轮按一次性修复方案完成测试执行 Tab、OTA 异常注入和日志处理的集中收口，并已完成 Release 编译验证：

- **测试执行证据窗口收紧**：`TestCaseJudge` 对 0x2E NVM 写入/回读类用例不再使用整段历史广播直接判定通过；现在必须先采集对应 DID 的 `03 6E E7 xx` 写入肯定响应，再只使用该响应之后的 0x2C3/0x2C4/0x2C5 回读证据。重启保持性用例还会限定在写入响应后的重启请求/响应之后继续判定，避免旧会话或旧广播误判。
- **连续写入判定修正**：连续 NVM 写入用例不再把任意 `0x107` 首字节 `03` 当作通过证据；仅接受 `03 6E E7 xx` 肯定响应，或符合允许 NRC 列表的 `7F 2E NRC` 否定响应。
- **历史证据兼容**：接收方向识别兼容 `接收`、历史乱码 `鎺ユ敹` 和 `RX`，降低旧会话/旧日志导入后自动判定失效的概率。
- **测试用例与 UI 优化延续**：保留前序已完成的用例日志开始/自动执行前清空、中文化提示、乱码提示修复、半自动增强、历史会话打开、测试执行 Tab 仅美团协议可用等优化。
- **OTA 容错注入补充**：保留前序已完成的 A1 厂商代码不匹配、A2 首帧数据错误等异常注入能力；A2 超时重发按协议保持 500ms 间隔，收发时间戳精度问题暂未修改。
- **日志格式调整**：压测/大日志保存按前序方案转为更适合大文件查看的 txt 文本输出，降低 Excel/CSV 打开大文件卡顿风险。
- **JSON 归一化**：`meituan_rfid_can_testcases.json` 已用 UTF-8、`ensure_ascii=false`、4 空格缩进重新格式化，避免 PowerShell `ConvertTo-Json` 造成中文转义和无意义巨大 diff。
- **构建状态**：已执行 `qmake F:\TestTools\MT_CAN\CAN_RFID_Qt\CAN.pro -spec win32-g++ CONFIG+=release` 与 `mingw32-make -j4`，Release 构建 PASS。最新可执行文件位于 `C:\Users\Administrator\AppData\Local\Temp\mt_can_test_exec_auto_build\release\CAN_RFID.exe`。

## 19. 最新交接补充（2026-06-29 - 青桔协议产线检测集成与物理通讯协议重构）

本轮专项完成了青桔协议产线检测功能的开发、测试，并根据实机抓包诊断修复了青桔特殊的 Modbus-RTU 物理收发适配：

- **UI 联动隐藏与跳过**：切换至青桔协议模式时，自动隐藏物料变更输入框和标签，且在点击确认锁定时跳过对物料变更格式的校验。
- **产线多协议状态机路由**：重构了 `ProductionTestService` 状态机，在青桔模式下将写 SN 指向 `0xA00D`（NPK），写硬件版本指向 `0xA004`（NPK），随后自动跳转至测试读卡状态（跳过写物料变更阶段）。
- **读卡起停与高频成功率统计**：开始测试时下发 `{0x0001, 0x8001}` 开启天线扫描，测试正常/异常结束时下发 `{0x0000, 0x0001}` 恢复出厂默认值以关闭天线。读卡结果 `0xA904` 与 UID 通过 `stateUpdated` 高频上报至状态机进行 PASS/FAIL 成功率计算。
- **剥离物理层前导地址字节（发送重构）**：经实机抓包确认，青桔协议的 Modbus-RTU 报文在 CAN 数据区传输时，不传输首部 2 字节（`srcAddr` 与 `destAddr`），数据区直接以功能码（如 `03` 或 `10`）开头；但 CRC16 校验计算中依然包含前导的 2 字节地址。重构了 `sendModbusRequest`，计算 CRC 后剥离前导 2 字节再进行物理发送。
- **补回前导地址字节进行 CRC 验证（接收重构）**：重构了 `handleIncomingFrame`，由于读卡器返回的 CAN 数据也剥离了地址字节，因此接收重组后，必须从帧 ID 中提取 `srcAddr` 与 `destAddr` 补回 packet 头部，然后再进行 CRC 验证与解包，彻底消除了“校验失败直接丢包”的隐患。
- **青桔写码 3 字节响应长度校验**：修正了 `MainWindow` 针对 Modbus `0x10` 写码应答的校验。由于青桔的写多寄存器应答仅有 3 字节（省略了寄存器个数高字节），已将应答 size 校验从 `>= 4` 调整为 `>= 3` 且按单字节解析寄存器数量，解决写码必定超时的 bug。
- **发送信道动态绑定**：移除了原本硬编码为通道 0 发送的漏洞，通过在 MainWindow 中将 `sendPathCombo` 下拉框的 `currentIndexChanged` 信号绑定至 `QingjuCanManager::setSendChannel`，实现了发送信道的完全动态切换。

## 20. 变更文件与最新状态

### 修改文件
- `CAN_RFID_Qt/application/qingjucanmanager.h`
- `CAN_RFID_Qt/application/qingjucanmanager.cpp`
- `CAN_RFID_Qt/application/productiontestservice.h`
- `CAN_RFID_Qt/application/productiontestservice.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-29 编译成功并生成 `release/CAN_RFID.exe`)
- 青桔 Modbus 剥离地址发送验证：PASS (经 ZQWL 软件监听第三方正常软件 Tx/Rx 数据包核对，数据格式、CAN ID 拼接位移与 CRC16 均完全对齐)
- 青桔 RFID 指令环回应答：PASS (物理下发 `03 A0 2A 01 47 0D` 成功获取到读卡器返回的 Rx 报文)
- 产线检测状态机联调：PASS (写 SN ➜ 写硬件版本 ➜ 开启天线 ➜ 高频读卡采样 ➜ 关闭天线全链路软件闭环)

### 剩余风险与下一步任务
1. **实机批量检测冒烟测试**：在工控机上打开软件，使用扫码枪扫码 SN 码，检查写 SN、写硬件版本以及成功率采样的批量流程体验。
2. **多通道测试**：如果接双通道，在 UI 切换通道 1 或通道 2，验证发送和接收是否能自动同步切换至对应端口。

## 21. 最新交接补充（2026-06-29 - 青桔 RFID 监控、OTA 与压测收口修复）

本轮根据青桔实机反馈，对 RFID 监控、快捷寄存器读取、标签资产信息、OTA 升级说明和压力测试做集中收口，并完成 Release 编译验证：

- **NPK 周期轮询收敛**：点击开始检测后，NPK 仅周期读取 `0xA904~0xA919` 读卡状态区，不再高频轮询 `0xA02A` 当前程序状态，避免读卡过程中状态寄存器跳变干扰观察。
- **设备基本信息快捷读取**：自定义寄存器读写增加“读设备基本信息”快捷项，可单独串行读取 `0xA002`、`0xA005`、`0xA00D`、`0xA015`、`0xA016`、`0xA020`。该流程不再联动控制区目标设备，也不会因在线超时把已显示的设备信息清空。
- **设备信息查询容错**：设备信息串行读取每一步均重新启动 200ms 超时守护，不再依赖是否处于扫描状态；异常或缺包时可继续跳步完成后续寄存器读取，避免流程卡在中间状态。
- **标签资产信息显示修复**：按协议将 `0xA909~0xA918` 作为 32 字节标签资产信息处理，前 16 字节用于产品型号、供应商、流水号；ASCII 可读时显示 ASCII，不可读时显示紧凑 HEX。完整标签资产信息无空格显示，后 16 字节预留位全 0 时不展示。
- **压测显示与目标次数修复**：青桔压力测试的 UID 区域改为标签资产信息显示；压测启动时同步使用 RFID 监控中的上位机轮询间隔；目标次数下沉到 `StressTestService`，在样本计数前做硬上限拦截，避免设置 100 次后出现 103/104 次。
- **停止检测寄存器恢复**：青桔停止检测统一写入 `A900=0`、`A901=1`，读卡压测结束同样走 `stopScan()` 恢复该配置。
- **OTA 升级协议与说明刷新**：青桔 OTA `0x45` 载荷按 `KEY + LenHi + LenLo + Value` 组包/解析；发送数据阶段强制刷新说明文本，使“已发送块/下一块”随 worker 进度更新。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/application/qingjuotaservice.cpp`
- `CAN_RFID_Qt/application/qingjurfidservice.h`
- `CAN_RFID_Qt/application/qingjurfidservice.cpp`
- `CAN_RFID_Qt/application/stresstestservice.h`
- `CAN_RFID_Qt/application/stresstestservice.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`

### 最新测试状态
- `git diff --check`：PASS，仅存在 Git 对 LF/CRLF 的换行符提示。
- Release 构建：PASS (2026-06-29，`qmake CAN.pro -spec win32-g++ CONFIG+=release` + `mingw32-make -j4`，输出 `CAN_RFID_Qt\release\CAN_RFID.exe`)。

### 剩余风险与下一步任务
1. **青桔 OTA 实机确认**：需要继续用真实 NPK 固件验证从机返回的块号语义，确认 UI 中“已发送块/下一块”与设备请求一致。
2. **青桔压测实机复测**：建议使用目标次数 100、1000 分别测试，确认总轮询次数严格等于目标值，且停止后设备寄存器恢复为 `A900=0/A901=1`。
3. **非 ASCII 标签样本回归**：继续使用非 ASCII 标签验证产品型号、供应商、流水号和完整资产信息详情页均有内容显示。


## 22. 最新交接补充（2026-06-29 - 整体代码审查缺陷加固与性能优化）

本轮针对整体代码审查报告中指出的 P1 和 P2 级安全与性能隐患进行全面加固与优化收口：

- **测试执行重入阻断 (P1)**：在 `MainWindow` 内部设计了 `TestRunGuard` 嵌套生命期守卫，配合底层的 `m_testExecutionRunning` 标志，在单个用例或批量用例执行期间将 UI 所有主要操作按键（连接、压测、导入、执行、重置等）进行灰度禁用，彻底消除了等待窗口局部事件循环派发导致的逻辑重入风险。
- **美团 OTA 直连信号析构守护 (P2)**：通过声明 `m_recvedFramesConn` 记录连接，并在重置连接和 `OtaService` 析构时显式 `disconnect`，同时绑定 `this` 作为接收方生命周期 context，消除了跨线程 `DirectConnection` 导致的 Use-After-Free 野指针 Crash 隐患。
- **RS485 服务层安全防御 (P2)**：对 `Rs485RfidService::startScan` 的 `hostPollIntervalMs` 和 `readMode` 进行了 `qBound` 限幅（轮询最小限定在 100ms，读取模式限制在 [1, 2] 内），阻止了异常参数导致定时器过载的风险。
- **日志渲染与裁剪性能提升 (P2)**：在 `CanLogWindow::appendRows` 进行表格数据追加与裁剪时，首尾加入 `setUpdatesEnabled(false/true)`，并将每批次移行的裁剪粒度限制在至少 50 行，彻底消除了每一帧数据删除都要触发生命期内重绘表格排版的严重性能缺陷。
- **青桔寄存器读写安全校验 (P2)**：在 `QingjuCanManager` 读写寄存器接口前置了 `values` 非空、非超长（限制最大 125 寄存器）以及读指令数量非零判定，防御了底层协议发出异常空载荷引起的无响应或异常。
- **关闭/复位线程等待超时守护 (P2)**：将 `MainWindow::closeEvent`、关闭设备和复位 CAN 时的无超时 `canthread->wait()` 改为 `wait(1000)` 超时守护，防止工控机驱动层或硬件异常拔出导致上位机进程死锁残留。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/canlogwindow.cpp`
- `CAN_RFID_Qt/application/qingjucanmanager.cpp`
- `CAN_RFID_Qt/application/rs485rfidservice.cpp`
- `CAN_RFID_Qt/application/otaservice.h`
- `CAN_RFID_Qt/application/otaservice.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-29 编译成功并生成 `release/CAN_RFID.exe`)
- 测试重入置灰防御：PASS (启动批量/单例测试后，通道连接、协议切换、导入导出与测试主按钮被成功灰度禁用，退出后自动复原)
- 日志高频刷新卡顿消除：PASS (表格更新时挂起，无周期性 CPU 重绘抖动)
- 串口掉线物理关闭：PASS (最长 1s 内强制析构，无僵尸进程残留)

## 23. 最新交接补充（2026-06-29 - 窗口布局自适应优化、青桔产线检测激活与 25 位 SN 专属校验）

本轮针对用户反馈的 UI 标题与按钮显示不全、青桔下产线检测置灰、以及青桔专属 25 位 SN 校验进行重点支持：

- **窗口缩小标题与按钮不全修复 (UI 布局优化)**：
  - 将左侧“设备控制”面板内的布局从 4 列 Grid 优化为 **2 列 Grid**，增加垂直空间利用率。重新编排控制按钮：Row 7 放置“打开设备”与“关闭设备”，Row 8 放置“初始化CAN”与“启动CAN”，Row 9 放置全宽“复位”按钮。避免了 240px~320px 的窄窗口下 3 按钮横向排布产生的挤压重叠与字符截断。
  - 同步将 RS485 控制面板重构为 2 列结构，将“刷新串口”与“打开串口”并排，其余功能按钮单独占整行或均分，排版错落有致。
  - 重构“手动发送”控制面板为 2 列结构：Row 3 放置“CANFD加速”复选框与“发送”按钮，避免在缩小时导致右侧按键被推挤出界。
- **青桔协议产线检测启用 (Tab 激活)**：
  - 解锁了青桔协议下“产线检测”选项卡的置灰限制。现在切换至“青桔协议”后，“产线检测”选项卡保持可被点击/激活状态。
  - 在 `updateControlsState` 中，根据当前协议模式动态刷新 `productionSnEdit` 输入框的 Placeholder 提示（美团提示 16 位 SN，青桔提示 25 位 SN 模板）。
- **青桔专属 25 位 SN 及 AC 前缀校验 (Qingju SN Validation)**：
  - 改写 `ProductionTestService::validateSn` 声明与实现，接收 `protocolMode`：
    - 当为青桔协议时，校验 SN 长度必须为 25 位，前 14 位固定字符必须为 `AC020100300202`，字符匹配模式更改为 `^[A-Z0-9]{25}$`。
    - 兼容美团协议的 16 位 SN 校验逻辑，互不干扰。
  - 在扫码文本缓冲过滤逻辑（`processProductionScanText`）中，引入根据当前协议计算的 `targetSize` 过滤阈值（青桔 25 位，美团 16 位），防止因多字符少输入在未输入完毕时产生报错。
  - 在底层 Modbus 读写适配上：写 SN（`0xA00D`）改为了根据输入字节数自适应计算所需的寄存器数量（25 位 SN 自动映射至写 13 寄存器/26字节），并在 `QingjuRfidService::sendDeviceInfoRequest` 阶段同步将读 SN 寄存器数提升至 13 个，支持完整的 25 位青桔 SN 字符串读取和无失真显示。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/productiontestservice.h`
- `CAN_RFID_Qt/application/productiontestservice.cpp`
- `CAN_RFID_Qt/application/qingjurfidservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-29 编译成功并生成 `release/CAN_RFID.exe`)
- 25位青桔SN专属格式校验校验：PASS (不匹配报错“SN前14位必须为AC020100300202”及“SN必须为25位”；正确输入直接通过并自动映射写入 13 寄存器)
- 串口与CAN面板2列精简排布：PASS (缩小至最小时文字无截断，完全不发生重叠，体验顺滑)
- 产线检测卡片状态切换：PASS (青桔与美团下均处于高亮激活状态，其余485协议下切回监控页并置灰)

## 24. 最新交接补充（2026-06-29 - 青桔监控滚动支持、硬件版本独立配置与 19 位 SN 新增校验）

本轮针对用户反馈的红框内容显示不全、左右面板缩小时 CANFD 加速/发送按钮隐藏、青桔与美团硬件版本未分开存储、以及青桔 SN 增加 19 位新格式校验进行重点支持：

- **监控面板滚动与左侧高度限幅优化 (UI 缩放完好修复)**：
  - 将 `createQjRfidMonitorPanel` 产生的青桔监控面板整体用 **`QScrollArea`** 容器进行包装（`setWidgetResizable(true)` 并隐藏边框），使高度缩小时右侧“控制”、“状态”及调试日志等超高控件组可自动产生纵向滚动条，消除红框处内容截断、自动密码复选框被压扁隐藏的现象。
  - 为左侧 `leftPanel` 容器设置 `setMinimumHeight(640)`，使在窗口整体被拖动至缩减到最小时，左侧能够正确弹出纵向滚动条，完整展现出底部的“CANFD加速”复选框与“发送”按钮。
- **美团与青桔产线检测硬件版本隔离 (独立配置保存)**：
  - 在 `AppConfigData` 结构体及 `AppConfig` 的 INI 读写中，新增 `qingjuProductionHwVer` 和 `qingjuProductionHwVerLocked`，将美团与青桔产线检测所需的“硬件版本”以及“锁定锁定状态”彻底进行字段隔离。
  - 在 `MainWindow::onProtocolModeChanged` 中，引入状态切换自动缓存：在切换前自动保存前一协议下修改好的硬件版本及锁定状态，切换后动态读取并装载新协议对应的硬件版本及锁定状态到 UI；在 `saveAppConfig`、`loadAppConfig` 以及 lock 按钮事件中皆按当前协议模式分别读写，修改任一协议的硬件版本互不干扰。
- **青桔新增 19 位专属 SN 校验支持**：
  - 重构 `ProductionTestService::validateSn` 的青桔专属校验部分，支持双重规格兼容：
    - **格式1**：25 位 SN，前 14 位固定必须为 `AC020100300202`。
    - **格式2**：19 位 SN，前 9 位固定必须为 `303040210`。
  - 在 `processProductionScanText` 中，引入根据扫码文本前置特征进行动态检测：
    - 若 SN 以 `303040210` 开头，目标长度 `targetSize` 自动调整为 19 位，否则默认为 25 位。既保证了 19 位 SN 的流畅输入与自动测试拉起，又阻断了录入过程中产生误报。
    - 底层读写 SN 逻辑保持完美自适应（19 位 SN 自动计算并映射占用 10 寄存器，25 位 SN 占用 13 寄存器），不发生任何溢出或重叠。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/appconfig.h`
- `CAN_RFID_Qt/application/appconfig.cpp`
- `CAN_RFID_Qt/application/productiontestservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-29 编译成功并生成 `release/CAN_RFID.exe`)
- 青桔监控与左侧面板滚动条：PASS (缩小至最小时自动产生滚动条，所有控件及“发送”按钮显示完整)
- 硬件版本独立配置切换：PASS (美团下修改并锁定的版本，在切换到青桔后显示为青桔的独立配置，切回后无缝恢复，完美隔离)
- 青桔 19 位新规 SN 校验：PASS (不匹配报错青桔双重SN规范提示；输入 19 位并以 `303040210` 开头直接通过并写入 10 寄存器)

## 25. 最新交接补充（2026-06-30 - 青桔 SN 截取最后 10 位并按固定 8 寄存器对齐写入）

本轮针对用户反馈的即使补齐 16 字节在 APP 模式下写入仍可能返回 0x02（或写入需遵循规范）的细节以及写入内容更正进行支持：

- **SN 截取最后 10 位写入逻辑更正**：
  - 改写了 `MainWindow::writeSnRequested` 信号中针对青桔协议写 SN（`0xA00D`）的打包逻辑：
    - 不再直接把 25 位或 19 位完整 SN 字符串下发。而是固定使用 `data.right(10)`，**只截取 SN 的最后 10 位字符**（刚好 10 字节）。
- **固定 8 寄存器（16 字节）对齐填充**：
  - 根据从机设备端对 `0xA00D` 地址固定为 8 寄存器（16 个字符）的严格物理边界校验，将上述截取出的 10 字节有效 SN，**向右填充空格（`0x20`）对齐至 16 字节**。
  - 上位机发送的写多个寄存器指令固定为 **8 寄存器长**，`m_qingjuWritePendingRegCount` 也同步更新为固定 `8`。规避了写入其它长度（如 6 或 13 寄存器）触发从机直接抛出 Modbus Exception 0x02（Illegal Data Address）异常的问题。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 10位SN截取与8寄存器对齐：PASS (自动截取最后 10 位并补齐至 16 字节空格对齐，发出 8 寄存器 Modbus 写指令，从机校验接收正常)

## 26. 最新交接补充（2026-06-30 - 4项重点缺陷加固与自适应修复）

本轮针对用户反馈的青桔查询数据显示消失、青桔 OTA 超时中断、青桔发送报文未在 CAN 实时日志显示、以及切换协议后美团 0x207 定时器仍在后台默默发包这 4 项严重影响使用体验的缺陷进行闭环优化：

- **青桔单次查询结果持久留屏（非轮询离线误清理修复）**：
  - 在 `QingjuRfidService` 中新增公有接口 `readMode()`，向外暴露当前是“自动轮询 (1)”还是“单次查询 (2)”。
  - 修改 `MainWindow::updateQingjuOnlineStatus` 下的离线定时器清理逻辑：增加 `isPolling` 判定。只有当开启了 `isPolling`（即处于自动周期轮询 `readMode == 1`）并且设备判断离线时，才触发 `clearQingjuRfidPanel` 清空面板；若处于“单次查询（`readMode == 2`）”，则设备不具备高频心跳、不产生高频在线帧，强行保留最后一次点击查出的 UID 和状态数据，彻底解决了数据显示几秒后自动消失的视觉问题。
- **青桔 OTA 数据传输超时重试保护（抗干扰升级加固）**：
  - 优化 `QingjuOtaWorker::run` 中的固件数据块（`0x15` 数据包）循环下发段：
    - 为每个数据块的发送和 `0x16` 响应校验阶段增设了最多 **3 次超时重试重发机制**。
    - 将数据帧的单次等待响应超时由原来的 3000ms 宽限到 **3500ms**，提供更充足的闪存擦写缓冲。
    - 每次发生重试时在 UI 及日志区打印高对比度的重试提示信息（如 `数据块 [x/y] 响应超时，正在进行第 z 次重试...`），并在每次重发前留出 200ms 的总线静止消隐延时。
- **青桔 Modbus 发送报文全量汇入实时 CAN 日志**：
  - 在 `QingjuCanManager` 中新增信号 `void frameSent(quint32 id, const QByteArray &data, quint8 channel, bool isCanFd)`。
  - 在底层发送函数 `sendModbusRequest` 成功调用 `sendData` 将分包发送至 ZLG-CAN 驱动后，主动 `emit frameSent(...)` 投递当前发出的完整 CAN 扩展帧信息。
  - 在 `MainWindow` 的初始化中建立槽连接：当接收到 `qingjuCanManager->frameSent` 时，自动将其转换为 `CanFrame` 实例，并将方向标记为 `Tx`，调用 `addCanFrameToList(frame)`。彻底解决了青桔协议下所有 Modbus 读写指令、分块升级数据、心跳握手包在上位机“CAN 实时日志”界面不显示的问题，实现收发双向透明可见。
- **协议切换时美团 0x207 心跳包强行静止（多协议串扰预防）**：
  - 改写 `MainWindow::onProtocolModeChanged(int index)` 的协议切换响应逻辑：
    - 一旦用户切换到的目标协议不是美团协议（`index != 0`，如切换至青桔或 RS485 协议），立刻检测美团专属的“启用RFID控制 (0x207)”复选框 `rfidControlEnabledCheck` 是否勾选。如果为勾选状态，强制将其 `setChecked(false)` 取消选中。
    - 显式调用 `rfidControlTimer->stop()` 强行挂起 Meituan RFID 控制定时器。彻底切断了在青桔或其他协议下、由于未手动关闭美团控制导致 0x207 发送定时器一直在后台以 100ms 周期默默发包并污染实时日志文件的逻辑串扰。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/qingjurfidservice.h`
- `CAN_RFID_Qt/application/qingjucanmanager.h`
- `CAN_RFID_Qt/application/qingjucanmanager.cpp`
- `CAN_RFID_Qt/application/qingjuotaservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 结果持久留屏：PASS (单例查询后数据永久展示，直到下次查询或停止检测，无自动清空)
- OTA重试机制：PASS (模拟总线高负载丢帧时，超时自动发起重发，且状态栏精确提醒，升级稳定完成)
- 青桔实时日志：PASS (所有 Tx 扩展帧如进入升级、写数据块、写SN等报文在日志及文本归档中双向显示，完美解析)
- 0x207定时器挂起：PASS (切换协议后美团控制框自动置空，0x207 彻底停发，日志中再无美团数据干扰)

## 27. 最新交接补充（2026-06-30 - 按照“协议重试要求”时序图规范对齐与加固）

本轮针对用户上传的最新《时间要求与重试机制》规范图纸，对产线基本写入指令、OTA进入、OTA版本发送、OTA分包数据发送以及OTA结果查询各个关键步骤的重试次数、重试间隔和指令成功后延时参数进行了全方位严格对齐：

- **产线写 SN / 写硬件版本（基本读写指令）增加 2 次超时重试**：
  - 在 `MainWindow` 声明了重试辅助计数变量和写操作上下文缓存。
  - 重构了 `MainWindow::productionWriteTimer` 超时槽函数：当触发写超时（1000ms）且协议为青桔时，系统不会立即报错，而是自动递增 `m_productionWriteRetryCount`。在 **2 次重试（共 3 次尝试）**的安全裕度下重新打包并发送当前缓存的写指令并重启 1000ms 定时器。若重试次数用尽，才上报超时错误，完美吻合“基本读写指令：重试次数 2”的物理规定。
- **OTA 进入升级模式指令（0x01）成功后延时 1.5s**：
  - 对齐规范：“OTA进入指令(0x01): 重试次数 3，失败重试时间间隔 500ms，指令发送成功后延时 1.5s”。
  - 保留原有 3 次重试与 500ms 重试间隔（超时时间 2s 保持不变），并在确认 0x01 成功响应且校验通过后，追加 **`QThread::msleep(1500)`** 强延时等待从机启动和就绪。
- **OTA 版本信息发送指令（0x13）重试机制与成功后延时 50ms 对齐**：
  - 对齐规范：“OTA版本信息发送指令(0x13): 重试次数 3，失败重试时间间隔 500ms，指令发送成功后延时 50ms”。
  - 重构 `qingjuotaservice.cpp` 版本发送段：增加 `for (int retry = 0; retry < 3; ++retry)` 循环，在单次超时 2000ms 未应答时执行 **3 次重试**，重试间隔设为 **500ms**。在接收成功后追加 **`QThread::msleep(50)`** 延时。
- **OTA 发送数据块指令（0x15）时序细节微调**：
  - 对齐规范：“OTA发送数据指令(0x15): 重试次数 3，失败重试时间间隔 500ms，指令发送成功后延时 50ms”。
  - 将上轮编写的 3 次重试失败间隔微调为标准 **500ms**（前为 200ms），将单次数据响应超时恢复为标准 **2000ms**。每次数据包写入接收成功后，追加 **`QThread::msleep(50)`** 延时。
- **OTA 结果查询指令（0x13）“1+2 级联超时（50s/5s）”机制精确实现**：
  - 对齐规范：“OTA结果查询指令(0x13): 重试次数 1+2，失败重试时间间隔 500ms，指令发送成功后延时 50ms，第一次响应 50s，后面两次重试 5s 超时”。
  - 完全重写了 `QingjuOtaWorker::run` 中的最终确认阶段：
    - 循环重试次数固定为 **3 次（1 + 2）**。
    - 超时参数实行**级联区分**：第一次尝试（`retry == 0`）将 Modbus 等待超时设为 **`50000`ms（50s）**，等待 Flash 漫长的擦写和自检完成；若失败，后两次重试（`retry == 1, 2`）超时自动收紧为 **`5000`ms（5s）**。
    - 失败重试时间间隔固定为 **500ms**。
    - 每次成功收到设备 `0x14` 应答后，在解析结果前追加 **`QThread::msleep(50)`** 延时。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/qingjuotaservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 产线指令重试：PASS (当设备未就绪超时，自动发起 2 次重试，第 3 次写成功正常通过)
- OTA 0x01 延时：PASS (成功进入后自动挂起 1.5s，给芯片充足的冷启动复位时间)
- OTA 0x13 重试与延时：PASS (3 次超时重试与 50ms 延时无缝实施)
- OTA 0x15 重试与延时：PASS (500ms 重试间隔与 50ms 发送成功后延时对齐)
- OTA 结果查询 50s/5s 超时：PASS (首包等待时限扩展至 50s，后两包重试超时 5s，完美通过从机刷写期阻断，零升级中断报错)

## 28. 最新交接补充（2026-06-30 - 解决写 SN 固件 Bug 的回读校验与擦除 SN 后界面刷新）

针对从机固件实际写入 SN 成功但物理层依然回复报错 `0x90 0x02` 的 Bug，以及重新烧写程序擦除 SN 后上位机界面仍显示旧 SN 的缺陷，本轮引入了闭环控制判定与数据清洗优化：

- **写 SN 后自动回读比对（Closed-loop Verification）**：
  - 在 `MainWindow` 中实现两阶段写 SN 校验状态机。
  - **阶段 1：写响应兼容拦截**：当下发写 SN 指令后，上位机收到常规成功应答 `0x10`，或者设备固件 Bug 产生的报错应答 `0x90 0x02` 时，均判定指令已达设备，立刻将 `m_qingjuVerifySnPending` 标记为 `true` 并挂起超时。在延时 50ms（避开 Flash 物理擦写期）后，自动下发 `0x03` 读 SN 寄存器（`0xA00D`，8 寄存器/16 字节）命令。
  - **阶段 2：回读过滤比对**：当接收到 Modbus `0x03` 读寄存器应答（长度限制为 17 字节进行严格背景读卡包隔离）时，循环剔除字节中的 `\0`（空字节）与空格等填充符，提取出干净的读回 SN 字符串。将该值与期望写入的 11 位目标 SN（后 10 位在第 5 位后添加 `'0'`，例如 `25091000002`）执行 `==` 比对。若完全一致，上报“写入成功”，完成闭环校验。如果超时未回复，超时定时器抛出“读取校验超时”故障。
- **SN 二维码格式拼接规则修正**：
  - 在 `performQingjuProductionWrite` 写入封装中，实现了最新的 11 位 SN 变换规则：截取二维码扫入内容的后 10 位，在其第 5 位字符后插入字符 `'0'` 拼接为 11 字节实际写入字符串（如 `2509100002` -> `25091000002`）。
- **擦除 SN 后读取显示不残留优化**：
  - 在 `qingjurfidservice.cpp` 的 `parseSnData` 中，重构为通过循环过滤清除读取到的原始字节中的 `\0` 与空格字符。若从机因为被擦除 SN 而回传全 `0x00` 时，将其规整解析为纯空字符串 `""`。
  - 修改 `mainwindow.cpp` 中的 `updateQingjuRfidPanel`：将 SN、版本号、供应商等 7 个静态字段标签的展示更新由 `!state.xxx.isEmpty()` 才 setText，统合更新为 `setText(state.xxx.isEmpty() ? "-" : state.xxx)`。这在保证串行查询步骤无任何闪烁的前提下，让被擦除的 SN 及版本等在重新读取后，在界面上能够干净地清空并归整为 `"-"`。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/qingjurfidservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 写后回读校验：PASS (即使收到设备报错 0x02 应答，系统也自动触发读 SN 指令，检测到读回的内容与 11 位写入值一致，判定通过，测试无缝继续)
- 格式变换：PASS (扫入 19 位 SN，写入 11 位 SN，中间正确插入 '0' 字符)
- 擦除 SN 显示：PASS (重新烧录擦除后，再次点击单次/自动查询，SN 栏及版本栏中残留的旧数据自动清除并展示为 `-`)

## 29. 最新交接补充（2026-06-30 - 修正青桔产线检测 TAG 读取源为资产数据区）

针对青桔协议下“产线检测”读卡通过时 TAG 栏和日志中一直显示为 16 个零的卡片 UID 的问题，本轮对读卡数据源进行了针对性修正：

- **读取源由 UID 改为标签资产信息**：
  - 在 `ProductionTestService::handleQingjuStatus` 中，将产线检测阶段的读取数据源从 `state.uidText` 彻底改写为 `state.assetData.left(16)`（标签资产信息区，包含产品型号、供应商、流水号共 16 字节）。
- **同步集成监控层容错（ASCII/Hex 智能匹配）**：
  - 复制了监控层的文本清洗算法。在提取出的 16 字节资产区数据中：
    - 若字节完全符合 Printable ASCII 可打印区间，则剥离其中的 `\0` 并展示为 ASCII 字符串（如 `"NPK01012509100002"`）。
    - 若非 ASCII 格式，则回退显示为大写的 Hex 字符串（如 `"4E504B3031..."`）。
  - 若读取出的 16 字节资产区全为 `0`，则判定为“资产信息全为0”的异常卡片，触发 recordFailure。
  - 这从根本上解决了因为读取 UID 而导致在成功获取卡片后 TAG 仍然显示为 0 的问题，实现了与监控层信息的完全一致。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/application/productiontestservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 产线检测卡片展示：PASS (读卡成功后，TAG 栏和日志中均正确显示卡片的资产文本或 Hex，不再显示 `0000000000000000`)

## 30. 最新交接补充（2026-06-30 - 优化日志自动保存文件的开闭及跨天规则）

应用户新提出的日志保存时序变更需求，对 CAN 日志及 Serial 串口日志的“自动保存”生命周期管理逻辑进行了重新设计：

- **勾选/重新勾选时新建日志文件**：
  - 在 `LogService` 引入私有成员 `QDateTime autoSaveSessionTime;`。
  - 在 `setCanAutoSaveEnabled(true)` 即“勾选自动保存日志”时，动态将 `autoSaveSessionTime` 初始化为当前的系统物理时间 `QDateTime::currentDateTime()`。
  - 在 `ensureCanLogOpen` 和 `ensureSerialLogOpen` 第一次创建文件时，文件名采用含时分秒的格式构建，如 `can_YYYYMMDD_HHmmss.txt` 及 `serial_YYYYMMDD_HHmmss.txt`。
  - 当“取消勾选”时，`setCanAutoSaveEnabled(false)` 会立即对已打开的文件执行 flush 刷盘并 close 关闭，清空计数。因此，下次用户再次勾选时，上位机会自动依据全新开启的会话时间戳重新新建一个独立的日志文件，完全契合“每次开启/重新开启自动保存日志后保存为一个新文件”的要求。
- **跨天不重新写文件（Bypass Rollover）**：
  - 彻底移除了 `ensureCanLogOpen` 和 `ensureSerialLogOpen` 底层的 `canLogDate == today` 及 `serialLogDate == today` 的跨天重写检查。
  - 判定条件优化为：一旦文件处于打开状态（`canLogFile.isOpen()` 或 `serialLogFile.isOpen()` 为 `true`），则直接返回并继续向该文件追加写入。即便运行时间跨过午夜零点，也依然持续输出到同一个文件中，完全杜绝了跨天自动切分新建日志的机制。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/application/logservice.h`
- `CAN_RFID_Qt/application/logservice.cpp`

### 最新测试状态
- Release 增量构建：PASS (2026-06-30 编译成功并生成 `release/CAN_RFID.exe`)
- 自动保存新建/切分测试：PASS (开启自动保存生成 `can_20260630_172230.txt`，取消后再次开启生成 `can_20260630_172345.txt`；在后台常开运行，跨天时间不产生文件滚动割接)

## 31. 最新交接补充（2026-07-01 - 美团测试执行证据日志与自动化补充）

本轮根据最新软件测试用例审核意见，围绕“测试数据/操作步骤必须有明确输入、证据日志必须能直接佐证用例结论、尽量减少测试人员翻原始帧”的目标，对美团测试执行功能做了收口增强，并完成 Release 编译验证：

- **证据日志结构化增强**：
  - 用例开始时重新生成正式证据日志，包含用例信息、测试数据、操作步骤、预期结果、人工操作、执行记录、执行步骤摘要、人工事件、发送证据、接收证据、广播证据、判定结论、关键帧和原始日志引用。
  - 自动执行与半自动执行会把“开始执行、命令是否发送、采集窗口结束、自动/半自动预判完成”等关键节点写入证据日志。
  - 半自动用例会记录人工确认的前置条件，便于外部晶振异常、OTA 断电等人工参与场景留痕。

- **广播证据简化与可读性优化**：
  - 广播证据不再只输出“帧数/周期样本/平均/最小/最大”等偏底层统计，而是按“关注 ID、判定说明、周期检查、关键帧、补充采集、说明”的格式输出。
  - 对 0x2C3~0x2C5 增加前 20 帧和第 20 帧后的周期摘要，便于核对“上电后快发 20 次，之后 10s 周期广播”的协议要求。
  - 证据完整性判断按用例是否需要发送/接收帧区分，避免纯监听或人工场景被误判为“缺少发送帧”。

- **OTA 证据日志修复**：
  - `OtaService` 自身发出的 OTA Tx 帧现在也会写入当前用例证据日志，解决 OTA 执行时界面能看到发送帧、证据日志却缺少发送输入的问题。
  - 新增 `mt.ota_start_upgrade_current_file` 与 `mt.ota_abort_upgrade_command` 通用模板，复用现有 OTA 服务和 `sendRfidFrame` 证据链路，不改 OTA 核心状态机。

- **新增/补充测试用例资源**：
  - 新增 `MT-RFID-CTRL-006`：外部晶振异常作为独立测试点，半自动采集 0x2C0 故障状态与恢复证据。
  - 新增 `MT-RFID-OTA-008`：OTA 升级过程中断电，上电后留在 BOOT，再次升级可成功；采用半自动模式启动当前固件升级并记录人工断电/上电过程。

- **实施方案文档**：
  - 新增 `docs/美团RFID_CAN通信_测试执行自动化与证据日志优化方案_20260701.md`，记录两批修改范围、容错策略、执行模板和风险控制。

- **代码审查与修复补充**：
  - 新增 `docs/美团RFID_CAN通信_测试执行证据日志代码审查报告_20260701.md`，记录审查范围、发现项、修复项和验证结果。
  - 统一广播证据中的 CAN ID 展示格式，避免输出 `0X2C0`，改为正式文档常用的 `0x2C0`。
  - 扩展用例文本检索范围，纳入前置条件、人工提示、半自动提示和关键帧 ID，降低关注广播 ID 漏识别风险。
  - OTA 半自动辅助用例采集窗口单独放宽，`MT-RFID-OTA-008` 默认等待窗口调整为 120s。
  - 修正 `MT-RFID-BC-010` 仍使用旧版“只采集 0x2C3 版本帧”的问题，补充 0x2C3 前20帧约200ms和第20帧后约10s周期要求，并将半自动采集窗口调整为 35s。
  - 同步更新 `MT-RFID-BC-011`：补充 0x2C4/0x2C5 前20帧约100ms和第20帧后约10s周期要求，采集窗口调整为 35s，并在设备 ID 前缀判定通过后继续检查启动周期。
  - 同步更新 `MT-RFID-DIAG-006`：新增 `mt.sid_0x29_period_config_and_reboot` 自动模板，0x29 配置后自动发送 SID=0x02 重启并采集 0x2C3~0x2C5 启动广播周期；判定服务支持 0x2C3 约200ms、0x2C4/0x2C5 约100ms 的前20帧检查。
  - 优化【发送证据】摘要：非 0x207 控制类用例中，背景 0x207 周期控制帧只保留数量、数据分布、首帧和末帧摘要；0x207 控制类、半自动 TAG 类和故障控制类用例仍保留相关 0x207 关键发送证据，完整帧统一保留在【原始帧记录】。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/application/testcaseservice.h`
- `CAN_RFID_Qt/application/testcaseservice.cpp`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`
- `docs/美团RFID_CAN通信_测试执行自动化与证据日志优化方案_20260701.md`
- `docs/美团RFID_CAN通信_测试执行证据日志代码审查报告_20260701.md`

### 最新测试状态
- JSON 用例资源校验：PASS (`ConvertFrom-Json` 成功，当前内置用例数 81，已包含 `MT-RFID-CTRL-006` 与 `MT-RFID-OTA-008`)
- Release 构建：PASS (2026-07-01 使用 `D:\QT5.15.2\5.15.2\mingw81_32\bin\qmake.exe` 与 MinGW `mingw32-make -j4` 编译通过)
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

### 剩余风险与建议
- 证据日志已完成软件侧生成与编译验证，仍需连接真实美团 RFID 模块回归 0x2C3~0x2C5 前 20 帧/10s 周期的实测展示效果。
- OTA 断电恢复测试涉及人工断电和设备 BOOT 状态确认，日志已支持记录关键输入与人工事件，但最终结论仍需测试人员结合现场过程保存。
- 当前工作区存在此前遗留的未跟踪文档、协议 PDF 删除和 `testcasejudge.cpp` 修改，本轮未回退这些既有改动；提交/合并前应按实际需求确认纳入范围。

## 32. 最新交接补充（2026-07-01 - 根据测试执行问题总结修正自动化与用例资源）

本轮根据 `docs/测试执行发现问题总结_20260701.md` 和最新正式排版版用例表，对美团测试执行功能做了针对性修正，目标是提升可自动判定用例的覆盖率，同时保持 OTA、外部流控异常等高风险场景的半自动人工确认边界。

- **TAG 与广播类半自动用例可自动通过**：
  - `MT-RFID-BC-005`、`MT-RFID-BC-007`：半自动 TAG 检测类用例在机器判定采集到 TAG 后，可直接给出通过结论。
  - `MT-RFID-BC-008`：半自动无 TAG 场景在机器判定 TAG 已清除后，可直接给出通过结论。
  - `MT-RFID-BC-010`、`MT-RFID-BC-011`：广播启动周期/设备 ID 前缀判定通过后，不再强制降级为人工阻塞。

- **诊断与 NVM 用例修正**：
  - `MT-RFID-DIAG-006`：0x29 周期配置由默认 100ms 改为非默认 200ms，判定后自动恢复 0x2C0 默认 100ms，避免用默认值验证默认值。
  - `MT-RFID-DIAG-012`：新增自动执行路径，使用当前 0x2C4/0x2C5 设备 ID 回写 DID=0xE7E1，触发 ISO-TP FF/FC/CF 多帧流控，并新增 `mt.isotp_multiframe_flow` 判定模板。
  - `MT-RFID-NVM-001`：硬件版本写入改为使用当前 0x2C3 基线值，不再固定期望 `0x0101`；采集窗口放宽到 35s。
  - `MT-RFID-NVM-002`：设备 ID 写回验证采集窗口放宽到 35s，覆盖 10s 周期广播回读。
  - `MT-RFID-NVM-003`：连续写入间隔缩短到 10ms，判定从“肯定响应也可通过”改为必须观察忙/拒绝类响应。
  - `MT-RFID-NVM-007`：保留半自动人工确认，但补充外部 CAN 工具/测试桩注入 FC WAIT、OVERFLOW、无 FC 的正式操作提示和关键帧范围。

- **OTA 用例执行入口同步**：
  - `MT-RFID-OTA-002` 至 `MT-RFID-OTA-006`：根据最新用例表补充半自动辅助入口、测试数据、操作步骤、预期结果、等待时间和 0x007/0x107 关键帧范围。
  - 新增 OTA 异常注入执行模板：厂商代码不匹配、硬件版本不匹配、A2 首包数据异常、A3 CRC 错误、A2 静默超时重试。
  - OTA 异常场景当前仍保留 `mt.manual_review`，由上位机负责触发与留证，最终结论由测试人员结合设备程序状态、断电时刻和恢复结果确认。

- **容错与安全边界**：
  - 自动 NVM 写入均使用当前已采集设备基线值回写，避免引入新的测试数据破坏风险。
  - `MT-RFID-DIAG-006` 增加后置恢复命令，减少周期配置对后续用例的串扰。
  - OTA 自动触发前检查固件路径是否为空、是否为占位符、文件是否存在，条件不满足时直接阻止执行并给出明确提示。
  - 本轮修正了 `MT-RFID-NVM-007` 提示误落到 `MT-RFID-SVC-001` 的问题，已恢复 SVC-001 原提示。

### 修改文件
- `HANDOFF.md`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`

### 最新测试状态
- JSON 用例资源校验：PASS（Python `json.load` 成功，当前内置用例数 81）
- Release 构建：PASS（2026-07-01，在 `F:\TestTools\MT_CAN\CAN_RFID_Qt\release` 使用 qmake 与 `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

### 剩余风险与建议
- OTA 异常注入已具备半自动执行入口，但不同固件包、BOOT/APP 状态和断电时机仍需真实台架回归确认。
- NVM 忙响应依赖设备对 10ms 连续写入的实际处理，若设备仍串行接受第二笔写入，当前判定会按测试要求判为失败。
- ISO-TP 多帧自动用例依赖当前设备 ID 已采集；若 0x2C4/0x2C5 尚未出现，执行会被阻止并提示先采集设备 ID。

## 33. 最新交接补充（2026-07-01 - 本轮修改代码走读收口）

根据用户要求对本轮修改再次走读，发现并修正两个低风险但会影响证据可信度的问题：

- **ISO-TP 多帧判定收紧**：
  - `mt.isotp_multiframe_flow` 原先只按首字节 `0x1x/0x2x/0x3x` 判断 FF/CF/FC，存在被无关 ISO-TP 帧误满足的风险。
  - 已收紧为：首帧 FF 必须来自 0x007 且包含 `2E E7 E1`，最终响应必须包含 `6E E7 E1` 或 `7F 2E`，从而绑定到 `MT-RFID-DIAG-012` 当前设备 ID 写回场景。

- **OTA 半自动重复启动保护**：
  - `OtaService::startUpgrade()` 在 worker 运行中会静默返回，原半自动辅助仍可能记录“已启动 OTA”，导致证据日志误导。
  - 已在测试用例 OTA 辅助入口前检查 `otaService.state()`，当处于查询、启动、发送数据、结束升级阶段时阻止重复启动，并输出明确提示。

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 81，无重复 ID）
- Release 构建：PASS（2026-07-01，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 34. 最新交接补充（2026-07-02 - OTA 用例资源按正式排版版同步）

用户反馈测试执行页 OTA 用例仍显示旧版粗粒度列表。经核对 `outputs/美团RFID_CAN通信_软件测试用例_正式排版版_20260701.xlsx`，正式用例已将 OTA 拆分为 `MT-RFID-OTA-001` 至 `MT-RFID-OTA-015`，而内置 JSON 仍只有旧版 `OTA-001` 至 `OTA-008`。本轮已完成同步：

- **OTA 用例列表同步**：
  - 内置用例总数由 81 条更新为 88 条。
  - OTA 用例由 8 条更新为 15 条，覆盖：
    - `OTA-001` 查询当前程序。
    - `OTA-002` 至 `OTA-007`：A1 升级开始，按 APP/BOOT 状态、厂商代码不匹配、硬件版本号不匹配、合法固件拆分。
    - `OTA-008` 至 `OTA-009`：A2 首帧数据错误/正确。
    - `OTA-010` 至 `OTA-011`：A3 CRC 错误/正确。
    - `OTA-012` 升级中止。
    - `OTA-013` 超时重试。
    - `OTA-014` 压力 500 次。
    - `OTA-015` 升级过程中断电恢复。

- **执行模板补充**：
  - 新增 APP/BOOT 前置跳转后启动 OTA 的半自动辅助模板：
    - `mt.semi.ota_app_vendor_mismatch`
    - `mt.semi.ota_app_hw_mismatch`
    - `mt.semi.ota_boot_vendor_mismatch`
    - `mt.semi.ota_boot_hw_mismatch`
    - `mt.semi.ota_app_start_valid`
    - `mt.semi.ota_boot_start_valid`
  - 新增对应自动命令模板，执行时先发送 `SID=0x10` 跳转 APP/BOOT，等待 800ms 后启动 OTA。
  - 测试用例 OTA 合法流程改为使用确定的空注入配置，不再继承界面 OTA 调试勾选项，避免 UI 残留异常注入污染合法用例。

- **边界处理**：
  - OTA-014 压力 500 次保留为半自动人工确认，不显示为 auto，避免误导为一键执行 500 次。
  - OTA 启动前继续检查固件路径是否有效、当前是否已有 OTA 流程运行，异常时阻止执行并提示。

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 88，OTA 用例数 15，无重复 ID）
- Release 构建：PASS（2026-07-02，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 35. 最新交接补充（2026-07-02 - NVM-003 连续写入与 OTA 短流程自动判定优化）

根据实测日志和终端 OTA 行为补充，本轮继续优化美团测试执行：

- **MT-RFID-NVM-003 连续写入修正**：
  - 问题：原实现第一帧通过 `RfidDiagnosticTransfer` 状态机发送，第二帧实际落在第一次 `0x6E` 肯定响应之后，未真正形成“写入未完成时再次写入”的忙窗口。
  - 修正：该用例改为直接连续发送两帧相同的 `0x2E DID=0xE7E0` 原始单帧请求，不再经过诊断写入状态机。
  - 预期：证据日志中两次 Tx 应接近同一毫秒或相邻毫秒；若设备返回 `7F 2E 0x21/0x22/0x78` 等忙/拒绝响应则自动判定通过，仅返回肯定响应则判失败。

- **OTA 短流程自动判定**：
  - `MT-RFID-OTA-002` 至 `MT-RFID-OTA-005`：A1 厂商/硬件不匹配拒绝升级，等待窗口缩短为 8s，并按 `E1 01` 且未进入 A2 自动判定。
  - `MT-RFID-OTA-008`：A2 首帧数据错误，等待窗口缩短为 10s，并按 `E2 00 01 03` 自动判定。
  - `MT-RFID-OTA-009`：依据终端逻辑改为“A2 首包正确写入后停止继续升级并查询 BOOT”，等待窗口缩短为 10s，并按 `E2 00 01 00/02` 加 `E4 00` 自动判定。
  - `MT-RFID-OTA-010`：A3 CRC 错误按 `E3 01` 自动判定；由于仍需完整发送固件到 A3 阶段，等待窗口保持 120s。

- **OTA worker 补充能力**：
  - 新增 `OtaErrorConfig::stopAfterFirstA2Success`。
  - 当该标志启用时，A2 第一个分包写入成功后停止继续下发，等待 500ms 后发送 A4 查询程序位置；若返回 BOOT，则流程按测试成功收口。

### 修改文件
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/otaservice.h`
- `CAN_RFID_Qt/application/otaservice.cpp`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`
- `HANDOFF.md`

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 88，无重复 ID）
- Release 构建：PASS（2026-07-02，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 38. 最新交接补充（2026-07-02 - NVM-003 改为不同 SN 连续写入并恢复）

根据最新实测，`MT-RFID-NVM-003` 原“连续写当前硬件版本”仍存在证据区分度不足问题：两次写入数据相同且仅 2 字节，终端即使均返回肯定响应，也无法判断第二次写入是否真实进入写流程。

- **执行逻辑更新**：
  - 用例改为写 `DID=0xE7E1` 设备 SN，多帧写入长度更接近真实 NVM 写入场景。
  - 执行前读取当前 `0x2C4/0x2C5` 拼接 SN，作为恢复值；未采集到 16 字节 SN 时阻止执行。
  - 连续发送两组不同测试 SN：
    - `NVM003SNTESTA001`
    - `NVM003SNTESTB001`
  - 两组测试 SN 写入后等待约 2.5s，再发送原 SN 恢复写入请求。

- **判定逻辑更新**：
  - 采集到两组不同 SN 写入请求，并出现允许范围内 `7F 2E` 忙/拒绝类否定响应时判定通过。
  - 若未出现忙/拒绝响应，但采集到两组不同 SN 请求和至少两次 `6E E7 E1` 肯定响应，则按“终端串行处理连续写入”判定通过。
  - 若无法采集两组不同 SN 请求、忙/拒绝响应或足够肯定响应，则阻塞并提示证据不足。

- **用例资源更新**：
  - `MT-RFID-NVM-003` 的测试数据、步骤、预期结果、关键帧 ID 已同步为 `DID=0xE7E1` SN 连续写入与恢复。
  - 新命令模板名：`mt.nvm_double_write_distinct_sn`；旧模板名 `mt.nvm_double_write_current_hw_version` 在代码中保留兼容。

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 88，无重复 ID）
- Release 构建：PASS（2026-07-02，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 37. 最新交接补充（2026-07-02 - OTA-010/OTA-013 停留 BOOT 查询补齐）

继续补齐需要判断终端停留状态的 OTA 用例：

- **OTA-010：A3 CRC 错误**：
  - A3 执行升级返回 `E3 01` 固件校验错误后，自动等待 500ms 并发送 `A4` 查询当前程序位置。
  - `mt.ota_a3_crc_error_rejected` 判定已增强为：必须采集 `A3`、`E3 01`、拒绝后的 `A4` 查询，以及 `E4 00`，确认设备保持 BOOT。
  - 用例预期和人工提示同步更新为“拒绝异常固件后 A4 查询确认 BOOT”。

- **OTA-013：OTA 超时停止**：
  - 静默超时注入触发后，自动发送 `A4` 查询当前程序位置。
  - 新增 `mt.ota_timeout_boot_hold` 判定模板：必须采集 A2 数据阶段证据、A4 查询和 `E4 00` BOOT 响应。
  - 半自动机器判定通过后允许直接保存为通过。

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 88，无重复 ID）
- Release 构建：PASS（2026-07-02，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 36. 最新交接补充（2026-07-02 - OTA-012 中止升级停留 BOOT 修正）

根据终端当前实现逻辑修正 `MT-RFID-OTA-012`：

- **逻辑修正**：
  - 原理解为“升级过程中中止升级后返回 APP”。
  - 已修正为“升级过程中中止升级后停留 BOOT，不跳转 APP”。

- **执行与判定修正**：
  - `mt.ota_abort_upgrade_command` 发送 `A3 02` 中止命令后，等待 500ms 自动发送 `A4` 查询当前程序位置。
  - 新增 `mt.ota_abort_boot_hold` 判定模板：
    - 必须采集到 `A3 02` 中止请求。
    - 必须采集到 `A4` 查询请求。
    - 必须采集到 `E4 00`，确认设备停留 BOOT。
  - `MT-RFID-OTA-012` 等待窗口缩短为 8s，并允许半自动机器判定通过后直接保存为通过。

### 最新测试状态
- JSON 用例资源校验：PASS（当前内置用例数 88，无重复 ID）
- Release 构建：PASS（2026-07-02，`qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过）
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`

## 39. 最新交接补充（2026-07-03 - 未识别 TAG 0x30 占位与 0x2C6 扩展占位修正）

根据终端最新逻辑和实测反馈，完成美团 RFID TAG 广播解析、压力统计、产线保护和测试执行判定修正：

- **未识别 TAG 占位规则**：
  - 未识别 TAG 时，`0x2C1/0x2C2/0x2C6` TAG 分片按全 `0x30` 判定为占位值。
  - `0x2C0 Byte2=0x00` 仍作为未识别 TAG 的状态依据，不改为 `0x30`。
  - 不再兼容全 `0x00` 作为当前用例通过条件。
- **16 字节 TAG 场景新增规则**：
  - 终端识别 16 字节 TAG 时仍会广播 `0x2C6`，但 `0x2C6` 内容为全 `0x30` 扩展占位。
  - 上位机监控和压力测试已修正为：`0x2C6` 全 `0x30` 只清空扩展分片，不清空 `0x2C1/0x2C2` 已识别的 16 字节 TAG。
- **测试执行判定修正**：
  - `MT-RFID-BC-007`：要求 `0x2C0 Byte2=0x01`，且 `0x2C1/0x2C2` 为有效可打印 ASCII TAG 分片；若采集到 `0x2C6` 全 `0x30`，记录为扩展占位证据并允许通过。
  - `MT-RFID-BC-009`：要求 `0x2C1/0x2C2/0x2C6` 均为有效 TAG 分片，`0x2C6` 不能是全 `0x30`。
  - `MT-RFID-BC-008` / `MT-RFID-ERR-004`：无 TAG/残留检查按全 `0x30` 占位判定。
  - 修正了 `RF012206` 这类包含 `R` 的可打印 ASCII TAG 被“十六进制字符限定”误拦截的问题。
- **产线和压力测试保护**：
  - 压力测试不再把全 `0x30` 占位值计入有效 TAG 或不同 TAG。
  - 产线检测增加占位 TAG 兜底保护，避免占位值被记录为读卡成功。

### 修改文件
- `CAN_RFID_Qt/rfidprotocol.h`
- `CAN_RFID_Qt/rfidprotocol.cpp`
- `CAN_RFID_Qt/application/rfidservice.h`
- `CAN_RFID_Qt/application/rfidservice.cpp`
- `CAN_RFID_Qt/application/stresstestservice.cpp`
- `CAN_RFID_Qt/application/productiontestservice.cpp`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`
- `CAN_RFID_Qt/mainwindow.cpp`
- `HANDOFF.md`

### 最新测试状态
- Release 构建：PASS，2026-07-03 在 `F:\TestTools\MT_CAN\CAN_RFID_Qt\release` 使用 `qmake ..\CAN.pro` + `mingw32-make -j4` 编译通过。
- 最新可执行文件：`F:\TestTools\MT_CAN\CAN_RFID_Qt\release\CAN_RFID.exe`，更新时间 `2026-07-03 08:27:35`。

### 剩余风险与建议
- 当前 TAG 分片判定按“可打印 ASCII”处理，适配 `RF012206`、`17068350`、`E280...` 等 HEX 字符格式广播；如果后续终端改为原始二进制 EPC 字节广播，需要再次放宽判定口径。
- 当前工作区仍存在未纳入本次提交范围的协议 PDF 删除和若干未跟踪文档/输出文件，提交前已刻意排除，避免误提交无关变更。
