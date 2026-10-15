/*
 * BLE 反追踪扫描器 — P1/P2 固件（扫描 + 识别 + 设备汇总）
 *
 * 输出策略（D11 + O9 决定）：不再逐包打印，而是维护一张按 MAC 归并的
 * 设备表，周期（默认 5s）快照输出。每次快照：
 *   - 一行摘要： {"snap":true,"devices":N,"evt":..,"disc":..}
 *   - 每设备一行：{"dev":true,"addr":..,"co":..,"vendor":..,"subtype":..,
 *                  "tracker":..,"n":..,"rssi":..,"rmin":..,"rmax":..,"age_ms":..}
 * 全部设备不筛（D-O9）。逐包原始输出可开 EMIT_RAW_PER_PACKET 调回。
 *
 * 置信度：Apple 子类型 0x02/0x10/0x12 经真机实测确认；其余标 [GUESS,LOW]。
 * EXT_ADV 上报走 BLE_GAP_EVENT_EXT_DISC（本版 C5 枚举值 19）。
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"

#include "company_db.h"
#include "apple.h"
#include "tracker.h"

static const char *TAG = "ble_scan";

#define EMIT_RAW_PER_PACKET 0        /* 1 = 额外逐包原始输出（调试，量大）*/
#define ACTIVE_SCAN         1        /* 1 = 主动扫描（发 SCAN_REQ，收 SCAN_RSP 拿到更多字段）；0 = 纯被动 */
#define MAX_DEVICES         200
#define SNAPSHOT_PERIOD_MS  5000

/* ---- 跟随判定（F12/F13）阈值，可调 ---- */
#define NEAR_RSSI_DBM       (-75)    /* 近距门限：RSSI ≥ 此值视为"近" */
#define PERSIST_MS          60000    /* 持续存在门限：观察时长 ≥ 此值 */
#define PRESENT_RECENT_MS   15000    /* 仍在线门限：age ≤ 此值视为"现在还在" */
#define PRINT_RECENT_MS     30000    /* 输出过滤：只打印 age ≤ 此值的设备，抑制僵尸条目 */

static uint8_t own_addr_type;

/* 诊断计数（快照摘要里带出）*/
static volatile uint32_t s_evt_count  = 0;
static volatile uint32_t s_disc_count = 0;
static volatile uint32_t s_scanrsp_count = 0;   /* 收到的 SCAN_RSP 数 */

/* ---------- 设备表 ---------- */
/* 身份键：默认按 MAC；对 Apple FindMy(0x12) 改按载荷公钥前 6 字节，
 * 因为轮换 MAC 的追踪器无法用 MAC 关联（F14）。 */
#define KEY_ADDR  0
#define KEY_FMKEY 1

typedef struct {
    bool     used;
    uint8_t  key_kind;            /* KEY_ADDR / KEY_FMKEY */
    uint8_t  key[6];              /* 身份键 */
    uint8_t  atype;               /* 最近所见的地址类型（显示用）*/
    uint8_t  addr[6];             /* 最近所见的源地址（显示用）*/
    int64_t  first_ms, last_ms;
    uint32_t count;
    int      rssi_last, rssi_min, rssi_max;
    int      co;                  /* 主身份厂商 ID，-1 无 */
    int      co_pri;              /* 主身份优先级：2=真厂商 1=Apple 0=无 */
    char     vendor[64];
    char     name[64];            /* 原始名（输出时转义）*/
    char     subtype[28];
    char     tracker[20];
} device_t;

static device_t s_dev[MAX_DEVICES];
static SemaphoreHandle_t s_lock;

/* ---------- AD 解析结果 ---------- */
typedef struct {
    int            co;
    const uint8_t *mfg;
    uint8_t        mfg_len;
    uint8_t        flags;
    int            tx;
    char           name[40];
    const uint8_t *fmkey;      /* FindMy(0x12) 22 字节公钥；无则 NULL */
    uint8_t        fmkey_len;
} adinfo_t;

