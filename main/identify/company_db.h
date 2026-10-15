/*
 * company_db.h — 厂商识别（F4）
 *
 * [KNOWN, MED] 起步版厂商表，仅供调试。完整/最新表见 Bluetooth SIG
 * Assigned Numbers；P2 必须用官方表重新生成（docs/07 O6）。
 */
#pragma once
#include <stdint.h>

/* 返回厂商名；未知返回空串 ""。 */
const char *cid_to_vendor(uint16_t company_id);
