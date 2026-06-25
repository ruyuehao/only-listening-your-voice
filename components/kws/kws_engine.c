/*
 * kws_engine.c — KWS TFLite Micro 推理实现
 *
 * 使用 esp-tflite-micro 组件 + ESP-NN 加速
 */

#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "kws_engine.h"
#include "sys_config.h"

static const char *TAG = "KWS_ENG";

/* ---- TFLite 全局对象 ---- */
static const tflite::Model        *s_model       = nullptr;
static tflite::MicroInterpreter   *s_interpreter = nullptr;
static TfLiteTensor               *s_input       = nullptr;
static TfLiteTensor               *s_output      = nullptr;

/* 静态 tensor arena（常驻 RAM） */
static uint8_t s_tensor_arena[KWS_TENSOR_ARENA_SIZE] __attribute__((aligned(16)));

/* ---- 外部引用的打桩引脚 ---- */
extern void gpio_set_level_profiling(int pin, int level);

/* ================================================================
 * 模型加载
 * ================================================================ */

static const uint8_t *load_model_from_partition(const char *label,
                                                 size_t *out_size)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, label);

    if (part == NULL) {
        ESP_LOGE(TAG, "Partition '%s' not found — trying SPIFFS...", label);
        /* 回退: 尝试 SPIFFS */
        part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, label);
    }

    if (part == NULL) {
        ESP_LOGE(TAG, "Partition '%s' not found in any subtype", label);
        return NULL;
    }

    size_t size = part->size;
    uint8_t *data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        data = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to alloc %d bytes for model", size);
        return NULL;
    }

    esp_err_t ret = esp_partition_read(part, 0, data, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read partition: %s", esp_err_to_name(ret));
        free(data);
        return NULL;
    }

    *out_size = size;
    ESP_LOGI(TAG, "Model loaded: %s (%d bytes)", label, size);
    return data;
}

/* ================================================================
 * OpResolver — 注册 KWS 模型所需的 TFLite 算子
 *
 * 最小集: CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED,
 *         SOFTMAX, RESHAPE, PAD, AVERAGE_POOL_2D
 * ================================================================ */

static tflite::MicroMutableOpResolver<12> *create_op_resolver(void)
{
    auto *resolver = new tflite::MicroMutableOpResolver<12>();

    resolver->AddConv2D();
    resolver->AddDepthwiseConv2D();
    resolver->AddFullyConnected();
    resolver->AddSoftmax();
    resolver->AddReshape();
    resolver->AddPad();
    resolver->AddAveragePool2D();
    resolver->AddMaxPool2D();
    resolver->AddRelu();
    resolver->AddQuantize();
    resolver->AddDequantize();
    resolver->AddAdd();

    return resolver;
}

/* ================================================================
 * 公共 API
 * ================================================================ */

esp_err_t kws_engine_init(void)
{
    ESP_LOGI(TAG, "Initializing KWS engine...");

    /* --- 1. 从 Flash 加载模型 --- */
    size_t model_size = 0;

    /* 尝试从 data/fat 分区加载 */
    const uint8_t *model_data = load_model_from_partition("models", &model_size);

    if (model_data == NULL) {
        ESP_LOGW(TAG, "KWS model not found in flash — running in NO-OP mode");
        ESP_LOGW(TAG, "Place model_kws.tflite in models partition");
        /* NO-OP 模式: 引擎初始化成功但不做推理，返回占位值 */
        return ESP_OK;
    }

    /* --- 2. 解析 FlatBuffers --- */
    s_model = tflite::GetModel(model_data);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema mismatch: got %d, expected %d",
                 s_model->version(), TFLITE_SCHEMA_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    /* --- 3. 创建 Op Resolver + Interpreter --- */
    auto *resolver = create_op_resolver();
    if (resolver == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    s_interpreter = new tflite::MicroInterpreter(
        s_model, *resolver, s_tensor_arena, KWS_TENSOR_ARENA_SIZE);

    if (s_interpreter == nullptr) {
        ESP_LOGE(TAG, "Failed to create interpreter");
        delete resolver;
        return ESP_ERR_NO_MEM;
    }

    /* --- 4. 分配 tensor --- */
    TfLiteStatus status = s_interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors: %d", status);
        delete s_interpreter;
        s_interpreter = nullptr;
        delete resolver;
        return ESP_FAIL;
    }

    /* --- 5. 获取输入/输出 tensor 指针 --- */
    s_input  = s_interpreter->input(0);
    s_output = s_interpreter->output(0);

    ESP_LOGI(TAG, "KWS engine ready:");
    ESP_LOGI(TAG, "  Input:  [%d,%d,%d] type=%d",
             s_input->dims->data[0], s_input->dims->data[1],
             s_input->dims->data[2], s_input->type);
    ESP_LOGI(TAG, "  Output: [%d,%d] type=%d",
             s_output->dims->data[0], s_output->dims->data[1], s_output->type);
    ESP_LOGI(TAG, "  Arena:  %d bytes used, %d free",
             KWS_TENSOR_ARENA_SIZE - s_interpreter->arena_used_bytes(),
             s_interpreter->arena_used_bytes());

    return ESP_OK;
}

esp_err_t kws_engine_infer(const uint8_t *input, float *confidence)
{
    /* NO-OP 模式: 模型未加载 */
    if (s_interpreter == nullptr) {
        *confidence = 0.5f;
        return ESP_OK;
    }

    /* --- 1. 填充输入 tensor (uint8 → INT8) --- */
    int8_t *input_data = s_input->data.int8;
    int input_size = KWS_INPUT_FRAMES * KWS_FEATURE_DIM;

    /* microfrontend 输出 0-255 uint8, TFLite INT8 输入 -128..127 */
    for (int i = 0; i < input_size; i++) {
        input_data[i] = (int8_t)((int)input[i] - 128);
    }

    /* --- 2. 推理 --- */
    /* GPIO 打桩: 测量推理延迟 */
    extern void gpio_set_level_profiling(int pin, int level);

    TfLiteStatus status = s_interpreter->Invoke();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed: %d", status);
        *confidence = 0.0f;
        return ESP_FAIL;
    }

    /* --- 3. 读取输出 --- */
    /* 输出是 softmax 的第二个值（唤醒词概率），INT8 → float */
    int8_t *output_data = s_output->data.int8;

    /* 反量化: float = (int8 - zero_point) * scale */
    float scale     = s_output->params.scale;
    int   zero_point = s_output->params.zero_point;

    *confidence = (float)(output_data[1] - zero_point) * scale;
    if (*confidence < 0.0f) *confidence = 0.0f;
    if (*confidence > 1.0f) *confidence = 1.0f;

    return ESP_OK;
}

void kws_engine_deinit(void)
{
    if (s_interpreter != nullptr) {
        delete s_interpreter;
        s_interpreter = nullptr;
    }
    s_model   = nullptr;
    s_input   = nullptr;
    s_output  = nullptr;
    ESP_LOGI(TAG, "KWS engine deinitialized");
}
