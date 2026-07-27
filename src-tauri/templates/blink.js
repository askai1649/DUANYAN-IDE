// SNAR IDE Template: LED Blink
// Basic GPIO control - toggle LED on/off
import { GPIO, Timer } from "snar:hw";

let led = GPIO.output(2);  // GPIO2 (onboard LED)

Timer.interval(500, () => {
    led.toggle();
});
