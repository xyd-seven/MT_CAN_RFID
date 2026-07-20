# Hand Off 交接文档

更新时间：2026-07-20
当前分支：`dev`

## 1. 项目目标

维护基于 Qt/C++、周立功 CAN 与 RS485 的 RFID 调试和测试上位机，兼容美团、青桔等协议，支持监控、参数配置、压力测试、OTA、产线检测及自动化测试执行。

## 2. 当前状态

- [x] CAN/RS485 基础通信、协议解析、状态监控和手动收发已实现。
- [x] 美团、青桔协议及相关 OTA、产线检测和压力测试已集成。
- [x] 美团测试执行已支持自动、半自动、人工三种模式以及证据日志和自动判定。
- [x] 内置美团测试用例共 91 条：自动 37、半自动 49、人工 5；无重复 ID。
- [x] CAN 收发显示、保存日志和测试证据统一使用同一映射时间线，并按真实事件顺序输出。
- [x] 压测发送调度、接收批处理、日志批量落盘和时序摘要已优化。
- [x] V1.2 测试用例表已同步关键修订并高亮。
- [ ] 最新软件改动仍需完成一轮全量实机回归。

## 3. 当前任务

- [x] 更新本交接文档为 2026-07-20 当前状态。
- [x] 收口美团测试执行、OTA、诊断广播、压力测试和日志时序相关修改。
- [x] 当前正式项目文件已提交并推送至远端 `dev` 分支。

涉及模块：

- CAN 接收线程与时间线：`CAN_RFID_Qt/canthread.*`、`CAN_RFID_Qt/domain/canframe.h`
- 日志显示与保存：`CAN_RFID_Qt/application/logservice.*`、`CAN_RFID_Qt/mainwindow.*`
- 美团协议与 ISO-TP：`CAN_RFID_Qt/rfidprotocol.*`、`CAN_RFID_Qt/domain/isotptransport.cpp`
- OTA 与诊断传输：`CAN_RFID_Qt/application/otaservice.*`、`CAN_RFID_Qt/application/rfiddiagnostictransfer.*`
- 测试执行：`CAN_RFID_Qt/application/testcase*`、`CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`

## 4. 关键设计决策

- **统一时间线**：接收帧以 ZLG 设备时间戳建立相对时间，再映射到统一 PC 基准；发送帧使用同一时间线。显示、自动日志和证据日志均使用排序后的统一事件时间，不能按 UI 到达顺序直接落盘。
- **日志批量处理**：接收线程只负责轻量取帧和入队；UI 刷新与文件写入批量执行，避免高频压测时阻塞接收。常规保存日志不再附加 `zlg_timestamp_raw`、`relative_ms`、`interval_ms` 等干扰列。
- **0x207 单一发送源**：压力测试期间由压测调度器独占 0x207，禁止界面周期定时器并行发送；证据日志必须记录真实发送事件，不得由采集窗口重复补录。
- **压力时序判定**：`0x207` 和 `0x2C0/1/2/6` 的 150ms 仅作为抖动警告；判断完整周期丢失需结合约两倍周期的长间隔和相邻补偿间隔。`0x2C3/4/5` 按约 10s 周期判断，阈值为 15s。
- **TAG 规则**：未识别 TAG 时 `0x2C1/0x2C2/0x2C6` 为全 `0x30`；16 字节 TAG 时允许 `0x2C6` 为全 `0x30` 扩展占位。`0x2C0` 报识别成功但 TAG 全零或全占位必须判异常。
- **ERR-003 规则**：0x207 保留字节非 0x55 时终端仍按当前设计执行；Byte1 非法时不执行。测试判定必须按此规则处理。
- **ERR-006 规则**：已改为全自动执行，流程为跳转 BOOT、A4 确认 `E4 00`、SID=0x11 软件复位、A4 确认 `E4 01` 和 APP 广播恢复。未确认 BOOT 时不得继续复位。
- **0x29 周期配置**：默认恢复映射为 `0x2C0/1/2/6=100ms`、`0x2C3/4/5=10s`；恢复响应缺失时只对缺失 ID 重试一次。
- **0xFFFF 禁止广播**：该配置为持久化完全禁用。重启后 `0x2C0~0x2C6` 均不得出现上电快发或正常周期广播；零帧应自动通过。
- **BOOT 广播逻辑**：正常 BOOT 下 `0x2C3` 上电后 200ms 快发 20 次，`0x2C4/0x2C5` 100ms 快发 20 次，之后按默认 10s 周期；收到 A1/A2 后停止。A1 后 5s 无后续流程会回 APP，A2 首包正确会擦除 APP 并保持 BOOT。
- **断电测试边界**：协议状态恢复优先使用 SID=0x11 软件复位模拟上下电；只有验证真实掉电保持、升级中掉电或硬件供电行为时才要求人工断电。
- **测试执行安全**：可确定、可恢复的流程使用自动模式；需要烧写射频芯片、真实断电或产线/实车外部条件的流程保留分阶段确认，不进入无人值守批量执行。

