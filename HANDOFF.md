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
