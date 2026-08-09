/* uart_g5in2.c - GPIO5 纯输入监视 (照抄 g5mon 成功配置), 长窗口 300s
 * 判据: Pi 用 pinctrl 把脚8 设为输出驱 Low/High, 看 g5 是否跟随
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
#define GPIO_IN            R(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_num(unsigned int v)
{
    char b[12]; int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[i++] = '0' + v % 10; v /= 10; }
    while (i--) uart_putc(b[i]);
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

    /* GPIO5 纯输入: 输出禁用 + FUN_IE (bit9), 与 g5mon 完全一致 */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);

    delay_ms(500);
    uart_puts("\r\n[G5IN2] GPIO5 input watch 300s @100ms, Pi drives pin8 now\r\n");

    int last = -1;
    for (int t = 0; t < 3000; t++) {
        int v = (GPIO_IN >> 5) & 1;
        if (v != last) {
            uart_puts("[G5IN2] t="); put_num(t);
            uart_puts(" g5="); put_num(v);
            uart_puts("\r\n");
            last = v;
        }
        delay_ms(100);
    }
    uart_puts("[G5IN2] done\r\n");
    for (;;) { delay_ms(1000); }
}
