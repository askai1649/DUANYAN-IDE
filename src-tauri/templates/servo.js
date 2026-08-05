// DUANYAN IDE Template: Servo Motor Control
// PWM-driven servo rotation
import { PWM, Timer } from "snar:hw";

let servo = PWM.init(5, 50);  // GPIO5, 50Hz (servo standard)

let angle = 0;
let direction = 1;

Timer.interval(20, () => {
    angle = angle + direction;
    if (angle >= 180 || angle <= 0) { direction = -direction; }
    let duty = 2 + (angle / 180) * 10;  // 2-12% duty cycle
    servo.setDuty(duty);
});
