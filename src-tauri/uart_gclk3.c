/* uart_gclk3.c - GPIO6 深诊断: 打印 en/out/iomux 寄存器回读, 判写入是否生效 */
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
#define GPIO_FUNC_OUT_SEL(p) R(GPIO_BASE + 0x554 + (p) * 4)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

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
    uart_puts("\r\n[GCLK3] deep diag GPIO6\r\n");

    PERIP_CLK_EN0 |= (1u << 4);
    uart_puts("clk_en0="); put_hex(PERIP_CLK_EN0); uart_puts("\r\n");

    IOMUX_PIN(6) = (2u << 12) | (2u << 10) | (1u << 9);
    /* ★OEN_SEL=2 (bit21:20=10): 输出使能常开, 绕开 ENABLE 寄存器 */
    GPIO_FUNC_OUT_SEL(6) = 128 | (2u << 20);
    uart_puts("outsel6="); put_hex(GPIO_FUNC_OUT_SEL(6));
    uart_puts(" iomux6="); put_hex(IOMUX_PIN(6)); uart_puts("\r\n");

    GPIO_ENABLE_W1TS = (1u << 6);
    uart_puts("en="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x20));
    uart_puts("\r\n");

    for (;;) {
        GPIO_OUT_W1TS = (1u << 6);
        delay_ms(400);
        uart_puts("HIGH out="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x04));
        uart_puts(" pad="); put_hex((GPIO_IN_REG >> 6) & 1);
        uart_puts(" in="); put_hex(GPIO_IN_REG); uart_puts("\r\n");
        delay_ms(600);
        GPIO_OUT_W1TC = (1u << 6);
        delay_ms(400);
        uart_puts("LOW  out="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x04));
        uart_puts(" pad="); put_hex((GPIO_IN_REG >> 6) & 1);
        uart_puts(" in="); put_hex(GPIO_IN_REG); uart_puts("\r\n");
        delay_ms(600);
    }
}
