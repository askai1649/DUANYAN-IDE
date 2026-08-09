/* uart_bridge2.c - 桥接判决 v2: GPIO6 方波源, GPIO4 显式输出禁用
 * 每周期打印: pad6, pad4, EN 寄存器全值 — 定位是谁在拉低桥接点
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
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define GPIO_IN_REG        R(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_REG    R(GPIO_BASE + 0x20)
#define GPIO_OUT_REG       R(GPIO_BASE + 0x04)
#define GPIO_FUNC_OUT_SEL(p) R(GPIO_BASE + 0x554 + (p) * 4)
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
    uart_puts("\r\n[BRIDGE2] GPIO6 src -> bridge -> GPIO4; GPIO4 OE forced OFF\r\n");

    /* GPIO4: 彻底释放 — 输出禁用 + 输入使能 + GPIO矩阵功能 */
    GPIO_ENABLE_W1TC = (1u << 4);
    IOMUX_PIN(4) = (2u << 12) | (1u << 9);
    GPIO_FUNC_OUT_SEL(4) = 128;

    /* GPIO6: 方波源 */
    IOMUX_PIN(6) = (2u << 12) | (2u << 10) | (1u << 9);
    GPIO_FUNC_OUT_SEL(6) = 128;
    GPIO_ENABLE_W1TS = (1u << 6);
    GPIO_ENABLE_W1TC = (1u << 4);   /* 再保险一次 */

    uart_puts("init en="); put_hex(GPIO_ENABLE_REG);
    uart_puts(" out="); put_hex(GPIO_OUT_REG); uart_puts("\r\n");

    for (;;) {
        GPIO_OUT_W1TS = (1u << 6);
        delay_ms(800);
        uart_puts("HI p6="); put_hex((GPIO_IN_REG >> 6) & 1);
        uart_puts(" p4="); put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts(" en="); put_hex(GPIO_ENABLE_REG); uart_puts("\r\n");
        delay_ms(200);
        GPIO_OUT_W1TC = (1u << 6);
        delay_ms(800);
        uart_puts("LO p6="); put_hex((GPIO_IN_REG >> 6) & 1);
        uart_puts(" p4="); put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts(" en="); put_hex(GPIO_ENABLE_REG); uart_puts("\r\n");
        delay_ms(200);
    }
}