/* ---------- JSON 字符串转义 ---------- */
static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 1 < out_sz; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            if (o + 2 >= out_sz) break;
            out[o++] = '\\'; out[o++] = (char)c;
        } else if (c < 0x20) {
            if (o + 6 >= out_sz) break;
            o += (size_t)snprintf(out + o, out_sz - o, "\\u%04x", (unsigned)c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/* ---------- 解析 AD 结构 ---------- */
static void parse_ad(const uint8_t *ad, uint8_t ad_len, adinfo_t *o)
{
    o->co = -1; o->mfg = NULL; o->mfg_len = 0; o->flags = 0; o->tx = 0x7f;
    o->name[0] = '\0';
    o->fmkey = NULL; o->fmkey_len = 0;

    for (uint8_t i = 0; i + 1 < ad_len; ) {
        uint8_t fl = ad[i];
        if (fl == 0) break;
        if (i + 1 + fl > ad_len) break;

        uint8_t t = ad[i + 1];
        const uint8_t *v = &ad[i + 2];
        uint8_t vl = fl - 1;

        switch (t) {
        case 0x01: if (vl >= 1) o->flags = v[0]; break;
        case 0x08: case 0x09:
            if (vl < sizeof(o->name)) { memcpy(o->name, v, vl); o->name[vl] = '\0'; }
            break;
        case 0x0A: if (vl >= 1) o->tx = (int8_t)v[0]; break;
        case 0xFF:
            o->mfg = v; o->mfg_len = vl;
            if (vl >= 2) o->co = v[0] | (v[1] << 8);
            /* Apple FindMy(0x12) 身份键。
             * [GUESS, LOW] 假设：长格式载荷字节 3..24 为 22 字节公钥。
             * **未验证**——实测只见到短格式（如 07ff4c0012020002，无公钥），
             * 短格式没有 22 字节密钥，此分支不会触发。
             * 若设备只发短格式，需另行校准（见对话/O3）。 */
            if (vl >= 24 && o->co == 0x004C && v[2] == 0x12) {
                o->fmkey = &v[3];
                o->fmkey_len = 22;
            }
            break;
        default: break;
        }
        i += 1 + fl;
    }
}

/* ---------- 归并到设备表 ---------- */
static void upsert(uint8_t atype, const uint8_t *addr, int8_t rssi, const adinfo_t *a)
{
    int64_t now = esp_timer_get_time() / 1000;

    /* 身份键：FindMy 公钥优先，否则用 MAC（F14）*/
    uint8_t key_kind = KEY_ADDR;
    uint8_t key[6];
    if (a->fmkey && a->fmkey_len >= 6) {
        key_kind = KEY_FMKEY;
        memcpy(key, a->fmkey, 6);
    } else {
        memcpy(key, addr, 6);
    }

    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return;

    int idx = -1, free_idx = -1, oldest_i = 0;
    int64_t oldest = INT64_MAX;

    /* pass 1：身份键精确匹配 */
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_dev[i].used &&
            s_dev[i].key_kind == key_kind &&
            memcmp(s_dev[i].key, key, 6) == 0) {
            idx = i; break;
        }
    }
    /* pass 2：跨键类型调和——同一 MAC 已存在但身份键类型不同时合并，
     * 避免同一设备被拆成 fmkey / addr 两条（双向）。fmkey 更稳定，优先升级。 */
    if (idx < 0) {
        int64_t best = -1;
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (!s_dev[i].used) continue;
            if (s_dev[i].key_kind == key_kind) continue;      /* 同类型走 pass 1 */
            if (memcmp(s_dev[i].addr, addr, 6) != 0) continue;
            if (s_dev[i].last_ms <= best) continue;
            best = s_dev[i].last_ms;
            idx = i;
        }
        if (idx >= 0 && key_kind == KEY_FMKEY) {
            /* 该条目原为 addr 键，现收到公钥——就地升级为更稳定的 fmkey 键 */
            s_dev[idx].key_kind = KEY_FMKEY;
            memcpy(s_dev[idx].key, key, 6);
        }
    }
    /* pass 3：未命中则找空位或最旧条目（供分配）*/
    if (idx < 0) {
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (s_dev[i].used) {
                if (s_dev[i].last_ms < oldest) { oldest = s_dev[i].last_ms; oldest_i = i; }
            } else if (free_idx < 0) {
                free_idx = i;
            }
        }
    }

    if (idx < 0) {
        idx = (free_idx >= 0) ? free_idx : oldest_i;   /* 表满则淘汰最旧的 */
        memset(&s_dev[idx], 0, sizeof(device_t));
        s_dev[idx].used     = true;
        s_dev[idx].key_kind = key_kind;
        memcpy(s_dev[idx].key, key, 6);
        s_dev[idx].first_ms = now;
        s_dev[idx].co       = -1;
        s_dev[idx].co_pri   = 0;
        snprintf(s_dev[idx].subtype, sizeof(s_dev[idx].subtype), "unknown");
        s_dev[idx].rssi_min = 127;
        s_dev[idx].rssi_max = -128;
    }

    device_t *d = &s_dev[idx];
    d->last_ms   = now;
    d->atype     = atype;             /* 刷新最近所见地址（轮换后即最新）*/
    memcpy(d->addr, addr, 6);
    d->rssi_last = rssi;
    d->count++;
    if (rssi < d->rssi_min) d->rssi_min = rssi;
    if (rssi > d->rssi_max) d->rssi_max = rssi;

    /* 主身份选择（D-主身份）：优先级 真厂商(2) > Apple/0x004C(1) > 无(0)。
     * 同一 MAC 常带多个厂商标识（如 iBeacon 的 0x004C + 自身 ID），
     * 取优先级更高者为该设备主身份，避免被最易冒用的 0x004C 覆盖。 */
    if (a->co >= 0) {
        int pri = (a->co == 0x004C) ? 1 : 2;
        if (pri > d->co_pri) {
            d->co     = a->co;
            d->co_pri = pri;
            snprintf(d->vendor, sizeof(d->vendor), "%s", cid_to_vendor((uint16_t)a->co));
            d->tracker[0] = '\0';   /* 主身份变更，旧 tracker 可能不再成立，先清 */
            if (a->co == 0x004C) {
                const char *s = (a->mfg && a->mfg_len >= 3)
                                ? apple_continuity_subtype(a->mfg, a->mfg_len) : "unknown";
                snprintf(d->subtype, sizeof(d->subtype), "%s", s);
            } else {
                snprintf(d->subtype, sizeof(d->subtype), "unknown");
            }
            const char *tr = tracker_classify((uint16_t)a->co, d->subtype);
            if (tr) snprintf(d->tracker, sizeof(d->tracker), "%s", tr);
        }
    }
    if (a->name[0]) {
        snprintf(d->name, sizeof(d->name), "%s", a->name);
    }

    xSemaphoreGive(s_lock);
}

