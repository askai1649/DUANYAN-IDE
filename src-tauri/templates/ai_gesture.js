// DUANYAN IDE Template: AI Gesture Light Strip
// QianLu Mini + MPU6050 + WS2812 NeoPixel
import { GPIO, Timer } from "snar:hw";
import { Brain } from "snar:ai";
import { NeoPixel } from "snar:pack/neopixel";
import { IMU } from "snar:pack/mpu6050";

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
