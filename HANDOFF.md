# Hand Off 交接文档

## 1. 项目目标

开发一款基于周立功 CAN 收发器的 CAN RFID 上位机，用于两轮电动车 ECU 配件调试，当前处于整体框架和基础功能验证阶段。

## 2. 当前状态

- [x] 已选型 Qt/C++ 作为上位机开发方案。
- [x] 已基于周立功 Qt 32 位例程整理出项目目录 `CAN_RFID_Qt`。
- [x] 已实现主界面框架：运行状态、CAN 设备、RFID 监控、压力测试、OTA 升级、实时 CAN 日志。
- [x] 已实现 CAN 设备打开、初始化、启动、关闭和一键启动。
- [x] 已实现 `0x207` RFID 控制帧 100ms 周期发送。
- [x] 已实现 CAN 实时日志显示、手动保存、自动保存开关。
- [x] 已实现 RFID 协议基础解析和 TAG 分段显示。
- [x] 已实现压力测试基础统计、目标时长、目标次数、CSV 自动保存开关。
- [x] 已实现 OTA 基础框架、ISO-TP 传输、查询/升级流程、错误注入配置。
- [x] 已完成本地 Git 仓库初始化并推送至 `https://github.com/xyd-seven/MT_CAN_RFID`。
- [x] 已成功在新分支 `dev` 验证本地 Qt 5.15.2 MinGW 32-bit 构建脚本和构建输出（生成 `release/CAN_RFID.exe`）。
- [x] 已配置 `zlgcan.dll` 自动复制到 release 运行目录。
- [ ] 未接入 CAN RFID 终端完成实机协议验证。
- [ ] 未完成 OTA 实机升级验证。
- [ ] 未完成长时间压力测试验证。

## 3. 当前任务

当前任务：切换至新分支 `dev` 并生成新的交接说明，准备在新会话中基于新分支开展实地联调测试与功能完善。

涉及模块：
- 所有模块

涉及文件：
- `CAN_RFID_Qt/` 目录下所有源文件

## 4. 关键设计决策

- 使用 Qt/C++ 开发，目标环境为 Qt 5.15.2 MinGW 32-bit。
- 保留周立功 ZLG CAN 二次开发库作为底层 CAN 访问能力，项目内使用 `CAN_RFID_Qt/third_party/zlgcan`。
- UI 采用单窗口布局：顶部运行状态，左侧 CAN 配置，右侧多 Tab，底部实时 CAN 日志。
- RFID 业务使用固定 CAN 帧：`0x207` 控制帧，`0x2C1/0x2C2/0x2C3` 等状态/TAG 响应帧。
- `0x207` 周期发送由 Qt `QTimer` 以 100ms 触发；未接入终端时 CAN 无 ACK 可能导致实际发送节奏受底层控制器影响。
- ZLG 接收帧 `timestamp` 注释为 us，但实际按 ms 处理；代码使用首帧设备时间戳映射到主机时间。
- 压力测试 CSV 自动保存默认关闭，CAN 日志自动保存默认关闭。
- 长时间日志写入采用批量 flush，避免每帧频繁刷盘。
- OTA 使用 `OtaService` + `IsoTpTransport` 分层，后续协议细节优先在 Application/Domain 层补齐。
- `.gitignore` 排除 Qt 构建产物、EXE/DLL/PDB、原始周立功例程库；当前仓库主要提交项目源码、协议 PDF、图标、规则文件。

## 5. 修改记录

修改/新增文件：
- `HANDOFF.md` (更新了构建状态与分支信息)

