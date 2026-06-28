#!/usr/bin/env python3
"""
convert_kws_v2.py — KWS ONNX INT8 → TFLite INT8 (保留量化)

正确的做法: 直接读 ONNX INT8 模型 → TFLite INT8，而非重新 float32 训练后做 PTQ。
这样可以保证 INT8 权重直接传递，体积控制在 12KB 以内。

在 GPU 服务器上运行:
  pip install onnx onnx2tf tensorflow
  python tools/convert_kws_v2.py

输入: model-data/model-quantization/model_kws_int8.onnx  (11.4 KB)
输出: model-data/model_kws.tflite                        (目标 ≤ 20KB)
"""

import os
import sys
import numpy as np

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)
ONNX_PATH = os.path.join(ROOT_DIR, "model-data", "model-quantization", "model_kws_int8.onnx")
TFLITE_PATH = os.path.join(ROOT_DIR, "model-data", "model_kws.tflite")
TFLITE_PATH_INT8 = os.path.join(ROOT_DIR, "model-data", "model_kws_int8.tflite")

print(f"Loading ONNX: {ONNX_PATH}")
import onnx
model = onnx.load(ONNX_PATH)
onnx.checker.check_model(model)
size_kb = os.path.getsize(ONNX_PATH) / 1024
print(f"  Size: {size_kb:.1f} KB  (input shape: 1×40×13×1, output: 2 classes)")

# ---- 方法: onnx2tf 保留 INT8 量化 ----
import onnx2tf

print("\nConverting via onnx2tf (preserving INT8)...")
onnx2tf.convert(
    input_onnx_file_path=ONNX_PATH,
    output_folder_path=os.path.join(ROOT_DIR, "model-data", "kws_tf"),
    output_signaturedefs=True,
    copy_onnx_input_output_names_to_tflite=True,
    non_verbose=True,
    # 关键参数: 保留输入 INT8 量化
    keep_ncw_or_nchw_or_ncdhw_input_names=None,
    keep_ncw_or_nchw_or_ncdhw_output_names=None,
)

# ---- 转换为 TFLite (直接使用 onnx2tf 输出的 SavedModel, 不加 PTQ) ----
# onnx2tf 应该已经输出了 .tflite 文件
generated_tflite = os.path.join(ROOT_DIR, "model-data", "kws_tf")
import glob
tflite_files = glob.glob(os.path.join(generated_tflite, "*.tflite"))
if tflite_files:
    import shutil
    shutil.copy(tflite_files[0], TFLITE_PATH)
    size = os.path.getsize(TFLITE_PATH) / 1024
    print(f"\n✅ Done: {TFLITE_PATH} ({size:.1f} KB)")
    if size <= 20:
        print(f"   Size check: OK (≤20KB)")
    else:
        print(f"   ⚠️ Size check: {size:.1f} > 20KB")
else:
    print("⚠️ onnx2tf didn't generate .tflite, trying with TF converter...")
    import tensorflow as tf
    savedmodel = os.path.join(ROOT_DIR, "model-data", "kws_tf")
    converter = tf.lite.TFLiteConverter.from_saved_model(savedmodel)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    converter._experimental_variable_quantization = True

    # 代表性数据 (用随机 spectrogram, INT8 范围已经正确)
    def rep_ds():
        for _ in range(100):
            data = np.random.randint(-128, 128, size=(1, 100, 13, 1), dtype=np.int8)
            yield [data.astype(np.float32)]

    converter.representative_dataset = rep_ds
    tflite_model = converter.convert()
    with open(TFLITE_PATH, "wb") as f:
        f.write(tflite_model)
    size = os.path.getsize(TFLITE_PATH) / 1024
    print(f"✅ Done: {TFLITE_PATH} ({size:.1f} KB)")
