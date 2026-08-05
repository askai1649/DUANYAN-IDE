/* HardyScript → ESP32-S3 Bare-metal Blink
 * GPIO2 (板载LED) 闪烁
 * 无需 ESP-IDF，直接操作寄存器
 */

/* ESP32-S3 GPIO 寄存器基址 */
#define GPIO_BASE       0x60004000UL
#define GPIO_OUT_REG    (*(volatile unsigned int *)(GPIO_BASE + 0x04))
#define GPIO_OUT_W1TS   (*(volatile unsigned int *)(GPIO_BASE + 0x08))
#define GPIO_OUT_W1TC   (*(volatile unsigned int *)(GPIO_BASE + 0x0C))
#define GPIO_ENABLE_REG (*(volatile unsigned int *)(GPIO_BASE + 0x20))

/* IO MUX */
#define IO_MUX_BASE     0x60009000UL
#define IO_MUX_GPIO2    (*(volatile unsigned int *)(IO_MUX_BASE + 2*4))

#define LED_PIN 2

static void delay(volatile unsigned int count) {
    while (count--) {
        __asm__ volatile("nop");
    }
}

void app_main(void) {
    /* 配置 GPIO2 为输出 */
    GPIO_ENABLE_REG |= (1 << LED_PIN);

    /* 无限闪烁 */
    while (1) {
        GPIO_OUT_W1TS = (1 << LED_PIN);  /* LED ON */
        delay(500000);
        GPIO_OUT_W1TC = (1 << LED_PIN);  /* LED OFF */
        delay(500000);
    }
}

/* 入口点 (ESP32-S3 ROM 从 0x403B2AE0 跳转) */
void _start(void) __attribute__((section(".entry")));
void _start(void) {
    app_main();
}
