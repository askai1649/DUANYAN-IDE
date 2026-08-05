// DUANYAN IDE Template: OLED Display
// I2C OLED screen text display
import { I2C, Timer } from "snar:hw";

let oled = I2C.init(0, 0x3C);  // I2C0, SSD1306 address

let line = 0;

Timer.interval(2000, () => {
    oled.write([0x00, 0x10]);  // Set cursor to line
    oled.write("DUANYAN IDE v1.0");
    oled.write("\nLine: " + line);
    line = line + 1;
    console.log("Display updated, line:", line);
});
