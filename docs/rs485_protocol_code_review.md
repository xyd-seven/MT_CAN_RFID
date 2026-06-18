# RS485 BB/FF 协议代码检查报告

## 1. 检查范围

- 分支/提交：`qingju` / `de695cd feat: integrate RS485 RFID BB/FF protocols and fix timeout/disconnect bugs`
- 协议文档：
  - `F:\TestTools\MT_CAN\协议文件\485通讯协议\RFID通信协议及处理(BB协议)V1.0.docx`
  - `F:\TestTools\MT_CAN\协议文件\485通讯协议\RFID通信协议及处理V1.4（FF协议）.docx`
- 代码文件：
  - `CAN_RFID_Qt/application/rs485manager.cpp`
  - `CAN_RFID_Qt/application/rs485rfidservice.cpp`
  - `CAN_RFID_Qt/mainwindow.cpp`
  - `CAN_RFID_Qt/application/appconfig.h`
  - `CAN_RFID_Qt/application/appconfig.cpp`

## 2. 协议符合性结论

整体帧格式实现方向正确：

- BB 协议已按 `0xBB + 类型 + 指令码 + 2字节长度 + 数据 + 1字节累加和 + 0x7E` 组包/拆包。
- BB 累加和计算范围与文档一致：从帧类型到最后一个指令参数取低 8 位。
- FF 协议已按 `0xFF + 0x02地址 + 指令码 + 1字节长度 + 数据 + 2字节CRC16/XMODEM` 组包/拆包。
- FF CRC16/XMODEM 使用大端追加，与文档示例一致。
- BB/FF 的主要命令码基本覆盖：读卡、设备信息、设备 ID、功率设置/查询、FF 重启、FF 解调参数、FF 读卡开关查询。

但代码和 UI 中存在若干会影响实机联调的风险点，优先级如下。

## 3. 问题清单

### P1-1 发射功率单位被二次乘以 100，实际下发值错误

**涉及文件**

- `CAN_RFID_Qt/application/rs485rfidservice.cpp`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/appconfig.h`

**依据**

- BB 文档示例：设置 20dBm 下发 `0x07D0`，即十进制 `2000`，单位为 `0.01dBm`。
- FF 文档示例同样使用 `0x07D0` 表示 20dBm。
- UI 默认值为 `2000`，后缀显示为 `(0.01dBm)`。

**当前代码**

- `MainWindow` 中 `bbPowerSpin` / `ffPowerSpin` 默认 `2000`，范围 `0~3300`，显示单位 `(0.01dBm)`。
- `Rs485RfidService::setPower(int powerDbm)` 中又执行 `rawPower = powerDbm * 100`。
- `queryPower()` 响应解析时执行 `rawPower / 100`，但 UI 仍显示为 `(0.01dBm)`。

**影响**

- 用户输入 `2000` 期望下发 `0x07D0`，代码会计算 `200000`，再截断为 `quint16`，实际下发值不符合协议。
- 查询到 `0x07D0` 后 UI 显示 `20 (0.01dBm)`，单位显示错误，容易误导实机调试。

**建议**

- 统一内部状态和 UI 使用协议原始单位 `0.01dBm`。
- `setPower()` 不再乘以 100，直接将 UI 值作为 2 字节大端原始值下发。
- `queryPower()` 不再除以 100，直接保存原始值；如需显示 dBm，可额外显示 `20.00 dBm`。

### P1-2 单次查询按钮当前基本不可用

**涉及文件**

- `CAN_RFID_Qt/application/rs485rfidservice.cpp`
- `CAN_RFID_Qt/mainwindow.cpp`

**依据**

- BB 协议单次查询标签命令：`0x22`。
- FF 协议单次查询标签命令：`0x00`。

**当前代码**

- `Rs485RfidService::triggerSingleQuery()` 开头判断 `if (!m_isScanning) return;`。
- UI 中 `bbQueryOnceBtn` / `ffQueryOnceBtn` 仅在 `serialOpened && !rs485Scanning` 时启用。
- 因此按钮启用时服务认为未扫描并直接返回；服务扫描中时按钮又被 UI 禁用。

**影响**

- 用户点击“单次查询”不会实际发送读卡命令。
- “自动轮询/单次查询”模式在界面上存在，但业务路径不闭合。

**建议**

- 让 `triggerSingleQuery()` 只依赖串口打开状态，不依赖 `m_isScanning`。
- 或者为单次查询模式定义独立状态，不把“扫描中”作为单次查询前置条件。
- UI 上建议将“单次查询”按钮在串口打开时始终可用，自动轮询运行中可按需求禁用。

### P1-3 FF 解调阈值 UI 范围过小，无法设置协议推荐值

**涉及文件**

- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/rs485rfidservice.cpp`

