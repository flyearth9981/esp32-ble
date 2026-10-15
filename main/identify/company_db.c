/*
 * company_db.c — Company ID → 厂商名（F4）
 *
 * 表数据在 company_db_data.inc。默认是 fallback 调试表；
 * 运行 tools/gen_company_db.py 用 SIG 官方 CSV 重新生成可得到完整表。
 * 完整表约数千条，存于 .rodata（flash），不在 RAM。
 */
#include "company_db.h"
#include <stddef.h>

typedef struct {
    uint16_t    id;
    const char *name;
} cid_entry_t;

#include "company_db_data.inc"

const char *cid_to_vendor(uint16_t id)
{
    for (size_t i = 0; i < sizeof(cid_table) / sizeof(cid_table[0]); i++) {
        if (cid_table[i].id == id) {
            return cid_table[i].name;
        }
    }
    return "";
}
