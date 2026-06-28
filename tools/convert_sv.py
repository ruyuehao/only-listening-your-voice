#!/usr/bin/env python3
"""
convert_sv.py — SV ONNX INT8 → .tflite 转换

在 GPU 服务器上运行:
  pip install onnx2tf tensorflow
  python tools/convert_sv.py

输入: model-data/model-quantization/model_sv_int8.onnx
输出: model-data/model_sv.tflite

注意: ONNX→TFLite 路径可能需根据 onnx2tf 版本调整。
如果 onnx2tf 不兼容, 备选: torch→TFLite (从 .pth 重新导出).
"""

import os
import sys
import numpy as np
import onnx
import tensorflow as tf

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)
ONNX_PATH = os.path.join(ROOT_DIR, "model-data", "model-quantization", "model_sv_int8.onnx")
TFLITE_PATH = os.path.join(ROOT_DIR, "model-data", "model_sv.tflite")

print(f"Loading ONNX model: {ONNX_PATH}")
onnx_model = onnx.load(ONNX_PATH)
onnx.checker.check_model(onnx_model)

# 检查输入输出
print(f"  Input:  {onnx_model.graph.input[0].name} shape={onnx_model.graph.input[0].type.tensor_type.shape.dim}")
print(f"  Output: {onnx_model.graph.output[0].name}")

# ---- 方法 1: onnx2tf ----
try:
    import onnx2tf
    print("\nConverting via onnx2tf...")
    onnx2tf.convert(
        input_onnx_file_path=ONNX_PATH,
        output_folder_path=os.path.join(ROOT_DIR, "model-data", "sv_tf"),
        output_signaturedefs=True,
        copy_onnx_input_output_names_to_tflite=True,
        non_verbose=True,
    )
    print("onnx2tf conversion done")
except ImportError:
    print("onnx2tf not available, trying alternative...")
except Exception as e:
    print(f"onnx2tf failed: {e}")

# ---- 方法 2: onnx → tf graph → tflite ----
print("\nAlternate: onnx-tf → TFLite...")
try:
    from onnx_tf.backend import prepare
    onnx_model = onnx.load(ONNX_PATH)
    tf_rep = prepare(onnx_model)
    tf_rep.export_graph(os.path.join(ROOT_DIR, "model-data", "sv_tf_alt"))
    print("TF SavedModel exported")
except ImportError:
    print("onnx-tf not available")
    print("\nManual steps required:")
    print("  1. pip install onnx2tf")
    print("  2. onnx2tf -i model_sv_int8.onnx -o sv_tflite/")
    print("  3. Copy sv_tflite/model_float32.tflite → model_sv.tflite")
    sys.exit(1)

# ---- 量化到 INT8 ----
print("\nQuantizing to INT8...")
savedmodel_dir = os.path.join(ROOT_DIR, "model-data", "sv_tf")
if not os.path.isdir(savedmodel_dir):
    savedmodel_dir = os.path.join(ROOT_DIR, "model-data", "sv_tf_alt")

converter = tf.lite.TFLiteConverter.from_saved_model(savedmodel_dir)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

def representative_dataset():
    for _ in range(100):
        data = np.random.randint(0, 256, size=(1, 40, 13, 1), dtype=np.float32)
        data = data / 255.0 * 2.0 - 1.0
        yield [data]

converter.representative_dataset = representative_dataset
tflite_model = converter.convert()

with open(TFLITE_PATH, "wb") as f:
    f.write(tflite_model)

size_kb = os.path.getsize(TFLITE_PATH) / 1024
print(f"Done: {TFLITE_PATH} ({size_kb:.1f} KB)")

if size_kb <= 15:
    print(f"✅ Size check: {size_kb:.1f} KB ≤ 15 KB — OK")
else:
    print(f"❌ Size check: {size_kb:.1f} KB > 15 KB — TOO LARGE!")
