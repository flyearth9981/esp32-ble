# 项目协作约定（Claude 必读）

## 编译与验证分工（重要）

- **用户负责**：在 VSCode + ESP-IDF 插件里手动 `build` / `flash` / `monitor`、验证。
- **Claude 负责**：写工程结构、代码、文档。
- **Claude 不要**尝试自己编译、烧录、运行固件（本机沙箱常不可用，且这是用户明确分工）。交付代码后，把 VSCode 执行步骤写清楚给用户。

## 文档标注约定

技术断言带标注：`[KNOWN]` 训练事实 · `[COMPUTED]` 推算 · `[INFERRED]` 推导 · `[COMMON]` 领域常识 · `[FRAME]` 符号系统 · `[GUESS]` 无依据。
置信度：HIGH ≥80% · MED 50–80% · LOW 20–50% · VERY LOW <20%。
`[FRAME]` 与现实类 `[GUESS]` 上限 LOW。不确定就写"待实测"，不编数据。

## 当前阶段

- P1（扫描骨架 + 识别层初版）**已通过**（2026-10-15）：C5 上 NimBLE 能初始化/同步/扫描/收广播，JSON 正常输出，Apple 识别实测正确。

## 关键技术事实（实测，别再搞错）

- **广播上报走 `BLE_GAP_EVENT_EXT_DISC`（枚举值 19），不是 `BLE_GAP_EVENT_DISC`（值 7）**——因为 `CONFIG_BT_NIMBLE_EXT_ADV=y`，C5 所有广播以扩展广播事件投递。`gap_event` 必须处理 EXT_DISC，否则事件全丢（表现为"events 狂涨但 disc=0"）。运行时自报：`[boot] enum DISC=7 EXT_DISC=19`。
- `ble_gap_ext_disc_desc` 字段：`legacy_event_type` / `addr.type` / `addr.val` / `rssi` / `data` / `length_data`。
- Apple Continuity 子类型实测确认：`0x02`=iBeacon、`0x10`=NearbyInfo、`0x12`=FindMy（`0x01` 未确认，代码里标了对冲）。
- **FindMy(0x12) 长格式载荷含 22 字节公钥**（位于 mfg 值字节 3..24），公钥在地址轮换期内稳定——**用它做设备身份可跨 MAC 追踪（F14）**，实测 life_ms 可累积到数百秒。短格式（如 `07ff4c0012020002`）无公钥。
- **普通 Apple 设备（iPhone/AirPods）也发 FindMy 类广播**，无法与 AirTag 区分（Apple 故意如此）。故"跟随告警"不可用（R9），已降级为信息项 `near_persist`。
- **公司 ID 可被冒用**：iBeacon 用 Apple 的 0x004C 作格式标识，实测非 Apple 设备（如 `MiCarFE7C`）广播里 co=76。主身份按"真厂商 > 0x004C > 无"选取以缓解。
- 输出量级：设备密集时表可达 150+ 台，需按 age 过滤（现只打印最近 30s 内见过的）。

## 项目速览

- 平台：ESP32-C5（BLE-only，无经典蓝牙、无测向）。
- 用途：BLE 反追踪。输出：debug 串口 NDJSON，5s 周期设备快照。
- 文档在 `docs/01..08`，入口 `README.md`。

## 本机环境坑（已踩过）

- **IDF 版本**：v5.5.5（明确支持 esp32c5）。
- **ccache 被 Windows 拒绝执行**（`ninja: fatal: CreateProcess: Access is denied`）：
  插件会以命令行 `-DCCACHE_ENABLE=1` 注入，UI 开关拦不住。
  已在顶层 `CMakeLists.txt` 用 `set(CCACHE_ENABLE 0 CACHE BOOL "" FORCE)` 强制禁用。
- **路径大小写**：构建目录缓存过 `F:\` vs `f:\`，出现 `configured for project ... not ...` 时 `idf.py fullclean` 重来。
- **组件依赖**：`main/CMakeLists.txt` 显式写 `REQUIRES` 会覆盖默认公共依赖，`esp_timer` / `nvs_flash` / `bt` 必须自己列。
