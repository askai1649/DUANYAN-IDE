// DUANYAN IDE Template: DC Motor Control
// PWM-driven DC motor speed control
import { PWM, GPIO, Timer } from "snar:hw";

let motor = PWM.init(2, 1000);   // GPIO2, 1kHz
let dir = GPIO.output(4);         // Direction pin

let speed = 0;

Timer.interval(100, () => {
    speed = speed + 10;
    if (speed > 100) { speed = 0; dir.toggle(); }
    motor.setDuty(speed);
    console.log("Speed:", speed, "%");
});
