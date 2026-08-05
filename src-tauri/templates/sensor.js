// DUANYAN IDE Template: I2C Sensor Reading
// Read temperature and humidity from I2C sensor
import { I2C, Timer } from "snar:hw";

let sensor = I2C.init(0, 0x27);  // I2C0, address 0x27

Timer.interval(1000, () => {
    let data = sensor.read(6);
    let temp = (data[0] * 256 + data[1]) / 10.0;
    let humidity = (data[2] * 256 + data[3]) / 10.0;
    console.log("Temp:", temp, "C  Humidity:", humidity, "%");
});