## 5. 修改记录

本轮正式交付文件：

- `HANDOFF.md`
- `CAN_RFID_Qt/application/logservice.cpp`
- `CAN_RFID_Qt/application/logservice.h`
- `CAN_RFID_Qt/application/otaservice.cpp`
- `CAN_RFID_Qt/application/otaservice.h`
- `CAN_RFID_Qt/application/qingjucanmanager.cpp`
- `CAN_RFID_Qt/application/qingjucanmanager.h`
- `CAN_RFID_Qt/application/rfiddiagnostictransfer.cpp`
- `CAN_RFID_Qt/application/rfiddiagnostictransfer.h`
- `CAN_RFID_Qt/application/testcasejudge.cpp`
- `CAN_RFID_Qt/application/testcasemodel.cpp`
- `CAN_RFID_Qt/application/testcaseservice.cpp`
- `CAN_RFID_Qt/application/testcaseservice.h`
- `CAN_RFID_Qt/application/testsession.h`
- `CAN_RFID_Qt/canthread.cpp`
- `CAN_RFID_Qt/canthread.h`
- `CAN_RFID_Qt/domain/canframe.h`
- `CAN_RFID_Qt/domain/isotptransport.cpp`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/resources/testcases/meituan_rfid_can_testcases.json`
- `CAN_RFID_Qt/rfidprotocol.cpp`
- `CAN_RFID_Qt/rfidprotocol.h`
- `docs/美团RFID_CAN通信_软件测试用例_V1.2.xlsx`

以下内容不属于本轮提交范围：协议 PDF 删除记录、未跟踪审查材料、`outputs/`、`tmp/` 和本地构建产物。

## 6. 已知问题

### P0

- 无已确认 P0 问题。

### P1

- 最新测试执行逻辑涉及大量真实终端状态和 OTA 分支，尚未完成 91 条用例的全量实机回归。
- `MT-RFID-CTRL-003` 仍依赖人工擦除/烧写射频芯片程序完成故障注入和恢复。
- OTA 中真实断电、固件擦除后保持 BOOT、A1/A2 超时状态等场景必须继续使用目标固件实测。
- 2026-07-17 五小时压测未发现终端广播完整周期丢失，但 `0x207` 出现一次 207ms 发送间隔，属于疑似发送调度漏期，需要后续压测继续观察。

### P2

- `0x2C0/1/2/6` 实测平均周期约 103.7ms，`0x2C3/4/5` 约 10.37s，约比名义周期慢 3.7%；需确认是终端时间基准偏差还是允许容差。
- `CAN_RFID_Release` 和本地 `release` 构建产物不纳入 Git，需要发布时另行打包。
- 工作区保留若干历史未跟踪文档和协议 PDF 删除标记，后续提交不得使用无范围确认的 `git add -A`。

## 7. 下一步任务

1. 按模块执行美团 91 条测试用例实机回归，优先覆盖 P0、诊断广播、ERR-006 和 OTA-009~017。
2. 复测 DIAG-007：配置 `0xFFFF` 后重启，确认 `0x2C0~0x2C6` 全部零广播，并检查默认周期定向恢复重试。
3. 再执行不少于 5 小时压力测试，重点记录 `0x207 >=180ms` 的时刻、发送返回值和 ZLG 错误状态。
4. 对比独立 CAN 分析仪，确认 103.7ms/10.37s 周期偏差来自终端还是上位机时间映射。
5. 完成目标工控机分辨率、Windows 125%/150% 缩放和高负载日志显示回归。

## 8. 测试状态

- Release 构建（Qt 5.15.2 MinGW，2026-07-17）：PASS
- JSON 用例资源解析、数量和重复 ID 检查：PASS
- V1.2 Excel 修订表全工作表渲染和公式错误检查：PASS
- DIAG-007 `0xFFFF` 零广播判定代码检查：PASS
- ERR-006 自动执行流程编译验证：PASS
- 2026-07-17 五小时压力日志完整性分析：PASS（广播无明确丢帧；0x207 一次疑似调度漏期）
- 美团 91 条用例全量实机回归：UNKNOWN
- OTA 全流程和真实断电恢复回归：UNKNOWN
- 青桔及 RS485 最新改动实机回归：UNKNOWN

## 9. 对下一位 Agent 的要求

- 先阅读本交接文档、`Agent Rules.md` 和待修改模块的相关实现。
- 不扫描整个项目。
- 非必要不读取大文件，日志分析优先流式处理。
- 保持现有架构和线程模型。
- 保持现有代码风格。
- 修改前分析影响范围。
- 遵守《AI Agent 工作准则》及 `Hand Off生成规范.md`。

发现以下情况立即停止并询问用户：

- 需求不明确；
- 涉及数据库结构调整；
- 涉及接口协议变更；
- 涉及跨模块重构；
- 涉及架构调整；
- 无法确认影响范围。
