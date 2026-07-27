// SNAR IDE Template: Button Input
// Read button state with debounce
import { GPIO, Timer } from "snar:hw";

let button = GPIO.input(0, "pullup");  // GPIO0 with internal pull-up
let led = GPIO.output(2);

let lastState = 1;
let count = 0;

Timer.interval(50, () => {
    let state = button.read();
    if (state == 0 && lastState == 1) {
        count = count + 1;
        led.toggle();
        console.log("Button pressed! Count:", count);
    }
    lastState = state;
});
