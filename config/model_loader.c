/*
 * model_loader.c — FAT 文件系统模型加载实现
 */

#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "model_loader.h"

static const char *TAG = "MODEL_LDR";

/* ---- FAT 挂载路径 ---- */
#define MODELS_MOUNT_POINT  "/models"
#define MODELS_PART_LABEL   "models"

/* ---- 全局挂载状态（惰性挂载，无需多次 mount/unmount） ---- */
static bool s_fat_mounted = false;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static esp_err_t ensure_fat_mounted(void)
{
    if (s_fat_mounted) return ESP_OK;

    ESP_LOGI(TAG, "Mounting FAT filesystem on '%s'...", MODELS_PART_LABEL);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
        MODELS_PART_LABEL);

    if (part == NULL) {
        ESP_LOGE(TAG, "Partition '%s' (type=data, subtype=fat) not found", MODELS_PART_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096,
    };

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(
        MODELS_MOUNT_POINT, MODELS_PART_LABEL,
        &mount_cfg, &s_wl_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAT mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_fat_mounted = true;
    ESP_LOGI(TAG, "FAT mounted at '%s'", MODELS_MOUNT_POINT);
    return ESP_OK;
}

uint8_t *model_loader_read(const char *filename, size_t *out_size)
{
    if (filename == NULL || out_size == NULL) return NULL;

    /* 确保 FAT 已挂载（惰性，仅首次挂载） */
    esp_err_t ret = ensure_fat_mounted();
    if (ret != ESP_OK) {
        return NULL;
    }

    /* 构建完整路径 */
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", MODELS_MOUNT_POINT, filename);
    ESP_LOGI(TAG, "Loading model: %s", path);

    /* 获取文件大小 */
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "stat failed for '%s' — file exists?", path);
        return NULL;
    }

    size_t size = st.st_size;
    if (size == 0 || size > (512 * 1024)) {
        ESP_LOGE(TAG, "Invalid file size: %zu bytes", size);
        return NULL;
    }

    /* 分配 buffer */
    uint8_t *data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to alloc %d bytes for model", size);
        return NULL;
    }

    /* 读取文件 */
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen failed for '%s'", path);
        free(data);
        return NULL;
    }

    size_t read = fread(data, 1, size, f);
    fclose(f);

    if (read != size) {
        ESP_LOGE(TAG, "fread incomplete: %d/%d bytes", read, size);
        free(data);
        return NULL;
    }

    *out_size = size;
    ESP_LOGI(TAG, "Model loaded: %s (%d bytes)", filename, size);
    return data;
}
