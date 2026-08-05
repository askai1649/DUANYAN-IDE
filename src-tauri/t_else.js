import { Timer } from "hardy:hw";
let x = 0;
while (true) {
    if (x == 0) {
        x = 1;
    } else if (x == 1) {
        x = 0;
    }
    Timer.delay(10);
}
