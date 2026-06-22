# qingju 分支 RS485/OTA 代码审查报告

审查日期：2026-06-18
审查对象：`qingju` 分支最近提交及当前本地未提交改动
目标工作树：`C:\Users\Administrator\.gemini\antigravity\worktrees\MT_CAN\review-handoff-encoding-format`

## 1. 审查范围

### 代码范围

- `c7696fb docs: update HANDOFF.md and fix OTA services thread-safety, crash-on-exit, and serial noise vulnerabilities`
- `7aefaa8 fix: stabilize rs485 polling and update handoff`
- 当前本地未提交改动：
  - RS485 通信线程化：新增 `Rs485Worker`
  - 实时日志 50ms 批量刷新
  - `Rs485State` 跨线程元类型注册

### 协议依据

- `协议文件\哈啰\哈啰助力车RFID通信协议_Ver2.pdf`（已用 `pypdf` 抽取文本复核）
- `协议文件\485通讯协议\RFID通信协议及处理(BB协议)V1.0.docx`
- `协议文件\485通讯协议\RFID通信协议及处理V1.4（FF协议）.docx`
- `协议文件\485通讯协议\OTA升级\RFID升级流程2022.1.28.docx`
- `协议文件\485通讯协议\OTA升级\OTA远程升级规范协议文档V1.1.1.pdf`（已用 `pypdf` 抽取文本复核）

补充说明：最初默认 `python/py` 入口命中了 Windows Store 占位启动器，后续已改用真实 Python 路径 `C:\Users\Administrator\AppData\Local\Programs\Python\Python39\python.exe`，并通过已安装的 `pypdf` 成功抽取两个 PDF 文本。以下结论已纳入 PDF 原文复核结果。

## 2. 总体结论

当前代码在架构方向上是正确的：RS485 串口对象已迁移到独立工作线程，GUI 线程只通过 queued signal/slot 调度串口操作；日志也改为 50ms 批量刷新，能降低缩放窗口时 GUI 表格重绘对实时通信的影响。

协议实现方面，BB/FF 基础读卡、设备信息、功率、FF 解调参数与文档基本一致。BB/FF OTA 的动态分包大小与 `OTA远程升级规范协议文档V1.1.1.pdf` 一致，但与旧流程 DOCX 中“非结尾包 256 字节”的描述存在文档差异，需要通过实机确认设备固件口径。本轮已修复哈啰 OTA 最终跳转成功误判、FF 地址校验、哈啰寄存器异常诊断和编译 warning。

## 3. Findings

### P2 - BB/FF OTA 分包大小存在 PDF 与流程 DOCX 口径差异，需要以设备实现确认（已加日志保护）

文件：`CAN_RFID_Qt\application\bbffotaservice.cpp`
位置：`BbFfOtaWorker::run()` 约 228-282 行

代码在开始升级应答中读取 `resp[1..2]` 作为 `negotiateChunkSize`，并允许 64-1024 字节，然后按该值切分固件包：

- `negotiateChunkSize = (resp[1] << 8) | resp[2]`
- 非法时才回退 256
- 数据包 `sendDataPacket(expectedId, chunk)` 使用协商大小

`OTA远程升级规范协议文档V1.1.1.pdf` 明确说明开始升级应答包含 `size 2byte 每个升级数据包大小(小于0xffff-4)`，且数据包 `data Nbyte`，最后一个数据包可不足分段大小。因此当前代码支持从机协商分包大小是符合 PDF 规范的。

同时，`RFID升级流程2022.1.28.docx` 写到：非结尾包升级数据大小为 256 字节，结尾包大小为升级包总大小模 256 的余数。两份文档存在口径差异。如果现场设备固件按旧流程 DOCX 固化为 256 字节，当前代码在从机返回非 256 包大小时仍可能出现实机兼容问题；但从 PDF 主规范看，这不是明确代码错误。

建议：

- 与设备固件负责人确认 BB/FF RFID 读卡器实际以 PDF 的 `size` 字段为准，还是以旧流程 DOCX 的固定 256 字节为准。
- 若实机固件返回 256，当前实现无需修改。
- 若实机固件返回非 256，应重点跑 OTA 边界包长测试，确认设备端接受动态长度。

处理状态：代码已保留 PDF 的动态分包逻辑，并对设备返回的异常 size 回退 256；当 size 非 256 时输出运行日志，便于实机确认。

### P1 - 哈啰 OTA 最终跳转命令无论是否收到有效应答都会显示升级成功（已修复）

文件：`CAN_RFID_Qt\application\hlotaservice.cpp`
位置：约 293-319 行

