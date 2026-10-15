# 08 · VSCode 执行步骤（P1 上板验证）

> 分工：**你在 VSCode 里执行以下步骤**；我负责改代码/文档。请把每步的结果（成功/报错原文）回给我。
> 位置：`F:\dev\esp32-ble`

---

## 0. 一次性准备

1. VSCode 装好 **ESP-IDF 扩展**，并按扩展引导装好一个 IDF 版本。
   - **推荐版本：[KNOWN, MED] v5.5.x 最新稳定版（明确支持 esp32c5）。** 若能选，别用 5.4。
   - **关键前提**：该 IDF 版本必须支持 `esp32c5` target（本工程的地基）。若不支持，下面第 2 步会失败。
   - 不确定版本时，先在命令面板看 `ESP-IDF: Show ESP-IDF Version`，或终端 `idf.py --version`。
2. `文件 → 打开文件夹 → F:\dev\esp32-ble`。
   - 打开后，VSCode 底部状态栏应出现 ESP-IDF 图标（target / 串口 / 烧录按钮）。

---

## 1. 选择目标芯片 = esp32c5

**方式 A（推荐，插件 UI）**：
- 点底部状态栏的 `set-target`（或命令面板 `ESP-IDF: Set Espressif Device Target`）→ 选 **esp32c5**。

**方式 B（终端）**：
```bash
idf.py set-target esp32c5
```

**若之前配置过其他 target，或看到 `Build directory ... configured for project ... not ...`**：
- 确认 VSCode 打开的工程目录始终使用同一种路径写法（例如统一为 `F:\dev\esp32-ble`；不要混用盘符大小写）。
- 在 VSCode ESP-IDF 终端依次运行：
  ```bash
  idf.py fullclean
  idf.py set-target esp32c5
  ```
  `fullclean` 会删除旧 build 缓存；随后 `set-target` 会重新生成对应芯片的配置。

**这一步的结果就是第一个 go/no-go**：
- ✅ 顺利设置 → 你的 IDF 支持 C5，继续。
- ❌ 报 `target esp32c5 not supported` / 未知 target → **STOP，告诉我**。需要升级 IDF 或换平台方案，后面都别做了。
- ✅ 配置后检查 CMake 输出应为 `Building ESP-IDF components for target esp32c5`；若仍显示 `esp32`，不要继续 Build，重新执行 `set-target` 并反馈结果。

---

## 2. 设置串口（用你之前调试过的那块板）

- 插上开发板。底部状态栏点 `select port`，或命令面板 `ESP-IDF: Select port to use`，选你的串口（如 COM5）。
- 状态栏应显示这个串口号。

---

## 3. 编译

- 底部状态栏点 **Build**（齿轮/锤子图标），或命令面板 `ESP-IDF: Build your project`。

**预期**：
- ✅ `Project build complete.`，末尾列出 `ble_antitrack.bin` 大小和分区信息。
- ❌ 若报错，**把完整报错原文回给我**。我最关心的几类：
  - `identify` 找不到 / `REQUIRES identify` 报错 → 组件结构问题
  - NimBLE 的 `ble_hs_id_infer_auto` / `BLE_HCI_ADV_RPT_EVTYPE_*` 未定义 → 头文件/版本问题
  - `nvs_flash` / `esp_timer` 相关 → 极少见，基本排除
  - `CONFIG_ESP_CONSOLE_UART_NUM` 相关 → 控制台配置问题

> ⚠️ 我没法在本机编译（工具链在你这边）。所以**编译报错很正常**，逐个抓原文给我，我来改。

---

## 4. 烧录

- 状态栏点 **Flash**（闪电图标），或 `ESP-IDF: Flash your project`。
- 首次可能提示选烧录方式，选 **UART**，波特率默认即可。

**预期**：`Hash of data verified.` + `Leaving... Hard resetting via RTS pin...`

---

## 5. 打开监视器

- 状态栏点 **Monitor**（插头/显示器图标），或 `ESP-IDF: Monitor your device`。
- 波特率默认 115200。

**预期看到**：先是几行 `[boot] ...` 启动探针，然后每 5 秒一批设备快照：
先一行摘要，再每设备一行，形如：
```json
{"snap":true,"devices":37,"evt":2400,"disc":2400,"period_ms":5000}
{"dev":true,"addr":"28:3c:90:82:45:89","at":0,"co":76,"vendor":"Apple, Inc.","name":"","subtype":"iBeacon","tracker":null,"n":12,"rssi":-88,"rmin":-90,"rmax":-81,"age_ms":340,"life_ms":5820}
```

**成功判据（P1 验收）**：
1. `[boot]` 探针显示 `nimble_port_init rc=0`、`ble_gap_disc rc=0`。
2. 每 5 秒出现一批 `{"snap":...}` + 多行 `{"dev":...}`，且 `snap` 的 `devices` 数 > 0。
3. 有 `co:76` 且 `subtype` 有值的行（Apple 识别通）。
4. 有 `tracker:"apple-findmy"` 的行（追踪器判定通，需附近有 Find My 设备）。

**若开头混入启动日志**：可能看到类似
```
ESP-ROM:esp32c5-...
rst:0x1 (POWERON),boot:0x...
```
这是芯片 ROM 引导器在 IDF 接管前打印的，**无法在 app 里抑制**。它只在开机瞬间出现几行，之后就是纯 NDJSON。校验脚本会容忍这几行（若有异常行把原文发我）。

---

## 6. 校验 NDJSON（可选，二选一）

**方式 A — 抓文件后校验**：
- 终端里（不是插件 monitor）用：
  ```bash
  idf.py -p COM5 monitor > capture.log
  ```
  跑十几秒，Ctrl-] 退出。然后：
  ```bash
  python tools\validate_ndjson.py --file capture.log --expect-co 76
  ```
  脚本会报告：总行数、合法/非法行数、匹配 Apple 的包数。

**方式 B — 直接读串口**（需 `pip install pyserial --break-system-packages`）：
```bash
python tools\validate_ndjson.py --port COM5 --expect-co 76
```

把脚本输出回给我。

---

## 7. 切换到主动扫描（F2 原型）

在 `main\main.c` 的 `start_scan()` 里，把
```c
params.passive = 1;
```
改成
```c
params.passive = 0;
```
重新 Build + Flash + Monitor。此时应能看到 `"evt":"SCAN_RSP"` 的行（主动扫描会收到扫描响应）。

---

## 8. 把结果回给我

请回报：
1. 第 1 步 target 设置是否成功；
2. 编译是否通过（不通过→贴报错原文）；
3. monitor 里 JSON 行的**真实样例**（贴 2–3 行，含 `co`/`subtype`/`tracker`）；
4. 校验脚本输出（若有跑）；
5. 第 7 步主动扫描是否看到 `SCAN_RSP`。

我据此更新代码和数据表，进入 P2。
