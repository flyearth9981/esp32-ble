/*
 * apple.h — Apple 设备识别与 Continuity 子类型分类（F5 / F6）
 *
 * ⚠️ 子类型编码 [GUESS, LOW]：本版按公开逆向资料整理，未在本机验证，
 *    可能不完整/不准确。**P3 必须以实测抓包校准后才能信任。**
 *    见 docs/05 §4、docs/07 O3。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* 是否为 Apple 厂商标识（Company ID 0x004C）。
 * 注意：Company ID 可伪造，true 仅表示"自述为 Apple"。*/
bool apple_is_apple(uint16_t company_id);

/* 依据 0xFF 厂商数据载荷判 Continuity 子类型。
 * payload/payload_len 指向厂商数据整体（含前 2 字节 Company ID）。
 * 返回静态子类型名；不是已知 Apple Continuity 类型则返回 "unknown"。*/
const char *apple_continuity_subtype(const uint8_t *payload, uint8_t payload_len);

/* 按 Continuity 子类型字节直接取名（供设备汇总时复用）。
 * 未知返回 "unknown"。*/
const char *apple_subtype_name(uint8_t code);
