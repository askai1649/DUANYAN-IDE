// 示例 1: 点亮 ESP32-S3 板载 RGB 灯 (实测跑通)
// 目标板: ESP32-S3-N16R8 (板载 WS2812, 数据脚 GPIO48, 供电脚 GPIO47)

import { RGB } from "hardy:hw";

// 常亮白色: 单次发送, 灯珠锁存保持
RGB.setColor(255, 255, 255);
