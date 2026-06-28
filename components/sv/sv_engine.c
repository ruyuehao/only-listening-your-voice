/*
 * sv_engine.c — 声纹验证引擎实现
 *
 * 关键技术:
 *   - 动态模型加载/卸载（esp_partition_read → TFLite → 推理 → free）
 *   - 余弦相似度: cos(θ) = A·B / (‖A‖‖B‖)
 *   - NVS 模板存储: 16 × float = 64 bytes
 */

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "sys_config.h"
#include "model_loader.h"
#include "sv_engine.h"

static const char *TAG = "SV_ENG";

/* ================================================================
 * 模型加载（动态分配）
 * ================================================================ */

typedef struct {
    const tflite::Model       *model;
    tflite::MicroInterpreter  *interpreter;
    uint8_t                   *arena;
    uint8_t                   *model_data;  /* 持有引用防止被 free */
} sv_session_t;

static sv_session_t *sv_session_create(void)
{
    sv_session_t *s = (sv_session_t *)calloc(1, sizeof(sv_session_t));
    if (s == NULL) return NULL;

    /* 分配 tensor arena */
    s->arena = (uint8_t *)heap_caps_aligned_alloc(16,
        SV_TENSOR_ARENA_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s->arena == NULL) {
        ESP_LOGE(TAG, "Failed to alloc tensor arena (%d bytes)", SV_TENSOR_ARENA_SIZE);
        free(s);
        return NULL;
    }

    /* 从 FAT 分区按文件名加载 SV 模型 */
    size_t model_size = 0;
    s->model_data = model_loader_read("model_sv.tflite", &model_size);
    if (s->model_data == NULL) {
        ESP_LOGE(TAG, "Failed to load model_sv.tflite from FAT partition");
        free(s->arena);
        free(s);
        return NULL;
    }

    s->model = tflite::GetModel(s->model_data);
    if (s->model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema mismatch");
        free(s->model_data);
        free(s->arena);
        free(s);
        return NULL;
    }

    /* OpResolver — 1D CNN:
     * Conv1d (实现为 Conv2D k×1) + BN + ReLU + GAP(Mean) + FC + L2_Norm
     * 全算子 TFLite Micro 原生支持 */
    tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();           /* Conv1d → Conv2D(k,1) */
    resolver.AddFullyConnected();   /* FC(24→16) */
    resolver.AddReshape();
    resolver.AddRelu();
    resolver.AddMean();             /* GlobalAveragePool */
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddMul();              /* BN scale (可能已 baked into weights) */
    resolver.AddL2Normalization();  /* output L2 norm */
    resolver.AddSoftmax();          /* (备用, 训练时可能留有) */

    /* 创建解释器 */
    s->interpreter = new tflite::MicroInterpreter(
        s->model, resolver, s->arena, SV_TENSOR_ARENA_SIZE);
    if (s->interpreter == nullptr) {
        ESP_LOGE(TAG, "Failed to create interpreter");
        free(s->model_data);
        free(s->arena);
        free(s);
        return NULL;
    }

    TfLiteStatus status = s->interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors: %d", status);
        delete s->interpreter;
        free(s->model_data);
        free(s->arena);
        free(s);
        return NULL;
    }

    ESP_LOGI(TAG, "SV session created: arena=%d used=%d",
             SV_TENSOR_ARENA_SIZE, s->interpreter->arena_used_bytes());
    return s;
}

static void sv_session_destroy(sv_session_t *s)
{
    if (s == NULL) return;
    if (s->interpreter) delete s->interpreter;
    if (s->model_data)  free(s->model_data);
    if (s->arena)       free(s->arena);
    free(s);
    ESP_LOGI(TAG, "SV session destroyed — RAM freed");
}

/* ================================================================
 * Embedding 提取
 * ================================================================ */

