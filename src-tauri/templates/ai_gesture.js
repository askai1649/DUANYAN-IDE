// DUANYAN IDE Template: AI Gesture Light Strip
// QianLu Mini + MPU6050 + WS2812 NeoPixel
import { GPIO, Timer } from "hardy:hw";
import { Brain } from "hardy:ai";
import { NeoPixel } from "hardy:pack/neopixel";
import { IMU } from "hardy:pack/mpu6050";

Brain.init("/sd/duanyan/gesture-10m-int4.duanyan");

let strip = NeoPixel.init(12, 30);  // GPIO12, 30 LEDs
let imu = IMU.init();

Timer.interval(200, () => {
    let motion = imu.read();
    let gesture = Brain.classify(motion, [
        "wave", "fist", "point_up", "point_down", "shake", "none"
    ]);
    if (gesture == "wave")       strip.rainbow(10);
    if (gesture == "fist")       strip.pulse(255, 0, 0);
    if (gesture == "point_up")   strip.brighten(20);
    if (gesture == "point_down") strip.dim(20);
    if (gesture == "shake")      strip.sparkle();
});
