/* uart_paddirv.c - 纯直驱判决: 完全绕开 GPIO 矩阵
 * W1TS/W1TC 直接驱动 GPIO4/GPIO5, 同时读 GPIO_IN bit4/5 回读
 * (gclk3 已证 GPIO_IN 读取机制活: flash 脚 26-31 读出 1)
 * 直驱也不生效 => pad 输出级死 (RTC 域/VDD3P3_RTC/芯片问题)
 * 慢钟注意: delay 拉伸约 7x
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

#define R(a) (*(volatile unsigned int *)(a))
#define GPIO_BASE  0x60004000u
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_IN_REG        R(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_REG    R(GPIO_BASE + 0x20)
#define GPIO_OUT_REG       R(GPIO_BASE + 0x04)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

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
    const char *d = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(d[(v >> i) & 15]);
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

    delay_ms(300);
    uart_puts("\r\n[PADDRV] pure direct drive GPIO4/5, no matrix\r\n");

    /* IOMUX: 只给 FUN_IE(回读用), 保留其余默认; 不碰 FUNC_OUT_SEL */
    IOMUX_PIN(4) |= (1u << 9);
    IOMUX_PIN(5) |= (1u << 9);

    /* 纯直驱使能 */
    GPIO_ENABLE_W1TS = (1u << 4) | (1u << 5);

    uart_puts("init en="); put_hex(GPIO_ENABLE_REG); uart_puts("\r\n");

    for (;;) {
        GPIO_OUT_W1TS = (1u << 4) | (1u << 5);
        delay_ms(300);
        uart_puts("HI en="); put_hex(GPIO_ENABLE_REG);
        uart_puts(" out="); put_hex(GPIO_OUT_REG);
        uart_puts(" in4="); put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts(" in5="); put_hex((GPIO_IN_REG >> 5) & 1); uart_puts("\r\n");
        delay_ms(700);
        GPIO_OUT_W1TC = (1u << 4) | (1u << 5);
        delay_ms(300);
        uart_puts("LO en="); put_hex(GPIO_ENABLE_REG);
        uart_puts(" out="); put_hex(GPIO_OUT_REG);
        uart_puts(" in4="); put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts(" in5="); put_hex((GPIO_IN_REG >> 5) & 1); uart_puts("\r\n");
        delay_ms(700);
    }
}
