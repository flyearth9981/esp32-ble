/*
 * tracker.c — 追踪器特征识别（F7）
 *
 * 依据：
 *  - [KNOWN, HIGH] Apple Company ID 0x004C。
 *  - [KNOWN, MED] Tile 使用自身 Company ID（具体值待 SIG 表确认，见 O6）。
 *  - [KNOWN, MED] Samsung 电子 0x0075（SmartTag 属其产品线的一类，
 *    但仅靠 Company ID 不能确认就是 SmartTag —— 见下方说明）。
 *  - [GUESS, LOW] "FindMy" 子类型判定依赖 apple.c 的子类型编码，同属待验证。
 */
#include "tracker.h"
#include <string.h>

#define CID_APPLE   0x004C
#define CID_SAMSUNG 0x0075
/* [GUESS, LOW] Tile 的 Company ID 值我无法确信，暂用占位并标注。
 * 实测确认后再启用；当前不放入表中以免误报。 */
/* #define CID_TILE 0x???? */

const char *tracker_classify(uint16_t company_id, const char *continuity_subtype)
{
    if (company_id == CID_APPLE) {
        if (continuity_subtype && strcmp(continuity_subtype, "FindMy") == 0) {
            /* [KNOWN, MED] Find My 网络广播 → 追踪器候选（AirTag/第三方 FindMy 配件）*/
            return "apple-findmy";
        }
        if (continuity_subtype && strcmp(continuity_subtype, "AirPods/ProximityPairing") == 0) {
            /* 不是追踪器，但常被误当；给个明确区分 */
            return "apple-audio";
        }
        return NULL; /* 其它 Apple 设备：不判为追踪器 */
    }

    if (company_id == CID_SAMSUNG) {
        /* [GUESS, LOW] 仅凭厂商标识不能确定是 SmartTag；
         * 需更多特征。此处保守返回 NULL，避免误报。 */
        return NULL;
    }

    return NULL;
}
