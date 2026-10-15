# ESP32-C5 BLE 反追踪扫描器

基于 ESP32-C5-WROOM-1 / ESP32-C5-DevKitC-1 的便携 BLE 扫描与设备识别工具。
**当前状态：P1–P5 已完成**（扫描 / 解析 / 厂商与 Apple 识别 / 按 FindMy 公钥追踪轮换设备 / 设备汇总）。
**注意**：原始"反跟踪告警"目标经实测判定不可达，功能已相应降级——详见 `docs/09_阶段验收报告.md`，动手前请先读它。

> 平台边界：ESP32-C5 是 **BLE-only**（无经典蓝牙）、**无测向**。详见 `docs/`。

---

## 目录

```
esp32-ble/
├── CMakeLists.txt          # ESP-IDF 顶层工程
├── sdkconfig.defaults      # 默认配置（esp32c5 / NimBLE observer / 串口仅 JSON）
├── main/
│   ├── main.c              # P1/P2 固件：NimBLE 扫描 + 识别 + 设备汇总输出
│   └── identify/           # 识别层：厂商表 / Apple 子类型 / 追踪器
│       ├── company_db.c    # F4 厂商表（读 company_db_data.inc）
│       ├── company_db_data.inc  # 厂商表数据（fallback；可用生成器覆盖）
│       ├── apple.c         # F5/F6 Apple + Continuity 子类型
│       └── tracker.c       # F7 追踪器特征
├── tools/
│   ├── gen_company_db.py   # 用 SIG CSV 生成完整厂商表（O6）
│   └── validate_ndjson.py  # 校验串口输出为合法 JSON
└── docs/                   # 项目文档
    ├── 01_开发计划书.md
    ├── 02_功能列表.md
    ├── 03_系统架构.md
    ├── 04_开发计划与里程碑.md
    ├── 05_协议与数据参考.md
    ├── 06_风险与合规.md
    ├── 07_开放问题.md
    ├── 08_VSCode执行步骤.md   # ← 你在 VSCode 里的执行清单
    └── 09_阶段验收报告.md     # ← 阶段结论与能力边界
```

## 输出格式（P2 起：设备汇总）

不再逐包打印（~110 行/秒，不可读）。固件维护按 MAC 归并的设备表，
每 5 秒输出一次快照：一行摘要 + 每设备一行。示例：

```json
{"snap":true,"devices":159,"shown":42,"hidden":117,"evt":43756,"disc":43756,"scanrsp":2684,"period_ms":5000}
{"dev":true,"key":"addr","addr":"28:3c:90:82:45:89","at":0,"co":33442,"vendor":"","name":"SGM2e7fb7","subtype":"unknown","tracker":null,"n":172,"rssi":-97,"rmin":-98,"rmax":-90,"age_ms":790,"life_ms":192005,"near_persist":false}
{"dev":true,"key":"fmkey","addr":"c0:9c:48:0b:87:d8","at":1,"co":76,"vendor":"Apple, Inc.","name":"","subtype":"FindMy","tracker":"apple-findmy","n":28,"rssi":-76,"rmin":-89,"rmax":-76,"age_ms":248,"life_ms":188702,"near_persist":false}
```

摘要行的 `shown`/`hidden`：本批次打印的设备数 / 因超时未打印的僵尸条目数（默认只打印最近 30s 见过的）。
`key` = 身份键类型（`addr` 按 MAC / `fmkey` 按 FindMy 公钥，见 docs/03 §4.3c）。
`near_persist` = **信息项**：追踪器类设备持续近距。普通 Apple 设备也会为 true，**不代表被跟踪**（见 docs/06 R9）。告警功能已按实测降级移除。
逐包原始输出可把 `main/main.c` 顶部 `EMIT_RAW_PER_PACKET` 设为 `1` 打开。

## 补全厂商表（O6）

厂商表由 SIG 官方《Assigned Numbers》文档生成，两步：

```bash
# 1) 从 PDF 抽取公司标识（PDF 放在 docs/，或用任意来源的同款文档）
pip install pdfplumber --break-system-packages
python tools\extract_company_ids.py "docs\Assigned_Numbers (1).pdf" -o company_ids.csv --dump-text company_ids_raw.txt

# 2) 生成 C 表并覆盖 fallback
python tools\gen_company_db.py company_ids.csv -o main\identify\company_db_data.inc

# 3) 重新编译
idf.py build
```

说明：PDF 文本抽取常吃掉名字里的空格（`NokiaMobilePhones`），
`gen_company_db.py` 默认在驼峰边界补空格；加 `--raw-names` 可关闭。
截至 2026-10-15 抽取得到 4048 条，含 Apple 0x004C。

---

## 前置

- **ESP-IDF**：**建议 v5.5.5**。[KNOWN, MED] 5.5.x 明确支持 `esp32c5`。
  - 若你的环境还提供 v6.0.3 / v6.1：代码按 5.x API 写，6.x 未验证；**优先 5.5.5**，5.5.5 上遇 C5/NimBLE 异常再试 6.0.3。理由见 `docs/08`。
  - 代码未由本机编译验证，隐含的 API 依赖见 `main/main.c` 头注释。
- VSCode + ESP-IDF 插件（你已有）。用插件打开本目录即可识别为 IDF 工程。

## 构建与烧录（命令行等价）

```bash
idf.py set-target esp32c5
idf.py build
idf.py -p <你的串口> flash monitor
```

在 VSCode 里用 ESP-IDF 插件的 Build / Flash / Monitor 按钮等价操作。

## 输出与校验

固件默认每 5 秒打印一批设备快照（见上方格式）到 debug 串口。
`sdkconfig.defaults` 已把日志级别设为 None，避免 ESP_LOG 行污染数据流。

校验（另开终端）：

```bash
python tools/validate_ndjson.py --port <你的串口>
# 或先用 idf.py monitor 存成文件，再：
python tools/validate_ndjson.py --file capture.log --dump-devices
```

---

## 阶段结论

**P1–P5 已完成并真机验证。** 详细的"做了什么 / 实测结论 / 未能达成的目标及原因 / 已知局限"见
**`docs/09_阶段验收报告.md`**——动手或评估本项目前请先读它。

一句话结论：本项目是**BLE 设备发现与分类工具**；"可靠判定谁在跟踪你"经实测判定**不可达**，
原因是 Apple Find My 有意使 AirTag 与普通设备不可区分，且地址轮换限制持续关联。

---

## 重要提醒

- 本项目采集他人设备广播信息，**公开/产品化前须过法务合规**（见 `docs/06`、`docs/07` O1）。
- 本项目**不含**也**不建议**用于任何未经授权的监控用途。

---

## 许可证

本项目代码与文档以 **MIT License** 授权，见 `LICENSE`。

**注意**：`company_ids.csv` 与 `main/identify/company_db_data.inc` 由 Bluetooth SIG
《Assigned Numbers》文档派生。MIT 仅覆盖本项目自身代码与文档，**不重新许可**该第三方数据。
SIG 的版权源文档与全文抽取**未包含在本仓库中**（见 `.gitignore`）。