主要项目文件列表：
- `CAN_RFID_Qt/CAN.pro`
- `CAN_RFID_Qt/main.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/mainwindow.ui`
- `CAN_RFID_Qt/canthread.h`
- `CAN_RFID_Qt/canthread.cpp`
- `CAN_RFID_Qt/rfidprotocol.h`
- `CAN_RFID_Qt/rfidprotocol.cpp`
- `CAN_RFID_Qt/application/appconfig.h`
- `CAN_RFID_Qt/application/appconfig.cpp`
- `CAN_RFID_Qt/application/logservice.h`
- `CAN_RFID_Qt/application/logservice.cpp`
- `CAN_RFID_Qt/application/otaservice.h`
- `CAN_RFID_Qt/application/otaservice.cpp`
- `CAN_RFID_Qt/application/rfidservice.h`
- `CAN_RFID_Qt/application/rfidservice.cpp`
- `CAN_RFID_Qt/application/stresstestservice.h`
- `CAN_RFID_Qt/application/stresstestservice.cpp`
- `CAN_RFID_Qt/domain/canframe.h`
- `CAN_RFID_Qt/domain/canframe.cpp`
- `CAN_RFID_Qt/domain/crc16.h`
- `CAN_RFID_Qt/domain/isotptransport.h`
- `CAN_RFID_Qt/domain/isotptransport.cpp`
- `CAN_RFID_Qt/third_party/zlgcan/zlgcan.h`
- `CAN_RFID_Qt/third_party/zlgcan/config.h`
- `协议文件/CAN总线.pdf`
- `协议文件/美团助力车--OTA协议.pdf`
- `协议文件/美团助力车-主控_RFID定位器协议（CAN）.pdf`
- `图标文件/MT_RFID.ico`
- `图标文件/MT_RFID.svg`

## 6. 已知问题

### P0
- 无已确认 P0 问题。

### P1
- 未接入实际 CAN RFID 终端，RFID 帧解析、TAG 完整性、在线状态、压力测试成功率均未完成实机验证。
- OTA 查询、传输、升级结束和错误注入流程未完成实机验证。

### P2
- 未接 RFID 终端时，`0x207` 日志可能出现非严格 100ms 连续显示；初步判断与 CAN 总线无 ACK/底层错误恢复有关。
- 仓库忽略了运行所需的部分二进制文件，其他机器运行前可能需要手动准备 `zlgcan.dll` 和 Qt 运行时。
- `CAN_RFID_Qt` 目录下仍可能存在本地构建生成文件，但已通过 `.gitignore` 排除。
- 自动化测试覆盖不足，当前主要依赖编译和人工/半自动冒烟测试。

## 7. 下一步任务

1. 接入 CAN RFID 终端，验证 `0x207` 周期控制帧和 `0x2C1/0x2C2/0x2C3` 接收解析。
2. 验证完整 TAG 拼接、卡状态、故障状态、设备 ID、版本号显示。
3. 执行压力测试：不限时、不限次数、限定时长、限定次数、CSV 自动保存。
4. 验证 CAN 日志保存：手动保存、自动保存开关、长时间运行文件大小和时间戳。
5. 验证 OTA：查询程序位置、选择固件、开始升级、中止升级、错误注入。
6. 增加 CAN 错误状态显示，优先关注 ACK Error、Error Passive、Bus Off。
7. 增加项目 README，说明 Qt 版本、编译方式、运行依赖、zlgcan 动态库准备方式。
8. 根据实机测试结果补充最小化自动测试或协议解析单元测试。

## 8. 测试状态

- Release 编译：PASS (已在本地 MinGW 32-bit 验证)
- 程序启动：PASS
- 已接 CAN 收发器、未接 RFID 终端的一键启动：PASS
- CAN 设备关闭：PASS
- `0x207` 周期发送 UI 日志显示：PASS
- 手动发送 CAN 帧：PASS
- `显示0x207` 过滤开关：PASS
- RFID 终端实机识别：UNKNOWN
- 压力测试长时间运行：UNKNOWN
- 压力测试 CSV 内容正确性：UNKNOWN
- CAN 日志自动保存长时间运行：UNKNOWN
- OTA 查询和升级：UNKNOWN

## 9. 对下一位 Agent 的要求

- 先阅读相关实现，再修改代码。
- 不扫描整个项目。
- 非必要不读取大文件，尤其不要反复读取 PDF 和构建产物。
- 保持现有架构。
- 保持现有代码风格。
- 修改前分析影响范围。
- 遵守《AI Agent 工作准则》。
- 优先读取 `Agent Rules.md`、本文件、用户当前需求、相关模块源码。
- 优先使用 `rg` 定位代码。
- 不要提交构建产物。
- 不要擅自修改协议语义。

发现以下情况立即停止并询问用户：
- 需求不明确
- 涉及数据库结构调整
- 涉及接口协议变更
- 涉及跨模块重构
- 涉及架构调整
- 无法确认影响范围
