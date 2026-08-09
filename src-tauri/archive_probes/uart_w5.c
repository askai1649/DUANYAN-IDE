/* uart_w5.c - RX 线终极判决: UART1 配置好但不开外设驱动,
 * GPIO5 纯输入 + 矩阵 U1RXD←GPIO5, 轮询 RX FIFO 计数。
 * Pi 侧阶段2 用 ttyAMA0 连发 UDRIVE → 若 rxfifo>0 → RX 通路全活!
 * (GPIO_IN 寄存器疑冻结, 改用 FIFO 计数作判决) */
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
#define GPIO_ENABLE_W1TC   GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)

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

    delay_ms(1000);
    uart_puts("\r\n[W5] UART1 RX-only judge via FIFO count\r\n");

    /* UART1 时钟+配置 (与 uart_link 一致) */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    /* GPIO5 纯输入 + 矩阵 U1RXD←GPIO5; GPIO4 不动(保持释放) */
    GPIO_ENABLE_W1TC = (1u << 5);
    GPIO_FUNC_OUT_SEL(5) = 118;
    GPIO_REG(IOMUX_BASE + 4 + 5 * 4) = (1u << 12) | (1u << 9) | (1u << 8);
    GPIO_FUNC_IN_SEL(15) = 5 | (1u << 6);
    uart_puts("[W5] insel15=");
    {
        unsigned int v = GPIO_FUNC_IN_SEL(15);
        const char *h = "0123456789ABCDEF";
        uart_puts("0x");
        for (int k = 7; k >= 0; k--) uart_putc(h[(v >> (k * 4)) & 0xF]);
    }
    uart_puts("\r\n");

    for (int i = 0; i < 100; i++) {
        unsigned int cnt = UART1_STATUS & 0x3FFu;
        uart_puts("[W5] i=");
        uart_putc('0' + (i / 10) % 10);
        uart_putc('0' + i % 10);
        uart_puts(" rxfifo=");
        if (cnt > 99) { uart_putc('9'); uart_putc('9'); }
        else { uart_putc('0' + cnt / 10); uart_putc('0' + cnt % 10); }
        if (cnt > 0 && cnt <= 128) {
            uart_puts(" data:");
            for (unsigned int k = 0; k < cnt && k < 16; k++) {
                char c = (char)(UART1_FIFO & 0xFFu);
                uart_putc((c >= 0x20 && c < 0x7F) ? c : '.');
            }
        }
        uart_puts("\r\n");
        delay_ms(1000);
    }
    for (;;) { delay_ms(1000); }
}
