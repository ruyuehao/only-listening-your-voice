# models_fat/ — 模型文件存放目录
#
# 将转换好的 .tflite 文件放入此处，然后用 fatfsgen 生成 FAT 镜像烧录:
#
#   python $IDF_PATH/components/fatfs/fatfsgen.py models_fat models.img
#   parttool.py write_partition --partition-name=models --input=models.img
#
# 文件要求:
#   model_kws.tflite  ≤ 20KB INT8, 输入 (1,100,13), 输出 2 类
#   model_sv.tflite   ≤ 15KB INT8, 输入 (1,40,13),  输出 16 维 Embedding
#
# 转换脚本: 见 ../tools/convert_kws.py 和 ../tools/convert_sv.py
