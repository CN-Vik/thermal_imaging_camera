/**
 * @file photo.c
 * @brief 热成像照片存储（Flash 原始分区，压缩存温度矩阵）
 *
 * 每张照片占一个 4KB 扇区：
 *   [24 字节头部][32x24 归一化温度 768 字节][padding]
 * 相比存 280x240 RGB565（134KB），压缩后只有 ~800 字节，约 170 倍。
 * 查看时用 ir_camera_render_norm() 实时重渲染成图像。
 */
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "photo.h"

#define TAG              "photo"
#define PHOTO_LABEL      "storage"
#define PHOTO_MAGIC      0x31544850u  /* "PHT1" */
#define PHOTO_SECTOR     4096
#define PHOTO_SLOT_SIZE  PHOTO_SECTOR

typedef struct {
    uint32_t magic;       /* +0 */
    uint32_t valid;       /* +4  1 = 有效 */
    uint32_t timestamp;   /* +8  秒 */
    uint32_t color_mode;  /* +12 */
    float min_t;          /* +16 */
    float max_t;          /* +20 */
    uint8_t norm[PHOTO_NORM_BYTES]; /* +24 .. +791 */
} photo_header_t;

static const esp_partition_t *s_part = NULL;

esp_err_t photo_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                      PHOTO_LABEL);
    if (!s_part) {
        ESP_LOGE(TAG, "partition '%s' not found", PHOTO_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "storage @0x%lx size=0x%lx, max=%d photos",
             (unsigned long)s_part->address, (unsigned long)s_part->size, photo_max());
    return ESP_OK;
}

int photo_max(void)
{
    if (!s_part) return 0;
    return (int)(s_part->size / PHOTO_SLOT_SIZE);
}

static bool slot_is_valid(int slot)
{
    uint32_t valid = 0xFFFFFFFF;
    esp_partition_read(s_part, (size_t)slot * PHOTO_SLOT_SIZE + 4, &valid, sizeof(valid));
    return (valid == 1);
}

int photo_count(void)
{
    int cnt = 0;
    for (int i = 0; i < photo_max(); i++) {
        if (slot_is_valid(i)) cnt++;
    }
    return cnt;
}

static int find_free_slot(void)
{
    for (int i = 0; i < photo_max(); i++) {
        if (!slot_is_valid(i)) return i;
    }
    return -1;
}

esp_err_t photo_capture(const uint8_t *norm, int color_mode, float min_t, float max_t)
{
    if (!s_part || !norm) return ESP_ERR_INVALID_ARG;

    int slot = find_free_slot();
    if (slot < 0) return ESP_ERR_NO_MEM;

    size_t off = (size_t)slot * PHOTO_SLOT_SIZE;
    esp_err_t err = esp_partition_erase_range(s_part, off, PHOTO_SLOT_SIZE);
    if (err != ESP_OK) return err;

    photo_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = PHOTO_MAGIC;
    hdr.valid = 1;
    hdr.timestamp = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    hdr.color_mode = (uint32_t)color_mode;
    hdr.min_t = min_t;
    hdr.max_t = max_t;
    memcpy(hdr.norm, norm, PHOTO_NORM_BYTES);

    err = esp_partition_write(s_part, off, &hdr, sizeof(hdr));
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "captured -> slot %d (total %d)", slot, photo_count());
    return ESP_OK;
}

esp_err_t photo_load(int logical_idx, uint8_t *out_norm, int *out_color_mode,
                     float *out_min_t, float *out_max_t)
{
    if (!s_part || !out_norm) return ESP_ERR_INVALID_ARG;

    int seen = 0;
    for (int i = 0; i < photo_max(); i++) {
        if (!slot_is_valid(i)) continue;
        if (seen != logical_idx) { seen++; continue; }

        photo_header_t hdr;
        esp_err_t err = esp_partition_read(s_part, (size_t)i * PHOTO_SLOT_SIZE, &hdr, sizeof(hdr));
        if (err != ESP_OK) return err;
        if (hdr.valid != 1) continue;

        memcpy(out_norm, hdr.norm, PHOTO_NORM_BYTES);
        if (out_color_mode) *out_color_mode = (int)hdr.color_mode;
        if (out_min_t) *out_min_t = hdr.min_t;
        if (out_max_t) *out_max_t = hdr.max_t;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t photo_delete(int logical_idx)
{
    if (!s_part) return ESP_ERR_INVALID_ARG;

    int seen = 0;
    for (int i = 0; i < photo_max(); i++) {
        if (!slot_is_valid(i)) continue;
        if (seen != logical_idx) { seen++; continue; }

        /* 只擦头部扇区：valid 变成 0xFF，视为删除 */
        return esp_partition_erase_range(s_part, (size_t)i * PHOTO_SLOT_SIZE, PHOTO_SECTOR);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t photo_delete_all(void)
{
    if (!s_part) return ESP_ERR_INVALID_ARG;
    /* 只擦除有效槽位，避免擦整片 2MB 分区导致长时间阻塞 */
    for (int i = 0; i < photo_max(); i++) {
        if (slot_is_valid(i)) {
            esp_partition_erase_range(s_part, (size_t)i * PHOTO_SLOT_SIZE, PHOTO_SECTOR);
        }
    }
    return ESP_OK;
}
