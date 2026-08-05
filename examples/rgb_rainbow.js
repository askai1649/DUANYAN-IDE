// ============================================================
// 示例 2: ESP32-S3 板载 RGB 灯 —— 三原色 128 级丝滑渐变
// 目标板: ESP32-S3-N16R8 (板载 WS2812, 数据脚 GPIO48, 供电脚 GPIO47)
//
// 用法 (DUANYAN-IDE):
//   1. 本文件可直接在编辑器中打开、修改
//   2. FlashPanel 选择目标 "esp32s3-bare" → 编译出 .bin
//   3. DUANYAN 烧录器把 .bin 烧进开发板
//
// 效果: 红→绿→蓝→红 无限循环, 每段过渡 128 级 x 80ms = 10.24s
//       一轮约 30.7 秒, 中间色相自然生长 (金黄/青绿/品红),
//       丝滑无闪烁 —— 128 级已逼近 8 位色深的物理极限
//
// 时序背景: WS2812 比特时序 T0H=3/T0L=13/T1H=11/T1L=5
//           (实测 RMT sclk≈11.7MHz 下判决胜出, 已固化在 HAL)
// 可玩: 改 Timer.delay(80) 的数字调节流速, 数字越小流转越快
//       (80 = 一轮约 30.7 秒; 20 = 一轮约 7.7 秒)
// ============================================================

import { RGB, Timer } from "hardy:hw";

// 三原色查表 (下标 0=红 1=绿 2=蓝)
function palR(i) {
    if (i == 0) { return 255; }
    return 0;
}
function palG(i) {
    if (i == 1) { return 255; }
    return 0;
}
function palB(i) {
    if (i == 2) { return 255; }
    return 0;
}

// 从颜色 a 渐变到颜色 b, 共 128 级
function trans(a, b) {
    let t = 0;
    while (t < 128) {
        let r = palR(a) + ((palR(b) - palR(a)) * t) / 127;
        let g = palG(a) + ((palG(b) - palG(a)) * t) / 127;
        let bl = palB(a) + ((palB(b) - palB(a)) * t) / 127;
        RGB.setColor(r, g, bl);
        Timer.delay(80);
        t = t + 1;
    }
}

while (true) {
    trans(0, 1);   // 红 → 绿 (途经金黄、黄绿)
    trans(1, 2);   // 绿 → 蓝 (途经青绿、青蓝)
    trans(2, 0);   // 蓝 → 红 (途经紫蓝、品红)
}
