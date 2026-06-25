# Communication Module (通信上报模块)

## 概述

验证通过后通过 UART 发送 JSON 事件给 4G/BLE 模组。

## 协议

### 硬件

| ESP32-C3 | 模组 |
|----------|------|
| GPIO21 (TX) | RX |
| GPIO20 (RX) | TX |
| GND | GND |

### 参数

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验位 | 无 |
| 流控 | 无 |

### JSON 格式

```json
{
  "dev": "esp32c3_kws_01",
  "evt": "wake",
  "conf": 0.92,
  "sim": 0.83,
  "ts": 1720000000
}
```

### 握手

1. ESP32 → 模组: JSON + "\r\n"
2. 模组 → ESP32: "OK\r\n" (超时 500ms)
3. 超时 → 重试 1 次
4. 错误恢复: `uart_driver_delete()` 清理资源

## API

| 函数 | 说明 |
|------|------|
| `event_reporter_init()` | 初始化 UART + 错误清理 |
| `event_reporter_send_wake(conf, sim)` | 发送唤醒事件 + 等待 ACK |

## 依赖

- ESP-IDF UART Driver
- FreeRTOS