**依据**

- FF 文档中 `Thrd` 为 2 字节。
- 示例推荐最小值为 `0x01B0`，十进制 `432`。

**当前代码**

- `ffThrdSpin->setRange(0, 255)`。
- `ffSetDemodulatorParams()` 实际按 2 字节大端下发阈值。

**影响**

- UI 无法输入文档推荐值 `432`。
- 实机调试时无法覆盖协议允许的常用阈值范围。

**建议**

- 将 `ffThrdSpin` 范围调整为至少 `0~65535`，或根据硬件约束设置更合理范围。
- 默认值建议设置为文档推荐 `432`，并在 UI 标签中显示十进制/十六进制对应关系。

### P1-4 FF 启动检测与设备信息查询共用单一超时状态，存在响应串扰风险

**涉及文件**

- `CAN_RFID_Qt/application/rs485rfidservice.cpp`

**依据**

- FF `0x06` 开始检测应答为 `0x07`。
- FF `0x04` 查询版本应答为 `0x05`。

**当前代码**

- `startScan()` 在 FF 模式下先发送 `0x06`，随后立即调用 `queryDeviceInfo()` 发送 `0x04` 并启动同一个 `m_timeoutTimer`。
- `onPacketReceived()` 收到任意合法包都会调用 `stopTimeoutGuard()`，不校验是否为当前等待的 `m_pendingCmdCode`。

**影响**

- 如果 `0x07` 先到，会停止版本查询的超时守护；如果后续 `0x05` 丢失，用户可能得不到查询超时反馈。
- 如果响应顺序不同，也可能影响后续 `0x0C` 查询 ID 的超时状态。
- 实机串口环境下响应顺序和延迟更不稳定，此处风险较高。

**建议**

- 为等待响应增加期望命令码校验：只有收到 `m_pendingCmdCode` 对应响应才停止当前超时。
- FF 开始检测、查询版本、查询 ID 建议串行化，避免同一时间存在多个未完成请求。
- 自动轮询命令和静态信息查询应使用独立状态，避免互相覆盖。

### P2-1 BB 标签详情字段未显示

**涉及文件**