/* ---------- 周期性设备快照 ---------- */
static void dump_snapshot(void)
{
    /* 先统计总数与"最近在场"数（用于摘要），再只打印最近在场的设备。 */
    uint32_t total = 0, shown = 0;
    int64_t now = esp_timer_get_time() / 1000;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (!s_dev[i].used) continue;
            total++;
            if (now - s_dev[i].last_ms <= PRINT_RECENT_MS) shown++;
        }
        xSemaphoreGive(s_lock);
    }

    printf("{\"snap\":true,\"devices\":%u,\"shown\":%u,\"hidden\":%u,"
           "\"evt\":%u,\"disc\":%u,\"scanrsp\":%u,\"period_ms\":%d}\n",
           (unsigned)total, (unsigned)shown, (unsigned)(total - shown),
           (unsigned)s_evt_count, (unsigned)s_disc_count,
           (unsigned)s_scanrsp_count, SNAPSHOT_PERIOD_MS);
    fflush(stdout);

    for (int i = 0; i < MAX_DEVICES; i++) {
        device_t d;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) continue;
        if (!s_dev[i].used) { xSemaphoreGive(s_lock); continue; }
        d = s_dev[i];
        now = esp_timer_get_time() / 1000;   /* 锁内取样，避免与 upsert 竞争产生负 age */
        xSemaphoreGive(s_lock);

        int64_t age  = now - d.last_ms;
        int64_t life = now - d.first_ms;
        if (age > PRINT_RECENT_MS) continue;   /* 输出过滤：僵尸条目不打印 */

        char name[160];
        json_escape(d.name, name, sizeof(name));
        char addr[24];
        snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 d.addr[5], d.addr[4], d.addr[3], d.addr[2], d.addr[1], d.addr[0]);
        char tracker_json[28];
        if (d.tracker[0]) snprintf(tracker_json, sizeof(tracker_json), "\"%s\"", d.tracker);
        else              snprintf(tracker_json, sizeof(tracker_json), "null");

        /* ---- 持续近距标记（F12，已按 D-R9 降级为信息项，非告警）----
         * 含义：追踪器类设备持续处于近距。**不表示"被跟踪"**——
         * 普通 Apple 设备（iPhone/AirPods）也会命中，见 docs/06 R9。 */
        int near_persist = (d.tracker[0] != '\0') &&
                           (d.rssi_max >= NEAR_RSSI_DBM) &&
                           (life >= PERSIST_MS) &&
                           (age <= PRESENT_RECENT_MS);

        printf("{\"dev\":true,\"key\":\"%s\",\"addr\":\"%s\",\"at\":%u,\"co\":%d,\"vendor\":\"%s\","
               "\"name\":\"%s\",\"subtype\":\"%s\",\"tracker\":%s,"
               "\"n\":%u,\"rssi\":%d,\"rmin\":%d,\"rmax\":%d,"
               "\"age_ms\":%" PRId64 ",\"life_ms\":%" PRId64 ","
               "\"near_persist\":%s}\n",
               d.key_kind == KEY_FMKEY ? "fmkey" : "addr",
               addr, (unsigned)d.atype, d.co, d.vendor, name, d.subtype, tracker_json,
               (unsigned)d.count, d.rssi_last, d.rssi_min, d.rssi_max,
               age, life,
               near_persist ? "true" : "false");
        fflush(stdout);
    }
}

