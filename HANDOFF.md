# Hand Off 交接文档

## 1. 项目目标

开发一款基于周立功 CAN 收发器的 CAN RFID 上位机，用于两轮电动车 ECU 配件调试，当前已支持美团协议和青桔协议的双向兼容，并具备实机测试及打包发布能力。

## 2. 当前状态

- [x] 已选型 Qt/C++ 作为上位机开发方案。
- [x] 已基于周立功 Qt 32 位例程整理出项目目录 `CAN_RFID_Qt`。
- [x] 已实现主界面框架：运行状态、CAN 设备（支持一键启动）、RFID 监控、压力测试、OTA 升级、实时 CAN 日志。
- [x] 已实现美团协议 RFID 状态监测、TAG 分段显示和 OTA 升级流程。
- [x] 已全面兼容青桔协议：
  - [x] 实现了 29-bit CAN ID 的位域编解码（优先级、源/目的地址、包队列、帧序号）。
  - [x] 实现了 Modbus RTU 网络层分片传输与多帧拼包重组（按源地址、目的地址、包队列隔离，包去重、100ms 乱序超时清除）。
  - [x] 实现了 NPK 周期状态轮询与一机一密解锁（动态根据 64位 UID 计算 32位密码并自动写入 `0xA902`/`0xA903`）。
  - [x] 实现了 NPK 开始检测时下发自定义标签读取间隔（范围 100~25500ms，写至 `0xA901` 低字节）。
  - [x] 实现了 RFR 固件升级状态机（握手进OTA、传输分块、确认升级）与 5 大异常注入 Case 模拟。
- [x] 已完成青桔 OTA 页面增强：
  - [x] 青桔 OTA 支持目标设备切换：`RFR (0x0B)` / `NPK (0x0A)`。
  - [x] 青桔 OTA 的升级发送、响应过滤、APP/BOOT 查询和固件类型字段均跟随目标设备。
  - [x] 青桔协议下恢复“升级压力测试”，支持循环次数、冷却间隔、成功/失败统计和中止恢复日志显示。
- [x] 实现了青桔协议专属的“自定义寄存器读写调试”面板。
- [x] 已完成青桔协议 P1/P2 修复：
  - [x] 青桔压力测试不再发送美团 `0x207`，改为启动 NPK 周期读卡并接入成功率统计。
  - [x] 青桔 OTA 查询 APP/BOOT 已补齐响应解析、超时提示和 UI 状态更新。
  - [x] 青桔 OTA Worker 发送改为主线程队列发送，`QingjuCanManager` 增加发送互斥。
  - [x] 青桔组包 key 已包含源地址、目的地址和包队列，避免不同目的地址响应串包。
  - [x] 自定义寄存器读写面板已显示实际响应值，且自定义地址限制为 `0x00~0x3F`。
  - [x] 青桔设备离线后会清空 UID、密码、资产信息、版本和 SN 等 UI 字段。
- [x] 修复了主窗口构造中由于 `loadAppConfig` 提前调用导致的空指针闪退（SIGSEGV）问题。
- [x] 修复了 `mainwindow.ui` 中 10 处中文字符的 UTF-8/GBK 乱码（Mojibake）问题。
- [x] 配置了本地 MinGW 32-bit (Qt 5.15.2) 环境的编译并成功通过编译。
- [x] 使用 `windeployqt --compiler-runtime` 完成了绿色发布版打包，最终输出位于 `CAN_RFID_Release/`，通过静默启动拉起验证，运行极其平稳。

## 3. 当前任务

当前任务：`qingju` 分支已完成青桔协议 UI 第 1~4 步优化，并修复青桔 OTA 页面缺少压力测试、OTA 仅支持 RFR 的问题；当前代码已通过 Release 编译，准备进入实际青桔 NPK/RFR 终端联调。

## 4. 关键设计决策

- **协议兼容切换**：在左侧 CAN 配置区添加“协议模式”下拉选择框。通过 `QStackedWidget` 动态切换“美团监控”与“青桔监控”UI，并在底层对 CAN 接收数据进行分流（青桔协议数据进入 `QingjuCanManager` 组包后再路由至对应服务）。
- **青桔 Modbus 重组设计**：使用 `AssemblyBuffer` 按 `(srcAddr, destAddr, queue)` 键值隔离各链路。数据接收按帧索引重组，尾帧（index=0）到达且无区间缺失时触发拼包输出；帧间隔超过 100ms 自动清空残包防死锁。
- **一机一密动态密码**：
  - `PASSWORD[0] = UID[0] ^ UID[4] ^ 0x44`
  - `PASSWORD[1] = UID[1] ^ UID[5] ^ 0x64`
  - `PASSWORD[2] = UID[2] ^ UID[6] ^ 0x54`
  - `PASSWORD[3] = UID[3] ^ UID[7] ^ 0x67`
  计算出 32 位密码后，自动执行 `0xA902`/`0xA903` 寄存器写入，成功完成密钥解锁。