代码发送寄存器 301 写 1 后，最多重试 3 次，但不管是否收到 `0x10` 写应答、是否连续写失败，最后都会执行：

```cpp
emit statusUpdated(6, QStringLiteral("升级成功! 设备已重启跳转"), 100);
```

注释认为设备可能立即重启导致无响应，因此把超时视为成功。这个策略可以接受“最后一帧已成功写出但设备重启无应答”的场景，但当前代码没有区分：

- `sendWritePacket(301, 1, regVal)` 成功写出后无应答
- 串口写入失败
- 连续 Modbus 异常
- 设备完全无响应

原问题会导致串口已断开或写入失败时也可能显示成功，实机会造成误判。

建议：

- 增加 `jumpCommandSent` 标志。只有至少一次 `sendWritePacket()` 返回 true 后，才允许“无应答视为可能成功”。
- 若 3 次写入均失败，应显示失败。
- 若收到 Modbus exception，应显示失败，不应继续归为成功。

处理状态：已增加 `jumpCommandSent`。只有寄存器 301 写命令至少成功发出一次，才允许将后续无应答视为设备重启成功；若 3 次写入均失败，显示升级跳转失败。

### P2 - FF 帧解析未校验地址字段，可能接收非本设备或噪声帧（已修复）

文件：`CAN_RFID_Qt\application\rs485manager.cpp`
位置：约 382-390 行

FF 协议帧格式固定为：

```text
0xFF 0x02 CMD LEN DATA CRC_H CRC_L
```

当前解析只验证 CRC，没有验证 `frame[1] == 0x02`。虽然注释写着“for flexibility”，但协议文档没有描述多地址场景。高频串口噪声或总线上存在其他地址设备时，合法 CRC 的非 0x02 地址帧会被业务层处理。

建议：

- 默认严格校验地址 `0x02`。
- 如确实需要兼容多地址，应把地址做成配置项，并在日志中显示实际地址。

处理状态：已在 `processFfBuffer()` 中增加 `frame[1] == 0x02` 校验，非目标地址帧会被丢弃并重新同步。

### P3 - 哈啰通用寄存器解析已与 PDF 对齐，但可增加范围校验提升可诊断性（已增强）

文件：`CAN_RFID_Qt\application\hlotaservice.cpp`
位置：约 160-176 行
文件：`CAN_RFID_Qt\application\rs485rfidservice.cpp`
位置：约 683-708 行

哈啰 Ver2 PDF 表 2 定义寄存器 `0..6` 分别为软件版本、硬件版本、协议版本、厂家标识、版本类型、预留、项目编号，每个 1 个 `uint16_t`，共 7 个寄存器，即 14 字节。当前代码读取寄存器 `0..6` 并按 14 字节解析，与 PDF 一致。

剩余问题不是协议错误，而是可诊断性不足：代码没有校验协议版本应为 3、厂家标识常见值 10136、版本类型应为 1/2。当现场设备返回异常值时，界面仍会按 APP/BOOT 展示，排查信息不够明确。

建议：

- 对 `mfgId`、`verType`、协议版本范围增加合理性校验。
- 报告未知值时不要直接判定 APP/BOOT，避免误导。

处理状态：已对版本类型 `verType` 增加 `BOOT/APP/UNKNOWN` 区分；协议版本非 3 时会写入错误提示，便于现场诊断。

### P2 - 实时日志批量刷新降低 UI 压力，但表格仍使用 `QTableWidget` 大量 item，长期高频压测仍有主线程成本

文件：`CAN_RFID_Qt\mainwindow.cpp`
位置：`AddDataToList()` / `flushPendingLogRows()` 约 3019-3068 行

本地改动已将逐帧插入改为 50ms 批量 flush，并在 flush 期间关闭表格重绘，这是正确优化。但 `QTableWidget` 仍会为每个单元格创建 `QTableWidgetItem`。当前最大 1000 行、10 列，即最多约 10000 个 item，短期压力可接受；如果后续提高最大行数或开启长时间压测，仍可能造成明显 GUI 开销。

建议：

- 保持当前 1000 行上限。
- 若后续还出现缩放卡顿，下一步应把实时日志表改为 `QTableView + QAbstractTableModel` 环形缓冲模型。

### P2 - 编译仍有既有 warning，影响后续回归判断（已清理）

当前 Release 构建通过，但仍存在既有 warning：

- `MainWindow::qjOtaAnomalyEnableCheck` 与 `qjRfidPeriodSpin` 初始化顺序不一致。
- `QString::split(QRegExp)` 已弃用。

这两个问题不是本轮 RS485/OTA 逻辑错误，但会干扰后续判断“本次修改是否引入新 warning”。

建议：