- `CAN_RFID_Qt/application/rs485rfidservice.cpp`
- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/rs485rfidservice.h`

**依据**

- BB 标签响应数据包含 `RSSI + PC + 标签ID + CRC`。
- UI 已创建 `bbRssiVal`、`bbPcVal`、`bbCrcVal`。

**当前代码**

- `handleBbResponse(0x22)` 只提取 `payload.mid(3, 12)` 作为标签 ID。
- `Rs485State` 没有保存 RSSI、PC、CRC 字段。
- `updateRs485RfidPanel()` 没有更新 `bbRssiVal`、`bbPcVal`、`bbCrcVal`。

**影响**

- UI 上存在字段但永远保持 `-`，联调人员会误以为设备未返回这些信息。

**建议**

- 在 `Rs485State` 中补充 `bbRssi`、`bbPc`、`bbCrc`。
- BB 成功响应长度满足协议时分别解析并更新 UI。

### P2-2 FF “启闭检测”按钮语义不清

**涉及文件**

- `CAN_RFID_Qt/mainwindow.cpp`
- `CAN_RFID_Qt/application/rs485rfidservice.cpp`

**依据**

- FF 协议有开始检测 `0x06`、停止检测 `0x08`、查询读卡开关 `0x16`。

**当前代码**

- UI 按钮文字为“启闭检测”。
- 实际绑定调用 `ffQueryCardSwitch()`，只发送 `0x16` 查询开关状态。

**影响**

- 用户可能误以为该按钮会切换开关，而实际只是查询。

**建议**

- 将按钮文案改为“查询检测状态”或“查询读卡开关”。
- 如果需要切换功能，应新增明确的“开始检测/停止检测”按钮或开关控件，并分别发送 `0x06` / `0x08`。

### P2-3 功率 UI 文案建议统一为 dBm 与协议原始值

**涉及文件**

- `CAN_RFID_Qt/mainwindow.cpp`

**当前问题**

- 输入框后缀为 `(0.01dBm)`，当前值也显示 `(0.01dBm)`，但用户通常更关心 `20.00 dBm`。

**建议**

- 输入框可保留协议原始值：`2000 (0.01dBm)`。
- 当前值建议显示：`2000 (20.00 dBm)`。
- 设置区域可增加说明：`协议单位：0.01dBm，20dBm = 2000`。

## 4. UI 优化建议

1. 单次查询模式下，不建议要求用户先点击“开始轮询”。可改为：
   - “开始轮询”：只用于自动轮询。
   - “单次查询”：串口打开后直接可用。
2. BB 面板的 RSSI、PC、CRC 字段应补齐数据，否则可先隐藏，避免空字段造成误解。
3. FF 解调参数建议显示单位/含义：
   - `Mixer_G`：协议值，可旁注实际增益映射。
   - `IF_G`：协议值，可旁注实际增益映射。
   - `Thrd`：支持十进制输入，同时显示十六进制。
4. FF “启闭检测”建议重命名为“查询读卡开关”，避免和开始/停止检测混淆。
5. 串口波特率文档只明确 BB 9600/115200，FF 示例包含 115200；当前 UI 允许 19200/38400/57600，建议标注“设备需支持”，或根据协议模式限制选项。

## 5. 建议验证用例

### BB

1. 设置功率输入 `2000`，抓取发送帧应为 `BB 00 B6 00 02 07 D0 8F 7E`。
2. 查询功率收到 `BB 01 B7 00 02 07 D0 91 7E` 后，UI 应显示 `2000 (20.00 dBm)`。
3. 点击“单次查询”应立即发送 `BB 00 22 00 00 22 7E`。
4. 收到标签响应后，UI 应显示 EPC/UID、RSSI、PC、CRC。

### FF

1. 设置功率输入 `2000`，抓取发送帧应为 `FF 02 0E 02 07 D0 8E EC`。
2. 解调阈值输入 `432`，发送数据区应包含 `03 06 01 B0`。
3. 点击“单次查询”应立即发送 `FF 02 00 00 25 C3`。
4. 查询读卡开关应发送 `FF 02 16 00 8C 16`，收到 `0x17` 后 UI 显示启用/关闭。

## 6. 总结

RS485 BB/FF 的基础协议栈已经搭起来，帧格式和主要命令码方向正确。但在进入实机联调前，建议优先修复：

1. 功率单位二次乘以 100 的 P1 问题。
2. 单次查询按钮不可用的 P1 问题。
3. FF 解调阈值范围不足的 P1 问题。
4. FF 多命令共用超时守护导致响应串扰的 P1 问题。

这些问题都属于小范围修改，主要集中在 `Rs485RfidService`、`Rs485State` 和 `MainWindow` 的 RS485 面板逻辑，不需要调整项目整体架构。
