// flicker_probe2.js - 判决性对照 (无限循环): 分离"刷新"与"数值变化"
import { RGB, Timer } from "hardy:hw";

let i = 0;
let hue = 0;
let flip = 0;

while (true) {
    // PHASE-A: 同一绿色高频刷新 (12ms), 300 帧 (~3.6秒)
    //   若闪 = 刷新/传输问题; 若稳 = 排除传输
    i = 0;
    while (i < 300) {
        RGB.setColor(0, 80, 0);
        Timer.delay(12);
        i = i + 1;
    }

    // PHASE-B: 红绿交替 12ms, 300 帧 (~3.6秒)
    //   83Hz 交替正常应看成混合色; 若明显闪烁 = 值跳变触发
    i = 0;
    flip = 0;
    while (i < 300) {
        if (flip == 0) {
            RGB.setColor(80, 0, 0);
            flip = 1;
        } else {
            RGB.setColor(0, 80, 0);
            flip = 0;
        }
        Timer.delay(12);
        i = i + 1;
    }

    // PHASE-C: 细粒度彩虹 12ms, 765 步两轮 (~18秒)
    i = 0;
    hue = 0;
    while (i < 1530) {
        RGB.setHue(hue);
        hue = hue + 1;
        if (hue >= 765) {
            hue = 0;
        }
        Timer.delay(12);
        i = i + 1;
    }

    // PHASE-D: 细粒度彩虹 48ms, 一轮 (~37秒)
    hue = 0;
    while (hue < 765) {
        RGB.setHue(hue);
        Timer.delay(48);
        hue = hue + 1;
    }
}