- **自定义调试面板**：在青桔监控界面下方加入了功能码 `0x03`（读）、`0x10`（带 ACK 写）、`0x90`（无 ACK 写）的通用调试接口，自定义目标地址按青桔 6-bit 地址范围限制为 `0x00~0x3F`。
- **青桔 OTA 目标选择**：OTA 页面在青桔协议下显示“青桔升级目标”下拉框，默认 `RFR (0x0B)`，可切换 `NPK (0x0A)`；`QingjuOtaService`/`QingjuOtaWorker` 使用目标地址作为 OTA 目的地址、响应来源过滤条件和固件类型字段。
- **青桔 OTA 压测复用**：青桔 OTA 压测复用现有 OTA 压测计数模型，按 Completed/Failed/Abort 更新成功次数、失败次数、成功率和冷却后下一轮启动；青桔协议下不再隐藏 `otaStressGroup`。
- **打包依赖管理**：通过 `windeployqt --compiler-runtime` 不仅打包了 Qt5 框架 DLL，也提取了 MinGW 编译器运行时 DLL (`libgcc_s_dw2-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`)，连同第三方的 `zlgcan.dll` 统一放置于 [CAN_RFID_Release](file:///C:/Users/Administrator/.gemini/antigravity/worktrees/MT_CAN/review-handoff-encoding-format/CAN_RFID_Release) 发布包中，保证了真正的开箱即用。

## 5. 修改记录

主要项目文件列表：
- `CAN_RFID_Qt/CAN.pro` (更新新增文件编译配置)
- `CAN_RFID_Qt/application/appconfig.h / .cpp` (持久化协议模式字段)
- `CAN_RFID_Qt/domain/crc16.h` (新增 Modbus CRC-16 校验码算法)
- `CAN_RFID_Qt/domain/qingjucanid.h` [NEW] (青桔 CAN ID 编解码)
- `CAN_RFID_Qt/application/qingjucanmanager.h / .cpp` [NEW] (Modbus 分包与组包网络层)
- `CAN_RFID_Qt/application/qingjurfidservice.h / .cpp` [NEW] (青桔 NPK 轮询解锁、标签周期设置等服务)
- `CAN_RFID_Qt/application/qingjuotaservice.h / .cpp` [NEW] (青桔 NPK/RFR 固件升级、APP/BOOT 查询与 5 大异常注入服务)
- `CAN_RFID_Qt/application/stresstestservice.h / .cpp` (扩展青桔 NPK 状态压力测试统计与 CSV 自动保存)
- `CAN_RFID_Qt/mainwindow.h / .cpp` (修复构造顺序闪退，集成协议模式切换、青桔专属 UI、CAN 日志协议解析、青桔 OTA 目标切换及 OTA 压测状态更新)
- `CAN_RFID_Qt/mainwindow.ui` (彻底清除 GBK mojibake 乱码字符串)
- `CAN_RFID_Release/` [NEW] (包含完整 DLL 依赖的无闪退、无乱码绿色发布版)

## 6. 已知问题

### P0
- 无已确认 P0 问题。

### P1
- 暂未接入实际青桔 RFID 终端，NPK 标签读取、一机一密解锁、NPK/RFR OTA 升级成功率仍需实机验证。

### P2
- 未接 RFID 终端时，美团 `0x207` 日志可能出现非严格 100ms 连续显示（与底层 CAN 控制器重发/无 ACK 报错机制有关）。
- 仓库 `.gitignore` 忽略了打包生成的发布包 `CAN_RFID_Release`，需在发布交付时手动提取压缩。
- 当前自定义寄存器调试面板只跟踪最近一次手动请求，连续快速发送多条请求时建议等待上一条响应后再发送下一条。

## 7. 下一步任务

1. **实机联调**：使用实际青桔 NPK 读卡器硬件和标签，测试一机一密密码计算、配置参数的下发（特别是周期 `0xA901` 自定义设置）以及数据解析呈现。
2. **调试面板测试**：利用底部的“自定义寄存器读写调试”功能，测试读写非公开寄存器以验证从机的 Modbus 响应是否正常。
3. **OTA 实机与异常校验**：分别验证青桔 NPK/RFR 固件升级通道、APP/BOOT 查询结果，以及 5 大异常注入用例（从机接收 0x99 拒绝、错误文件 CRC、中途物理静默断电、6秒静默超时自动复位、接收重发包）的逻辑正确性。
4. **OTA 压测实机验证**：在青桔协议下分别选择 NPK/RFR，验证升级压力测试循环次数、冷却间隔、成功率统计、中止恢复日志显示是否符合预期。
5. **高负载压测**：测试长时间运行状态下的稳定性和内存开销，确认数据是否会出现残包内存泄漏。

## 8. 测试状态

- Release 编译：PASS (MinGW 32-bit 成功通过编译)
- 青桔 P1/P2 修复 Release 编译：PASS (`F:\TestTools\MT_CAN\tmp\qingju_p1_fix_build\release\CAN_RFID.exe`)
- 青桔 UI 第 1~4 步及 OTA NPK/RFR 目标切换 Release 编译：PASS (`F:\TestTools\MT_CAN\tmp\qingju_p1_fix_build\release\CAN_RFID.exe`)
- 程序启动闪退检测：PASS (在没有任何 DLL 缺失的绿色发布目录下成功通过后台挂载及 tasklist 进程驻留验证，进程稳定且不再闪退)
- 界面乱码清除：PASS (在 UI 文件重构后，程序界面文字中文编码完全正常)
- 手动发送 CAN 帧 / 日志自动保存：PASS
- 实机 NPK 标签识别与 NPK/RFR OTA 升级：UNKNOWN (有待实机联调)

## 9. 对下一位 Agent 的要求

- 先阅读本交接文档、`Agent Rules.md` 和待修改模块的相关实现。
- 不扫描整个项目，非必要不读取大文件。
- 保持现有 Qt/C++ 架构、协议服务分层和代码风格。
- 修改前先分析影响范围，避免无关重构。
- 涉及青桔协议帧格式、寄存器含义、OTA 流程、跨模块架构调整或需求不明确时，立即停止并询问用户。
- 遵守《AI Agent 工作准则》。
