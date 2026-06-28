#!/usr/bin/env python3
"""
convert_sv_v2.py — SV ONNX INT8 → TFLite INT8 (保留量化)

在 GPU 服务器上运行:
  pip install onnx onnx2tf tensorflow
  python tools/convert_sv_v2.py

输入: model-data/model-quantization/model_sv_int8.onnx  (14.3 KB)
输出: model-data/model_sv.tflite                        (目标 ≤ 15KB)
"""

import os
import sys
import numpy as np
import glob
import shutil

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)
ONNX_PATH = os.path.join(ROOT_DIR, "model-data", "model-quantization", "model_sv_int8.onnx")
TFLITE_PATH = os.path.join(ROOT_DIR, "model-data", "model_sv.tflite")

print(f"Loading ONNX: {ONNX_PATH}")
import onnx
model = onnx.load(ONNX_PATH)
onnx.checker.check_model(model)
size_kb = os.path.getsize(ONNX_PATH) / 1024
print(f"  Size: {size_kb:.1f} KB  (input: 1×40×13×1, output: 16-dim embedding)")

# ---- onnx2tf 保留 INT8 ----
import onnx2tf

print("\nConverting via onnx2tf (preserving INT8)...")
onnx2tf.convert(
    input_onnx_file_path=ONNX_PATH,
    output_folder_path=os.path.join(ROOT_DIR, "model-data", "sv_tf_v2"),
    output_signaturedefs=True,
    copy_onnx_input_output_names_to_tflite=True,
    non_verbose=True,
)

# onnx2tf 会直接生成 .tflite
generated_dir = os.path.join(ROOT_DIR, "model-data", "sv_tf_v2")
tflite_files = glob.glob(os.path.join(generated_dir, "*.tflite"))

if tflite_files:
    shutil.copy(tflite_files[0], TFLITE_PATH)
    size = os.path.getsize(TFLITE_PATH) / 1024
    print(f"\n✅ Done: {TFLITE_PATH} ({size:.1f} KB)")
    if size <= 15:
        print(f"   Size check: OK (≤15KB)")
    else:
        print(f"   ⚠️ Size {size:.1f}KB > 15KB — 需要手动检查")
        # 备选: 用 TF converter 再做一次 INT8 量化
        print("   Trying TF converter fallback...")
        import tensorflow as tf
        converter = tf.lite.TFLiteConverter.from_saved_model(generated_dir)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
        def rep_ds():
            for _ in range(100):
                data = np.random.randint(-128, 128, size=(1, 40, 13, 1), dtype=np.int8)
                yield [data.astype(np.float32)]
        converter.representative_dataset = rep_ds
        tflite = converter.convert()
        with open(TFLITE_PATH, "wb") as f:
            f.write(tflite)
        size = os.path.getsize(TFLITE_PATH) / 1024
        print(f"   Fallback result: {size:.1f} KB")
else:
    print("❌ onnx2tf 未生成 .tflite")
    sys.exit(1)
