/*
 * tracker.h — 追踪器特征识别（F7）
 *
 * ⚠️ 本版只做基础判定：已知追踪器厂商 Company ID。
 *    更精确的"Find My 网络"签名需 P3 实测补充（docs/07 O3）。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* 是否具备追踪器特征。company_id 为 0xFF 厂商 ID；
 * continuity_subtype 为 Apple 子类型名（非 Apple 传 "unknown"）。
 * 返回追踪器类别名；未命中返回 NULL。*/
const char *tracker_classify(uint16_t company_id, const char *continuity_subtype);
