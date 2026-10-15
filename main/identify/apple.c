/*
 * apple.c — Apple 识别 + Continuity 子类型（F5 / F6）
 *
 * ============================================================
 *  置信度声明
 *  - apple_is_apple：Company ID 0x004C [KNOWN, HIGH]。
 *  - 子类型字节值：
 *    · 0x02=iBeacon、0x10=NearbyInfo、0x12=FindMy：【实测确认】
 *      （2026-10-15，真机 C5 抓到 iPhone 广播逐一核对通过）。
 *    · 其余值（0x01/0x03..0x0F）：仍为 [GUESS, LOW]，来自公开
 *      逆向资料，**未验证**。0x01 尤其可疑，暂用对冲标签。
 *  - 载荷首字节若为长度字段的结构（Nearby/Handoff），真实类型
 *    在下一字节；本版两处都试（启发式，未完全验证）。
 * ============================================================
 */
#include "apple.h"
#include <string.h>

#define APPLE_COMPANY_ID 0x004C

bool apple_is_apple(uint16_t company_id)
{
    return company_id == APPLE_COMPANY_ID;
}

/* 首字节 → 子类型名 */
static const char *subtype_by_first_byte(uint8_t b)
{
    switch (b) {
    /* ---- 实测确认（2026-10-15 真机核对）---- */
    case 0x02: return "iBeacon";
    case 0x10: return "NearbyInfo";
    case 0x12: return "FindMy";
    /* ---- 以下 [GUESS, LOW]，未验证 ---- */
    case 0x01: return "Continuity-01(unknown)";
    case 0x03: return "AirPrint";
    case 0x04: return "AirDrop(legacy)";
    case 0x05: return "AirDrop";
    case 0x06: return "HomeKit";
    case 0x07: return "AirPods/ProximityPairing";
    case 0x08: return "HeySiri";
    case 0x09: return "AirPlayTarget";
    case 0x0A: return "AirPlaySource";
    case 0x0B: return "Watch";
    case 0x0C: return "Handoff";
    case 0x0D: return "TetheringTarget";
    case 0x0E: return "TetheringSource";
    case 0x0F: return "NearbyAction";
    default:   return NULL;
    }
}

const char *apple_subtype_name(uint8_t code)
{
    const char *n = subtype_by_first_byte(code);
    return n ? n : "unknown";
}

const char *apple_continuity_subtype(const uint8_t *payload, uint8_t len)
{
    /* payload 结构： [0]=ComId 低  [1]=ComId 高  [2..]=厂商数据 */
    if (payload == NULL || len < 3) {
        return "unknown";
    }
    if (!apple_is_apple((uint16_t)(payload[0] | (payload[1] << 8)))) {
        return "unknown";
    }

    uint8_t b2 = payload[2];

    /* 直接命中 */
    const char *name = subtype_by_first_byte(b2);
    if (name) {
        return name;
    }

    /* [GUESS, LOW] Nearby/Handoff 类：首字节是长度，类型在其后。
     * 例：len=0x10，则 payload[3] 才是 0x10(NearbyInfo)。 */
    if (b2 >= 0x0C && len >= 4) {
        const char *n2 = subtype_by_first_byte(payload[3]);
        if (n2) {
            return n2;
        }
    }

    return "unknown";
}