static void snapshot_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SNAPSHOT_PERIOD_MS));
        dump_snapshot();
    }
}

#if EMIT_RAW_PER_PACKET
/* ---------- 逐包原始输出（调试用，默认编译掉）---------- */
static const char *adv_type_str(uint8_t evt_type)
{
    switch (evt_type) {
    case 0x00: return "ADV_IND";
    case 0x01: return "ADV_DIRECT_IND";
    case 0x02: return "ADV_SCAN_IND";
    case 0x03: return "ADV_NONCONN_IND";
    case 0x04: return "SCAN_RSP";
    default:   return "UNKNOWN";
    }
}

static void emit_raw(uint8_t evt_type, uint8_t atype, const uint8_t *addr_val,
                     int8_t rssi, const uint8_t *ad, uint8_t ad_len)
{
    adinfo_t a;
    parse_ad(ad, ad_len, &a);

    char name[160];
    json_escape(a.name, name, sizeof(name));

    char addr[24];
    snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
             addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    char hex[2 * 63 + 1];
    uint8_t n = ad_len < 63 ? ad_len : 63;
    for (uint8_t i = 0; i < n; i++) snprintf(&hex[i * 2], 3, "%02x", (unsigned)ad[i]);
    hex[n * 2] = '\0';

    printf("{\"t\":%" PRId64 ",\"evt\":\"%s\",\"addr\":\"%s\",\"at\":%u,\"rssi\":%d,"
           "\"len\":%u,\"co\":%d,\"vendor\":\"%s\",\"name\":\"%s\",\"data\":\"%s\"}\n",
           esp_timer_get_time() / 1000, adv_type_str(evt_type), addr,
           (unsigned)atype, (int)rssi, (unsigned)ad_len, a.co,
           (a.co >= 0) ? cid_to_vendor((uint16_t)a.co) : "", name, hex);
    fflush(stdout);
}
#endif /* EMIT_RAW_PER_PACKET */

/* ---------- GAP 事件回调 ---------- */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    s_evt_count++;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc *d = &event->disc;
        s_disc_count++;
        if (d->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) s_scanrsp_count++;
        adinfo_t a;
        parse_ad(d->data, d->length_data, &a);
        upsert(d->addr.type, d->addr.val, d->rssi, &a);
#if EMIT_RAW_PER_PACKET
        emit_raw(d->event_type, d->addr.type, d->addr.val, d->rssi, d->data, d->length_data);
#endif
        return 0;
    }
    case BLE_GAP_EVENT_EXT_DISC: {
        const struct ble_gap_ext_disc_desc *e = &event->ext_disc;
        s_disc_count++;
        if (e->legacy_event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) s_scanrsp_count++;
        adinfo_t a;
        parse_ad(e->data, e->length_data, &a);
        upsert(e->addr.type, e->addr.val, e->rssi, &a);
#if EMIT_RAW_PER_PACKET
        emit_raw(e->legacy_event_type, e->addr.type, e->addr.val, e->rssi,
                 e->data, e->length_data);
#endif
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        return 0;
    default:
        return 0;
    }
}

/* ---------- 启动扫描 ---------- */
static void start_scan(void)
{
    struct ble_gap_disc_params params = {0};
    params.itvl              = 0x0060;   /* 60ms */
    params.window            = 0x0030;   /* 30ms */
    params.passive           = ACTIVE_SCAN ? 0 : 1;   /* 0=主动扫描(F2)，1=被动 */
    params.filter_duplicates = 0;
    params.filter_policy     = 0;
    params.limited           = 0;

    xTaskCreate(snapshot_task, "snap", 4096, NULL, 3, NULL);

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    printf("[boot] ble_gap_disc rc=%d passive=%d\n", rc, (int)params.passive);
    fflush(stdout);
    if (rc != 0) ESP_LOGE(TAG, "ble_gap_disc failed: rc=%d", rc);
}

/* ---------- NimBLE 生命周期 ---------- */
static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    printf("[boot] sync: rc=%d own_addr_type=%d\n", rc, (int)own_addr_type);
    fflush(stdout);
    if (rc != 0) { ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc); return; }
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "nimble reset, reason=%d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    printf("[boot] app_main start enum DISC=%d EXT_DISC=%d\n",
           (int)BLE_GAP_EVENT_DISC, (int)BLE_GAP_EVENT_EXT_DISC);
    fflush(stdout);

    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    printf("[boot] nimble_port_init rc=%d\n", (int)ret);
    fflush(stdout);
    if (ret != ESP_OK) return;

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(host_task);
    printf("[boot] host task started\n");
    fflush(stdout);
}
