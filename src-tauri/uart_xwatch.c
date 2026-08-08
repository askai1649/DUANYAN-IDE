/* uart_xwatch.c - 交叉驱动观察端: GPIO4 设为纯输入(释放输出驱动),
 * 每 ~1s 打印 GPIO_IN bit4 电平, 持续 120 次
 * Pi 侧同时用 GPIO14 输出 Low/High 交替 → 若 ESP32 读数跟随 → 接线通
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
#define GPIO_IN            GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_READ   GPIO_REG(GPIO_BASE + 0x20)
#define GPIO_ENABLE_W1TC   GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) uart_putc(h[(v >> (i * 4)) & 0xF]);
}
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
    uart_puts("\r\n[XWATCH2] GPIO4 pure input, watch level (EN/IOMUX readback)\r\n");

    /* 彻底释放 GPIO4: 关输出使能 + 矩阵输出切常量 + IOMUX GPIO功能+输入使能, 无上拉 */
    GPIO_ENABLE_W1TC = (1u << 4);
    GPIO_FUNC_OUT_SEL(4) = 118;
    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (1u << 9);
    uart_puts("[XWATCH2] en=");
    put_hex(GPIO_ENABLE_READ);
    uart_puts(" iomux4=");
    put_hex(GPIO_REG(IOMUX_BASE + 4 + 4 * 4));
    uart_puts(" outsel4=");
    put_hex(GPIO_FUNC_OUT_SEL(4));
    uart_puts("\r\n");

    for (int i = 0; i < 120; i++) {
        unsigned int v = GPIO_IN;
        uart_puts("[XWATCH2] i=");
        uart_putc('0' + (i / 10) % 10);
        uart_putc('0' + i % 10);
        uart_puts(" in=");
        put_hex(v);
        uart_puts(" bit4=");
        uart_putc('0' + ((v >> 4) & 1));
        uart_puts(" bit5=");
        uart_putc('0' + ((v >> 5) & 1));
        uart_puts("\r\n");
        delay_ms(1000);
    }
    for (;;) { delay_ms(1000); }
}
