#!/usr/bin/env python3
"""
generate_filterbank.py — 预计算 Mel 滤波器组常量 (Flash 存储)

将 micro_frontend.c 中运行时计算的 float[40][257] (41KB BSS)
预计算为 const uint8_t[40][257] (10KB Flash)，释放 41KB SRAM。

量化: uint8 = clamp(float * 255, 0, 255), 反量化: float = uint8 / 255.0

用法:
  python generate_filterbank.py > ../components/frontend/mel_filterbank_const.h
"""

import math
import sys

SAMPLE_RATE = 16000
FFT_SIZE = 512
NUM_CHANNELS = 40
LOWER_BAND_HZ = 125
UPPER_BAND_HZ = 7500
NUM_BINS = FFT_SIZE // 2 + 1  # 257


def hz_to_mel(hz):
    return 2595.0 * math.log10(1.0 + hz / 700.0)


def mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def bin_to_freq(bin):
    return bin * SAMPLE_RATE / FFT_SIZE


def compute_filterbank():
    mel_low = hz_to_mel(LOWER_BAND_HZ)
    mel_high = hz_to_mel(UPPER_BAND_HZ)
    num_mel_pts = NUM_CHANNELS + 2
    mel_step = (mel_high - mel_low) / (NUM_CHANNELS + 1)

    mel_pts = [mel_low + mel_step * i for i in range(num_mel_pts)]
    hz_pts = [mel_to_hz(m) for m in mel_pts]
    bin_pts = [int(h * FFT_SIZE / SAMPLE_RATE) for h in hz_pts]

    filterbank = [[0.0] * NUM_BINS for _ in range(NUM_CHANNELS)]

    for ch in range(NUM_CHANNELS):
        left, peak, right = bin_pts[ch], bin_pts[ch + 1], bin_pts[ch + 2]
        for b in range(left, min(peak + 1, NUM_BINS)):
            if peak > left:
                filterbank[ch][b] = (b - left) / (peak - left)
        for b in range(peak, min(right + 1, NUM_BINS)):
            if right > peak:
                filterbank[ch][b] = (right - b) / (right - peak)

    return filterbank, bin_pts


def quantize(filterbank):
    result = [[0] * NUM_BINS for _ in range(NUM_CHANNELS)]
    for ch in range(NUM_CHANNELS):
        for b in range(NUM_BINS):
            val = filterbank[ch][b] * 255.0
            result[ch][b] = max(0, min(255, int(round(val))))
    return result


def generate_header(filterbank_q8, bin_pts, filterbank_float):
    print("/*")
    print(" * mel_filterbank_const.h — 自动生成的 Mel 滤波器组常量")
    print(" *")
    print(" * 由 tools/generate_filterbank.py 生成")
    print(f" * 参数: {NUM_CHANNELS}ch, {LOWER_BAND_HZ}-{UPPER_BAND_HZ}Hz, {FFT_SIZE}pt FFT")
    print(f" * 存储: const uint8_t[{NUM_CHANNELS}][{NUM_BINS}] = {NUM_CHANNELS * NUM_BINS} bytes (Flash)")
    print(" *")
    print(" * 反量化: float_val = raw_val / 255.0f")
    print(" *")
    print(" * Mel 频点 (Hz):")
    for i, b in enumerate(bin_pts):
        print(f" *   pt[{i}] = bin {b} = {b * SAMPLE_RATE / FFT_SIZE:.0f} Hz")
    print(" */")
    print()
    print("#pragma once")
    print(f"#define MEL_FB_CHANNELS {NUM_CHANNELS}")
    print(f"#define MEL_FB_BINS     {NUM_BINS}")
    print()

    # uint8 table
    print("static const uint8_t mel_filterbank_table[MEL_FB_CHANNELS][MEL_FB_BINS] = {")
    for ch in range(NUM_CHANNELS):
        row = ", ".join(str(filterbank_q8[ch][b]) for b in range(NUM_BINS))
        print(f"    {{{row}}},")
    print("};")
    print()

    # Dequantization function
    print("/**")
    print(" * @brief 反量化 Mel 滤波器组: uint8 → float")
    print(" * @param ch   Mel 通道 [0, 39]")
    print(" * @param bin  FFT bin [0, 256]")
    print(" * @return float 滤波器系数 [0.0, 1.0]")
    print(" */")
    print("static inline float mel_filterbank_get(int ch, int bin)")
    print("{")
    print("    return (float)mel_filterbank_table[ch][bin] / 255.0f;")
    print("}")
    print()

    # Legacy float access (for verification)
    print("/* 参考: 原始 float 值（非零值） */")
    for ch in range(NUM_CHANNELS):
        nonzeros = [(b, filterbank_float[ch][b]) for b in range(NUM_BINS) if filterbank_float[ch][b] > 0.001]
        if nonzeros:
            first, last = nonzeros[0], nonzeros[-1]
            print(f"/* ch{ch:2d}: bins [{first[0]},{last[0]}] peak={filterbank_float[ch][bin_pts[ch+1]]:.4f} */")


if __name__ == "__main__":
    filterbank_float, bin_pts = compute_filterbank()
    filterbank_q8 = quantize(filterbank_float)
    generate_header(filterbank_q8, bin_pts, filterbank_float)
