/* uart_watch.c - GPIO 电平连续监视: 每 300ms 打印 GPIO4/5/6/7 电平
 * 配合 Pi 侧把 GPIO14 置输出翻转, 观察哪路跟随, 定位接线
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
#define GPIO_IN_REG GPIO_REG(GPIO_BASE + 0x3C)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
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
    uart_puts("\r\n[WATCH] level monitor, 40 rounds\r\n");
    /* 4/5/6/7 输入使能 + GPIO 功能 */
    for (int p = 4; p <= 7; p++)
        GPIO_REG(IOMUX_BASE + (unsigned)p * 4) = (1u << 12) | (1u << 9);

    for (int r = 0; r < 40; r++) {
        unsigned int gi = GPIO_IN_REG;
        uart_puts("[W] ");
        for (int p = 4; p <= 7; p++) {
            uart_putc('0' + p);
            uart_putc('=');
            uart_putc('0' + ((gi >> p) & 1));
            uart_putc(' ');
        }
        uart_puts("\r\n");
        delay_ms(300);
    }
    uart_puts("[WATCH] done\r\n");
    for (;;) { delay_ms(1000); }
}