static esp_err_t sv_extract_embedding(sv_session_t *s,
                                       const uint8_t *input,
                                       float *embedding)
{
    TfLiteTensor *sv_input  = s->interpreter->input(0);
    TfLiteTensor *sv_output = s->interpreter->output(0);

    /* 输入转换: uint8 → INT8 */
    int8_t *input_data = sv_input->data.int8;
    int input_size = SV_INPUT_FRAMES * SV_FEATURE_DIM;
    for (int i = 0; i < input_size; i++) {
        input_data[i] = (int8_t)((int)input[i] - 128);
    }

    /* 推理 */
    TfLiteStatus status = s->interpreter->Invoke();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "SV Invoke failed: %d", status);
        return ESP_FAIL;
    }

    /* 反量化: INT8 → float */
    float scale     = sv_output->params.scale;
    int   zero_point = sv_output->params.zero_point;
    int8_t *out_data = sv_output->data.int8;

    for (int i = 0; i < SV_EMBEDDING_DIM; i++) {
        embedding[i] = (float)(out_data[i] - zero_point) * scale;
    }

    return ESP_OK;
}

/* ================================================================
 * 余弦相似度
 * ================================================================ */

static float cosine_similarity(const float *a, const float *b, int dim)
{
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;

    for (int i = 0; i < dim; i++) {
        dot   += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = sqrtf(norm_a) * sqrtf(norm_b);
    if (denom < 1e-9f) {
        return 0.0f;  /* 零向量 → 相似度为 0 */
    }

    return dot / denom;
}

/* ================================================================
 * NVS 模板存取
 * ================================================================ */

esp_err_t sv_template_save(const float *embedding)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 写入 16 × 4 = 64 字节 */
    ret = nvs_set_blob(handle, NVS_KEY_TEMPLATE, embedding, SV_EMBEDDING_DIM * sizeof(float));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s — template NOT persisted!", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Template saved to NVS (key=%s, %d bytes)",
             NVS_KEY_TEMPLATE, SV_EMBEDDING_DIM * (int)sizeof(float));
    return ESP_OK;
}

esp_err_t sv_template_load(float *embedding)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t size = SV_EMBEDDING_DIM * sizeof(float);
    ret = nvs_get_blob(handle, NVS_KEY_TEMPLATE, embedding, &size);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "No template in NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

bool sv_template_exists(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t size = 0;
    ret = nvs_get_blob(handle, NVS_KEY_TEMPLATE, NULL, &size);
    nvs_close(handle);
    return (ret == ESP_OK);
}

/* ================================================================
 * 公共 API
 * ================================================================ */

esp_err_t sv_engine_verify(const uint8_t *input,
                            float *similarity,
                            float *embedding)
{
    if (input == NULL || similarity == NULL || embedding == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* --- 1. 动态加载模型 --- */
    sv_session_t *s = sv_session_create();
    if (s == NULL) {
        *similarity = -3.0f;  /* 模型加载失败 */
        return ESP_FAIL;
    }

    /* --- 2. 提取 Embedding --- */
    esp_err_t ret = sv_extract_embedding(s, input, embedding);
    if (ret != ESP_OK) {
        sv_session_destroy(s);
        *similarity = -3.0f;
        return ret;
    }

    /* --- 3. 检查模板 --- */
    float template_emb[SV_EMBEDDING_DIM];
    ret = sv_template_load(template_emb);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No voiceprint template — verify cannot proceed");
        sv_session_destroy(s);
        *similarity = -2.0f;  /* 无模板 */
        return ESP_OK;
    }

    /* --- 4. 计算余弦相似度 --- */
    *similarity = cosine_similarity(embedding, template_emb, SV_EMBEDDING_DIM);

    ESP_LOGI(TAG, "Verification: similarity=%.4f (threshold=%.2f)",
             *similarity, SV_THRESHOLD);

    /* --- 5. 卸载模型 --- */
    sv_session_destroy(s);
    return ESP_OK;
}

esp_err_t sv_engine_extract(const uint8_t *input, float *embedding)
{
    if (input == NULL || embedding == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sv_session_t *s = sv_session_create();
    if (s == NULL) return ESP_FAIL;

    esp_err_t ret = sv_extract_embedding(s, input, embedding);
    sv_session_destroy(s);
    return ret;
}
