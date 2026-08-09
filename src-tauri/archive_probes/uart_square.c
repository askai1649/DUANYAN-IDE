/* uart_square.c - GPIO4 直驱方波探针 (IOMUX 偏移修正版)
 * 绕过 UART 矩阵, 直接 OUT_W1TS/W1TC 翻转 GPIO4, 1Hz 方波
 * Pi 侧 pi_lvl_probe.py 每秒采样 → 应看到 1/0 交替
 * 若 Pi 看到交替 → pad 输出驱动正常, 问题在 U1TXD 矩阵路
 * 若 Pi 恒 1 → GPIO4 pad 输出驱动死 (或 IOMUX 仍有问题)
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_OUT_W1TS   GPIO_REG(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC   GPIO_REG(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;

    delay_ms(2000);
    uart_puts("\r\n[SQUARE] GPIO4 direct-drive square wave (IOMUX fixed)\r\n");

    /* IOMUX GPIO4 = +0x14: MCU_SEL=1(GPIO) + DRV2 + 上拉; 不开 FUN_IE 无所谓 */
    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (2u << 10) | (1u << 8);
    /* 矩阵旁路: OUT_SEL=128 → 直连 GPIO_OUT */
    GPIO_FUNC_OUT_SEL(4) = 128;
    GPIO_ENABLE_W1TS = (1u << 4);
    uart_puts("[SQUARE] configured, toggling @ ~1Hz...\r\n");

    for (int i = 0; i < 60; i++) {
        GPIO_OUT_W1TS = (1u << 4);
        uart_puts("[SQUARE] HIGH\r\n");
        delay_ms(1000);
        GPIO_OUT_W1TC = (1u << 4);
        uart_puts("[SQUARE] LOW\r\n");
        delay_ms(1000);
    }
    for (;;) { delay_ms(1000); }
}
