/* uart_g5mon.c - GPIO5 输入活体监视器 (RX 救援线终审)
 * 背景: rxsrc 终审——RXD_INV 生效(1→0) 但矩阵任何源都进不了 RXD,
 *       ILB 证明 RX 解码器活 → 死段=矩阵输入→RXD (FPGA 布线缺陷, 无解)。
 * 救援路线: TX 走硬件 U1(GPIO4, 已证活), RX 用 GPIO5 软件位啜。
 * 本探针: GPIO5 FUN_IE + 输出禁用, 10ms 采样 GPIO_IN bit5, 变化即报, 120s。
 * 配合: Pi 侧向 /dev/ttyAMA0 (GPIO14=TXD) 发长串 0x00 突发, 看 bit5 是否跳 0。
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
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)

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

    /* GPIO5: 输出禁用, FUN_IE 开, 不加内部上下拉 (外线 Pi 驱动) */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);

    delay_ms(500);
    uart_puts("\r\n[G5MON] GPIO5 monitor 120s @10ms, Pi serial burst now\r\n");
    unsigned int last = 9u;
    unsigned int zeros = 0, ones = 0;
    for (unsigned int t = 0; t < 12000; t++) {
        unsigned int v = (GPIO_IN >> 5) & 1u;
        if (v) ones++; else zeros++;
        if (v != last) {                     /* 边沿立即报 */
            uart_puts("[G5MON] t=");
            uart_putc('0' + (t / 1000) % 10);
            uart_putc('0' + (t / 100) % 10);
            uart_putc('0' + (t / 10) % 10);
            uart_putc('0' + t % 10);
            uart_puts(" g5="); uart_putc('0' + v); uart_puts("\r\n");
            last = v;
        }
        delay_ms(10);
    }
    uart_puts("[G5MON] end g5="); uart_putc('0' + ((GPIO_IN >> 5) & 1u));
    uart_puts(" zeros="); put_hex(zeros);
    uart_puts(" ones="); put_hex(ones); uart_puts("\r\n");
    for (;;) { delay_ms(1000); }
}
