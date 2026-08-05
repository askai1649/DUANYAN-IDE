// pace_test.js - 测量 JS 主循环真实节拍
// 每 83 步打印一次 tick (标称 12ms/步 时约每秒 1 次)
// 数 10 秒内 tick 个数即可得到真实速度

import { RGB, Timer } from "hardy:hw";

let hue = 0;
let n = 0;

console.log("pace start");

while (true) {
    RGB.setHue(hue);
    hue = hue + 1;
    if (hue >= 256) {
        hue = 0;
    }
    n = n + 1;
    if (n >= 83) {
        n = 0;
        console.log("tick");
    }
    Timer.delay(12);
}
