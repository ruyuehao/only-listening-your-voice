#!/usr/bin/env python3
"""
onnx2tflite_v2.py — ONNX → TFLite (纯 flatbuffers, 零 ML 依赖)

只依赖 onnx + flatbuffers + numpy (均已安装), 不加 onnx2tf/tensorflow。
用法: python tools/onnx2tflite_v2.py ONNX_PATH TFLITE_PATH
"""

import sys, os, struct
import numpy as np
import onnx
from onnx import numpy_helper
import flatbuffers
from flatbuffers import Builder

# ============================================================
def onnx2tflite(onnx_path, tflite_path):
    model = onnx.load(onnx_path)
    g = model.graph

    # Extract QDQ weights
    weights = {}  # name → bytes
    scales = {}   # name → float
    zps = {}      # name → int
    biases = {}
    other = {}

    for init in g.initializer:
        arr = numpy_helper.to_array(init)
        name = init.name
        if name.endswith('_quantized'):
            weights[name.replace('_quantized', '')] = arr.astype(np.int8).tobytes()
        elif name.endswith('_scale'):
            scales[name.replace('_scale', '')] = arr.item()
        elif name.endswith('_zero_point'):
            zps[name.replace('_zero_point', '')] = int(arr.item())
        elif 'bias' in name.lower():
            biases[name] = arr.astype(np.int32).tobytes()
        else:
            other[name] = arr.tobytes()

    print(f"Weights: {len(weights)} INT8 groups, Biases: {len(biases)}")

    # Input/output
    inp_shape = [d.dim_value or 1 for d in g.input[0].type.tensor_type.shape.dim]
    out_shape = [d.dim_value or 1 for d in g.output[0].type.tensor_type.shape.dim]
    print(f"Shape: {inp_shape} -> {out_shape}")

    # Collect all weight buffers and build tensor list
    b = Builder(4096)

    # Buffer 0: empty
    buf_empty = b.CreateByteVector(b'')

    # Weight buffers (pre-create byte vectors)
    buffers = [b.CreateByteVector(d) for d in [b'', *list(biases.values()), *list(weights.values()), *list(other.values())]]

    # Build buffers vector
    b.StartVector(4, len(buffers), 4)
    for buf in reversed(buffers):
        b.PrependUOffsetTRelative(buf)
    buf_vec = b.EndVector(len(buffers))

    # Strings
    def S(s):
        b.CreateString(s)
        return b.EndVector(len(s))

    # We'll build the simplest possible TFLite model manually as raw bytes
    # This avoids the complex flatbuffers API nesting issues

    # Collect all data
    all_bufs = [b'']  # buffer 0 = empty
    buf_map = {}  # tensor_name → buffer_index
    
    # Assign buffers to tensors
    bias_bufs = {}
    for i, (name, data) in enumerate(biases.items(), start=1):
        bias_bufs[name] = i
        all_bufs.append(data)  # align to 4 bytes
    weight_bufs = {}
    for name, data in weights.items():
        idx = len(all_bufs)
        weight_bufs[name] = idx
        data_aligned = data + b'\x00' * ((4 - len(data) % 4) % 4)
        all_bufs.append(data_aligned)
    other_bufs = {}
    for name, data in other.items():
        idx = len(all_bufs)
        all_bufs.append(data)
        other_bufs[name] = idx

    n_bufs = len(all_bufs)
    print(f"Total buffers: {n_bufs}, Total weight bytes: {sum(len(b) for b in all_bufs)}")

    # Build a stripped-down TFLite model by patching the existing .tflite template
    # This is the most reliable approach: take a valid TFLite, replace weights
    from pathlib import Path
    template_dir = Path(onnx_path).parent

    # Build the tflite as raw flatbuffer
    # Use the proper flatbuffers API
    builder = Builder(65536)

    # ---- Buffer Table (manual offset-based) ----
    # Each buffer: [offset to data]
    buf_data_offsets = []
    for raw in all_bufs:
        buf_data_offsets.append(builder.CreateByteVector(raw))
    
    builder.StartVector(4, len(buf_data_offsets), 4)
    for off in reversed(buf_data_offsets):
        builder.PrependUOffsetTRelative(off)
    buffers_vec = builder.EndVector(len(buf_data_offsets))

    # ---- OperatorCodes (minimal set) ----
    ops_used = ['ADD','AVERAGE_POOL_2D','','CONV_2D','DEPTHWISE_CONV_2D',
                '','','','FULLY_CONNECTED','','','','','','','','','RELU',
                'MUL','','','','RESHAPE','','SOFTMAX','','','','','','','',
                '','','','','','','','','','','','','','','','','','','',
                '','','','','','','','','','','','','','','','','','','', 'MEAN']
    
    opcode_objs = []
    for name in ops_used:
        ns = builder.CreateString(name)
        builder.StartObject(2)
        builder.PrependUOffsetTRelativeSlot(0, ns, 0)
        builder.PrependInt8Slot(1, 1, 0)
        opcode_objs.append(builder.EndObject())
    
    builder.StartVector(4, len(opcode_objs), 4)
    for o in reversed(opcode_objs):
        builder.PrependUOffsetTRelative(o)
    opcodes_vec = builder.EndVector(len(opcode_objs))

    # ---- Map tensors to buffer indices ----
    t_buffer = {}  # tensor name → buffer index
    # Input: buffer 0
    t_buffer[g.input[0].name] = 0
    # Biases
    for name, idx in bias_bufs.items():
        t_buffer[name] = idx
    # Weights
    for name, idx in weight_bufs.items():
        t_buffer[name] = idx
    # Other
    for name, idx in other_bufs.items():
        t_buffer[name] = idx

    # ---- Built Op list from ONNX graph ----
    op_list = []
    for node in g.node:
        op_list.append(node)
    
    # Assign tensor IDs (0 = input, N-1 = output)
    tid = {}  # name → id
    next_id = 0
    
    # Input
    inp_name = g.input[0].name
    tid[inp_name] = next_id; next_id += 1
    
    # All other tensors from nodes
    for node in op_list:
        for inp in node.input:
            if inp not in tid:
                tid[inp] = next_id; next_id += 1
        for out in node.output:
            if out not in tid:
                tid[out] = next_id; next_id += 1
    
    n_tensors = next_id
    print(f"Tensors: {n_tensors}, Nodes: {len(op_list)}")

    # ---- Build Tensor objects ----
    tensor_objs = []
    for name, t_id in sorted(tid.items(), key=lambda x: x[1]):
        ns = builder.CreateString(name)
        shape = inp_shape if name == inp_name else out_shape if name == g.output[0].name else [1]
        sv = builder.CreateNumpyVector(np.array(shape, dtype=np.int32))
        
        # Quant params
        qp_obj = 0
        if name in scales:
            b2 = Builder(256)
            b2.StartObject(4)
            b2.PrependFloat32Slot(2, scales[name], 0.0)
            b2.PrependInt64Slot(3, zps.get(name, 0), 0)
            qp_data = b2.Finish(b2.EndObject())
            # Re-do quantization object properly
            builder.StartObject(4)
            qname = builder.CreateString("")
            builder.PrependUOffsetTRelativeSlot(0, qname, 0)
            builder.PrependFloat32Slot(2, scales[name], 0.0)
            builder.PrependInt64Slot(3, zps.get(name, 0), 0)
            qp_obj = builder.EndObject()
        
        buf_idx = t_buffer.get(name, 0)
        dtype = 9  # INT8 default for intermediate tensors
        if name.endswith('.bias') or 'bias' in name.lower():
            dtype = 2  # INT32
        elif name in scales:
            dtype = 0  # FLOAT32
        
        builder.StartObject(6)
        builder.PrependUOffsetTRelativeSlot(0, ns, 0)
        builder.PrependUOffsetTRelativeSlot(1, sv, 0)
        builder.PrependInt8Slot(2, dtype, 0)
        builder.PrependUint32Slot(3, buf_idx, 0)
        if qp_obj:
            builder.PrependUOffsetTRelativeSlot(4, qp_obj, 0)
        tensor_objs.append(builder.EndObject())
    
    builder.StartVector(4, len(tensor_objs), 4)
    for t in reversed(tensor_objs):
        builder.PrependUOffsetTRelative(t)
    tensors_vec = builder.EndVector(len(tensor_objs))

    # ---- Build Operator objects (pass-through identity for now) ----
    op_objs = []
    for node in op_list:
        in_ids = [tid[n] for n in node.input if n in tid]
        out_ids = [tid[n] for n in node.output if n in tid]
        if not in_ids or not out_ids:
            continue
        
        in_v = builder.CreateNumpyVector(np.array(in_ids, dtype=np.int32))
        out_v = builder.CreateNumpyVector(np.array(out_ids, dtype=np.int32))
        
        opcode = 0  # default ADD
        if node.op_type == 'Conv' or node.op_type == 'ConvInteger':
            opcode = 3  # CONV_2D
        elif 'Relu' in node.op_type:
            opcode = 17  # RELU
        elif node.op_type == 'Softmax':
            opcode = 25  # SOFTMAX
        elif node.op_type == 'Mul':
            opcode = 18  # MUL
        elif node.op_type == 'Add':
            opcode = 0   # ADD
        elif node.op_type == 'MatMul' or node.op_type == 'MatMulInteger':
            opcode = 9   # FULLY_CONNECTED
        elif node.op_type == 'AveragePool' or node.op_type == 'GlobalAveragePool':
            opcode = 77  # MEAN
        elif node.op_type == 'Reshape' or node.op_type == 'Flatten':
            opcode = 22  # RESHAPE
        
        builder.StartObject(6)
        builder.PrependUOffsetTRelativeSlot(0, in_v, 0)
        builder.PrependUOffsetTRelativeSlot(1, out_v, 0)
        builder.PrependUint32Slot(2, opcode, 0)
        op_objs.append(builder.EndObject())
    
    builder.StartVector(4, len(op_objs), 4)
    for o in reversed(op_objs):
        builder.PrependUOffsetTRelative(o)
    ops_vec = builder.EndVector(len(op_objs))

    # ---- SubGraph ----
    in_ids_v = builder.CreateNumpyVector(np.array([0], dtype=np.int32))
    out_id = len(tid) - 1
    out_ids_v = builder.CreateNumpyVector(np.array([out_id], dtype=np.int32))
    sg_name = builder.CreateString("main")
    
    builder.StartObject(6)
    builder.PrependUOffsetTRelativeSlot(0, tensors_vec, 0)
    builder.PrependUOffsetTRelativeSlot(1, in_ids_v, 0)
    builder.PrependUOffsetTRelativeSlot(2, out_ids_v, 0)
    builder.PrependUOffsetTRelativeSlot(3, ops_vec, 0)
    builder.PrependUOffsetTRelativeSlot(4, sg_name, 0)
    sg_obj = builder.EndObject()
    
    builder.StartVector(4, 1, 4)
    builder.PrependUOffsetTRelative(sg_obj)
    sgs_vec = builder.EndVector(1)

    # ---- Model ----
    ver = builder.CreateString("3.0.0")
    desc = builder.CreateString("onnx2tflite_v2")
    builder.StartObject(6)
    builder.PrependUOffsetTRelativeSlot(0, ver, 0)
    builder.PrependUOffsetTRelativeSlot(1, opcodes_vec, 0)
    builder.PrependUOffsetTRelativeSlot(2, sgs_vec, 0)
    builder.PrependUOffsetTRelativeSlot(3, desc, 0)
    builder.PrependUOffsetTRelativeSlot(4, buffers_vec, 0)
    model_obj = builder.EndObject()
    
    builder.Finish(model_obj, b'TFL3')
    
    with open(tflite_path, 'wb') as f:
        f.write(builder.Output())
    
    size = len(builder.Output())
    print(f"Done: {tflite_path} ({size} bytes = {size/1024:.1f} KB)")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python onnx2tflite_v2.py <input.onnx> <output.tflite>")
        sys.exit(1)
    onnx2tflite(sys.argv[1], sys.argv[2])
