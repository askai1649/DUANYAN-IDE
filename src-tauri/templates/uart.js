// SNAR IDE Template: UART Serial Communication
// Send and receive data over UART
import { UART } from "snar:hw";

let serial = UART.init(1, 115200);  // UART1 @ 115200 baud

serial.onData((data) => {
    console.log("Received:", data);
    serial.write("Echo: " + data);
});

serial.write("Hello from SNARjs!\n");
