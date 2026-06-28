/*
 * kws_engine.c — KWS TFLite Micro 推理实现 (DS-CNN)
 *
 * 使用 esp-tflite-micro 组件 + ESP-NN 加速
 * 模型从 FAT 分区按文件名加载
 */

#include <string.h>
#include "esp_log.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "kws_engine.h"
#include "model_loader.h"
#include "sys_config.h"

static const char *TAG = "KWS_ENG";

/* ---- TFLite 全局对象 ---- */
static const tflite::Model        *s_model       = nullptr;
static tflite::MicroInterpreter   *s_interpreter = nullptr;
static TfLiteTensor               *s_input       = nullptr;
static TfLiteTensor               *s_output      = nullptr;
static tflite::MicroMutableOpResolver<12> *s_resolver = nullptr;

/* 模型数据 — 保持引用防止被 GC，deinit 时释放 */
static uint8_t                    *s_model_data  = nullptr;

/* 静态 tensor arena（常驻 RAM，16 字节对齐） */
static uint8_t s_tensor_arena[KWS_TENSOR_ARENA_SIZE] __attribute__((aligned(16)));

/* ================================================================
 * OpResolver
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

    /* --- 1. 从 FAT 分区加载模型文件 --- */
    size_t model_size = 0;
    s_model_data = model_loader_read("model_kws.tflite", &model_size);

    if (s_model_data == NULL) {
        ESP_LOGW(TAG, "KWS model not found — running in NO-OP mode");
        ESP_LOGW(TAG, "Place model_kws.tflite in models FAT partition");
        return ESP_OK;
    }

    /* --- 2. 解析 FlatBuffers --- */
    s_model = tflite::GetModel(s_model_data);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema mismatch: got %d, expected %d",
                 s_model->version(), TFLITE_SCHEMA_VERSION);
        free(s_model_data);
        s_model_data = nullptr;
        return ESP_ERR_INVALID_VERSION;
    }

    /* --- 3. 创建 Op Resolver + Interpreter --- */
    s_resolver = create_op_resolver();
    if (s_resolver == nullptr) {
        free(s_model_data);
        s_model_data = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_interpreter = new tflite::MicroInterpreter(
        s_model, *s_resolver, s_tensor_arena, KWS_TENSOR_ARENA_SIZE);

    if (s_interpreter == nullptr) {
        ESP_LOGE(TAG, "Failed to create interpreter");
        delete s_resolver;
        s_resolver = nullptr;
        free(s_model_data);
        s_model_data = nullptr;
        return ESP_ERR_NO_MEM;
    }

    /* --- 4. 分配 tensor --- */
    TfLiteStatus status = s_interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors: %d", status);
        delete s_interpreter;
        s_interpreter = nullptr;
        delete s_resolver;
        s_resolver = nullptr;
        free(s_model_data);
        s_model_data = nullptr;
        return ESP_FAIL;
    }

    /* --- 5. 获取输入/输出 tensor 指针 --- */
    s_input  = s_interpreter->input(0);
    s_output = s_interpreter->output(0);

    ESP_LOGI(TAG, "KWS engine ready:");
    ESP_LOGI(TAG, "  Model:  %d bytes", model_size);
    ESP_LOGI(TAG, "  Input:  [%d,%d,%d] type=%d",
             s_input->dims->data[0], s_input->dims->data[1],
             s_input->dims->data[2], s_input->type);
    ESP_LOGI(TAG, "  Output: [%d,%d] type=%d",
             s_output->dims->data[0], s_output->dims->data[1], s_output->type);
    ESP_LOGI(TAG, "  Arena:  %d bytes used",
             s_interpreter->arena_used_bytes());

    return ESP_OK;
}

esp_err_t kws_engine_infer(const uint8_t *input, float *confidence)
{
    if (s_interpreter == nullptr) {
        *confidence = 0.5f;
        return ESP_OK;
    }

    int8_t *input_data = s_input->data.int8;
    int input_size = KWS_INPUT_FRAMES * KWS_FEATURE_DIM;

    for (int i = 0; i < input_size; i++) {
        input_data[i] = (int8_t)((int)input[i] - 128);
    }

    TfLiteStatus status = s_interpreter->Invoke();
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed: %d", status);
        *confidence = 0.0f;
        return ESP_FAIL;
    }

    int8_t *output_data = s_output->data.int8;
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
    if (s_resolver != nullptr) {
        delete s_resolver;
        s_resolver = nullptr;
    }
    if (s_model_data != nullptr) {
        free(s_model_data);
        s_model_data = nullptr;
    }
    s_model   = nullptr;
    s_input   = nullptr;
    s_output  = nullptr;
    ESP_LOGI(TAG, "KWS engine deinitialized");
}
