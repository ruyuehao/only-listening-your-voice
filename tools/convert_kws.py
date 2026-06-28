#!/usr/bin/env python3
"""
convert_kws.py — KWS .keras → .tflite INT8 转换

在 GPU 服务器上运行 (需 TensorFlow >= 2.16):
  python tools/convert_kws.py

输入: model-data/model-training/model_kws_tf.keras
输出: model-data/model_kws.tflite
"""

import tensorflow as tf
import numpy as np
import os

# 路径
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)
MODEL_DIR = os.path.join(ROOT_DIR, "model-data", "model-training")
OUTPUT_DIR = os.path.join(ROOT_DIR, "model-data")
KERAS_PATH = os.path.join(MODEL_DIR, "model_kws_tf.keras")
TFLITE_PATH = os.path.join(OUTPUT_DIR, "model_kws.tflite")

print(f"Loading Keras model: {KERAS_PATH}")
model = tf.keras.models.load_model(KERAS_PATH)
model.summary()

# 检查输入形状
input_shape = model.input_shape
print(f"Input shape: {input_shape}")  # 应为 (None, 100, 13, 1)
print(f"Output shape: {model.output_shape}")  # 应为 (None, 2)

# ---- INT8 量化转换 ----
def representative_dataset():
    """代表性数据集: 100 条随机 spectrogram"""
    for _ in range(100):
        data = np.random.randint(0, 256, size=(1, 100, 13, 1), dtype=np.float32)
        data = data / 255.0 * 2.0 - 1.0  # 归一化到 [-1, 1]
        yield [data]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8
converter.representative_dataset = representative_dataset
converter._experimental_variable_quantization = True

print("Converting to TFLite INT8...")
tflite_model = converter.convert()

# 保存
with open(TFLITE_PATH, "wb") as f:
    f.write(tflite_model)

size_kb = os.path.getsize(TFLITE_PATH) / 1024
print(f"Done: {TFLITE_PATH} ({size_kb:.1f} KB)")

# 验证
print("\nVerifying...")
interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()
print(f"  Input:  {input_details[0]['shape']} dtype={input_details[0]['dtype']}")
print(f"  Output: {output_details[0]['shape']} dtype={output_details[0]['dtype']}")

if size_kb <= 20:
    print(f"\n✅ Size check: {size_kb:.1f} KB ≤ 20 KB — OK")
else:
    print(f"\n❌ Size check: {size_kb:.1f} KB > 20 KB — TOO LARGE!")
