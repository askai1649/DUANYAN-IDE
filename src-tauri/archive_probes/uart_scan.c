/* uart_scan.c - 4脚电平扫描: 直驱 GPIO4/5 测试, 判断新接线后的实际连接状态 */
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
#define GPIO_IN_READ     GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TS GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_OUT_W1TS    GPIO_REG(GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC    GPIO_REG(GPIO_BASE + 0x0C)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
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
static unsigned int pin(unsigned int p) { return (GPIO_IN_READ >> p) & 1u; }

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

    delay_ms(5000);
    uart_puts("\r\n[SCAN] pin wiring scan v8 (邻址扫描+RXD探针)\r\n");

    uart_puts("[SCAN] raw GPIO_IN(+0x3C)=");
    put_hex(GPIO_IN_READ);
    uart_puts(" STRAP(+0x38)=");
    put_hex(GPIO_REG(GPIO_BASE + 0x38));
    uart_puts(" OUT(+0x4)=");
    put_hex(GPIO_REG(GPIO_BASE + 0x4));
    uart_puts(" EN(+0x20)=");
    put_hex(GPIO_REG(GPIO_BASE + 0x20));
    uart_puts(" CLK_EN0=");
    put_hex(SYSTEM_PERIP_CLK_EN0);
    uart_puts(" PIN4reg(+0x74)=");
    put_hex(GPIO_REG(GPIO_BASE + 0x74 + 4 * 4));
    uart_puts("\r\n");

    /* 测试 A: 4/5/6/7 开内部上拉, 全部高阻, 看上拉能否拉起来 */
    for (unsigned int p = 4; p <= 7; p++) {
        GPIO_REG(IOMUX_BASE + 4 + p * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8); /* FUN_PU */
        GPIO_FUNC_OUT_SEL(p) = 128;
    }
    GPIO_ENABLE_W1TC = 0xF0u;
    delay_ms(5);
    uart_puts("[SCAN] iomux readback:");
    for (unsigned int p = 4; p <= 7; p++) {
        uart_puts(" p"); uart_putc('0' + p); uart_puts("=");
        put_hex(GPIO_REG(IOMUX_BASE + 4 + p * 4));
    }
    uart_puts("\r\n");
    uart_puts("[SCAN] pullup-hiz 4="); uart_putc('0' + pin(4));
    uart_puts(" 5="); uart_putc('0' + pin(5));
    uart_puts(" 6="); uart_putc('0' + pin(6));
    uart_puts(" 7="); uart_putc('0' + pin(7));
    /* 测试 B: 对照脚 16/17/18 (未接线) 同样处理 */
    for (unsigned int p = 16; p <= 18; p++) {
        GPIO_REG(IOMUX_BASE + 4 + p * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
        GPIO_FUNC_OUT_SEL(p) = 128;
    }
    uart_puts(" | ctrl 16="); uart_putc('0' + pin(16));
    uart_puts(" 17="); uart_putc('0' + pin(17));
    uart_puts(" 18="); uart_putc('0' + pin(18));
    /* 对照脚直驱 High */
    GPIO_OUT_W1TS = (7u << 16);
    GPIO_ENABLE_W1TS = (7u << 16);
    delay_ms(5);
    uart_puts(" drvH 16="); uart_putc('0' + pin(16));
    uart_puts(" 17="); uart_putc('0' + pin(17));
    uart_puts(" 18="); uart_putc('0' + pin(18));
    uart_puts("\r\n");

    /* 测试 C: 4/5/6/7 直驱 High (强驱动硬碰硬) */
    GPIO_OUT_W1TS = 0xF0u;
    GPIO_ENABLE_W1TS = 0xF0u;
    delay_ms(5);
    uart_puts("[SCAN] strong-drv 4="); uart_putc('0' + pin(4));
    uart_puts(" 5="); uart_putc('0' + pin(5));
    uart_puts(" 6="); uart_putc('0' + pin(6));
    uart_puts(" 7="); uart_putc('0' + pin(7));
    uart_puts(" OUT="); put_hex(GPIO_REG(GPIO_BASE + 0x4));
    uart_puts(" EN="); put_hex(GPIO_REG(GPIO_BASE + 0x20));
    uart_puts(" outsel4="); put_hex(GPIO_FUNC_OUT_SEL(4));
    uart_puts(" GPIO_IN="); put_hex(GPIO_IN_READ);
    uart_puts("\r\n");

    /* 邻址扫描: 读 0x30~0x44 一圈 */
    uart_puts("[SCAN] near+0x3C:");
    for (unsigned int off = 0x30; off <= 0x44; off += 4) {
        uart_puts(" "); put_hex(off);
        uart_puts("="); put_hex(GPIO_REG(GPIO_BASE + off));
    }
    uart_puts("\r\n");

    /* RXD 探针: UART1 RX 取 GPIO18, 驱动 18 高低, 看 STATUS b15 */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    GPIO_REG(IOMUX_BASE + 4 + 18 * 4) = (1u << 12) | (1u << 9) | (2u << 10);
    GPIO_FUNC_OUT_SEL(18) = 128;
    GPIO_ENABLE_W1TS = (1u << 18);
    GPIO_FUNC_IN_SEL(15) = 18 | (1u << 6);   /* U1RXD ← GPIO18 */
    for (int t = 0; t < 4; t++) {
        if (t & 1) GPIO_OUT_W1TC = (1u << 18); else GPIO_OUT_W1TS = (1u << 18);
        delay_ms(3);
        uart_puts("[SCAN] rxd t"); uart_putc('0' + t);
        uart_puts(" drive="); uart_putc('0' + (1 - (t & 1)));
        uart_puts(" STATUS="); put_hex(UART1_STATUS);
        uart_puts(" RXD="); uart_putc('0' + ((UART1_STATUS >> 15) & 1u));
        uart_puts("\r\n");
    }
    for (;;) { delay_ms(1000); }
}
