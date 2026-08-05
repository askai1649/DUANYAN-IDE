// 调试版彩虹: 带串口打印, 用于确认固件真实运行
import { RGB, Timer } from "hardy:hw";

console.log("[DUANYAN-OK] js rainbow boot");

let hue = 0;
let frame = 0;
while (true) {
    RGB.setHue(hue);
    if ((frame % 16) == 0) {
        console.log("hue");
        console.log(hue);
    }
    hue = hue + 1;
    if (hue >= 256) {
        hue = 0;
    }
    frame = frame + 1;
    Timer.delay(20);
}
