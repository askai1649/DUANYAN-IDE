/* uart_harddrv.c - 强驱判线探针:
 * GPIO4 输出使能+矩阵直连, 先驱 Low 20s 再驱 High 20s, 交替;
 * 每次打印 ENABLE/OUT 回读, 证明驱动寄存器真实生效。
 * Pi 侧 GPIO14 高阻采样: 跟随 0/1 → 线通; 恒 1 → 线断或 pad 驱动器死
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
#define GPIO_OUT_READ      GPIO_REG(GPIO_BASE + 0x4)
#define GPIO_OUT_W1TS      GPIO_REG(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      GPIO_REG(GPIO_BASE + 0xC)
#define GPIO_ENABLE_READ   GPIO_REG(GPIO_BASE + 0x20)
#define GPIO_ENABLE_W1TS   GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)

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

    delay_ms(1000);
    uart_puts("\r\n[HARDDRV] force-drive GPIO4 Low/High with readbacks\r\n");

    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (2u << 10); /* GPIO功能+DRV2, 无上拉(避免干扰判线) */
    GPIO_FUNC_OUT_SEL(4) = 128;
    GPIO_ENABLE_W1TS = (1u << 4);

    for (int round = 0; round < 8; round++) {
        GPIO_OUT_W1TC = (1u << 4);
        uart_puts("[HARDDRV] DRIVE-LOW  en=");
        put_hex(GPIO_ENABLE_READ);
        uart_puts(" out=");
        put_hex(GPIO_OUT_READ);
        uart_puts("\r\n");
        delay_ms(3000);
        GPIO_OUT_W1TS = (1u << 4);
        uart_puts("[HARDDRV] DRIVE-HIGH en=");
        put_hex(GPIO_ENABLE_READ);
        uart_puts(" out=");
        put_hex(GPIO_OUT_READ);
        uart_puts("\r\n");
        delay_ms(3000);
    }
    for (;;) { delay_ms(1000); }
}
