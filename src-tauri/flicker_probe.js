import { RGB, Timer } from "hardy:hw";
let phase = 0;
let n = 0;
let hue = 0;
while (true) {
    if (phase == 0) {
        if (n == 0) {
            console.log("PHASE-A STATIC-12ms");
        }
        RGB.setColor(16, 0, 0);
        n = n + 1;
        if (n >= 640) {
            phase = 1;
            n = 0;
            hue = 0;
        }
        Timer.delay(12);
    } else {
        if (phase == 1) {
            if (n == 0) {
                console.log("PHASE-B RAINBOW-12ms");
            }
            RGB.setHue(hue);
            hue = hue + 1;
            if (hue >= 256) {
                hue = 0;
            }
            n = n + 1;
            if (n >= 640) {
                phase = 2;
                n = 0;
                hue = 0;
            }
            Timer.delay(12);
        } else {
            if (n == 0) {
                console.log("PHASE-C RAINBOW-48ms");
            }
            RGB.setHue(hue);
            hue = hue + 1;
            if (hue >= 256) {
                hue = 0;
            }
            n = n + 1;
            if (n >= 160) {
                phase = 0;
                n = 0;
                hue = 0;
            }
            Timer.delay(48);
        }
    }
}
