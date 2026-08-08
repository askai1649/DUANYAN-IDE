// YL-69 土壤湿度传感器 — HardyScript ADC 端到端示例
// 接线: VCC→3V3, GND→GND, A0→GPIO6 (ADC1_CH5)
// 实测: 干 ≈ 4095, 湿 ≈ 3300 (读数越小 = 越湿)

let i = 0;
while (i < 60) {
    let m = ADC.read(6);
    console.log("moisture=");
    console.log(m);
    // 板载 RGB 指示: 干(>3800) = 红, 湿 = 绿
    if (m > 3800) {
        RGB.setColor(255, 0, 0);
    } else {
        RGB.setColor(0, 255, 0);
    }
    Timer.delay(1000);
    i = i + 1;
}
console.log("done");