- 单独小提交修复成员声明/初始化顺序。
- 将 `QRegExp` split 改为 Qt 5.15 推荐写法，例如 `QRegularExpression`。

处理状态：已调整 `MainWindow` 初始化顺序，并将相关正则匹配/分割迁移到 `QRegularExpression`。Release 构建无 warning。

## 4. 已验证为基本一致的点

### BB 基础协议

- 帧头 `0xBB`、帧尾 `0x7E`、长度 2 字节、校验为从帧类型到参数末尾累加低字节：代码实现一致。
- 查询标签 `0x22`、查询版本 `0x03`、查询设备 ID `0x15`、设置/查询功率 `0xB6/0xB7`：命令码一致。
- BB 设备 ID 当前按 HEX 展示，符合当前使用约定，暂不作为问题项。
- 查询标签成功响应按 `RSSI + PC + 12 字节标签 ID + CRC` 解析：与文档一致。
- 无标签错误 `0xFF + 0x15`：代码已处理。

### FF 基础协议

- 帧头 `0xFF`、地址 `0x02`、1 字节长度、CRC16/XMODEM：发送侧实现一致。
- 查询标签 `0x00/0x01`、重启 `0x02/0x03`、查询版本 `0x04/0x05`、查询 ID `0x0C/0x0D`、功率 `0x0E..0x11`、解调参数 `0x12..0x15`、读卡开关 `0x16/0x17`：命令码与文档一致。
- 错误码 `0xFFFF/-1`、`0xFFFE/-2`：代码已处理。

### BB/FF OTA 协议

- OTA 帧格式 `AA 55 + Address + Cmd + Len + Payload + 1 byte checksum`：代码实现一致。
- RFID 读卡器 OTA 地址 `0x20`：发送侧实现一致，接收侧也允许 `0x20`。
- `Len` 和 `Payload` 大端：代码实现一致。
- 开始升级 `0x1A`：payload 为 3 字节版本号 + 4 字节总大小，代码实现一致。
- 开始升级应答：`rc` + 2 字节单包大小，代码按 PDF 读取并使用，符合 PDF。
- 发送数据 `0x1B`：4 字节包序号从 1 开始 + 数据，代码实现一致。
- 结束升级 `0x1C`：4 字节 checksum，代码使用高 2 字节置 0、低 2 字节放 CRC16-CCITT，需实机确认设备是否按 4 字节 checksum 解析低 16 位。

### 哈啰协议

- Modbus RTU、地址 `0x0D`、功能码仅使用 `0x03`/`0x10`：代码实现一致。
- 通用寄存器 `0..6` 解析：代码与 Ver2 PDF 表 2 一致。
- BOOT 寄存器 `300/301/302`：代码分别用于使能下载、快速启动/校验跳转、写 66 寄存器版本数据包，与 PDF 表 3 一致。
- `version_packet_t`：4 字节 offset + 128 字节 payload，共 66 个寄存器，代码实现一致。
- 扫描控制寄存器 `4101` 数量 7、包含 `decrypt_enable`：代码实现一致。
- 查询标签寄存器 `4211` 数量 111：代码使用 4211/111，与 Ver2 变更记录一致。

### 当前本地线程化改动

- `Rs485Manager`、`Rs485RfidService`、`HlOtaService`、`BbFfOtaService` 已统一由 `Rs485Worker` 持有，并运行在 RS485 工作线程。
- 主界面通过信号请求串口打开、关闭、轮询、参数设置和 OTA 操作，避免 GUI 线程直接操作串口。
- `Rs485State` 已注册元类型，支持跨线程 queued signal。
- 协议切换时 worker 内会停止当前轮询，避免旧协议定时器继续发送。

## 5. 建议处理顺序

1. P2：确认 BB/FF OTA 实机固件是否接受 PDF 中的动态分包大小，重点观察设备返回 size 是否为 256。
2. P2：如后续存在多地址 FF 设备，再把 FF 地址从固定 `0x02` 扩展为配置项。
3. P2：继续执行 BB/FF/哈啰 OTA 实机升级验证，覆盖断线、无响应、异常应答和正常重启。

## 6. 测试建议

- BB OTA：使用大小为 `260`、`512`、`1024`、`25KB` 边界固件验证分包和最后一包处理。
- FF OTA：同 BB OTA，重点验证 CRC 和设备端包序号应答。
- 哈啰 OTA：断开串口、设备不响应、Modbus exception、正常重启四类场景分别验证最终状态。
- 窗口缩放压测：BB/FF 1000 次轮询，开启实时日志，拖动缩放窗口，记录轮询跳过次数。
- 协议切换：BB/FF/哈啰轮询运行中切换协议，确认旧协议不再继续发送。
