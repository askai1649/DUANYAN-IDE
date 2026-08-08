/* uart_filt.c - RX 滤波器+U0RXD正确信号 判决 v1:
 * 修正: U0RXD = sig 12 (之前误用 sig 6)
 * 新疑点: RX_FILT_REG(+0x18): bit4 RXD_FILT_EN 若被残留置位, RXD 采样被钉死
 * 1) 读 U0/U1 RXFILT 原值 → 显式清 0
 * 2) sig15 挂 CONST0/CONST1 看 U1 RXD
 * 3) sig12 挂 CONST0/CONST1 看 U0 RXD
 * 4) 双 UART 重做常量伪帧收包
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
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u

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
    uart_puts("\r\n[FILT] ===== rx_filt readback =====\r\n");
    uart_puts("u0 filt="); put_hex(U0(0x18));
    uart_puts(" u1 filt="); put_hex(U1(0x18)); uart_puts("\r\n");
    U0(0x18) = 0;
    U1(0x18) = 0;
    uart_puts("cleared: u0="); put_hex(U0(0x18));
    uart_puts(" u1="); put_hex(U1(0x18)); uart_puts("\r\n");

    /* U1 初始化 */
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    uart_puts("[FILT] ===== mux -> rxd watch =====\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    delay_ms(2);
    uart_puts("u1 c1 rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6);
    delay_ms(2);
    uart_puts(" c0 rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    delay_ms(2);
    uart_puts(" c1b rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
    uart_puts("\r\n");

    GPIO_FUNC_IN_SEL(12) = CONST1 | (1u << 6);
    delay_ms(2);
    uart_puts("u0 c1 rxd="); uart_putc('0' + ((U0(0x1C) >> 15) & 1u));
    GPIO_FUNC_IN_SEL(12) = CONST0 | (1u << 6);
    delay_ms(2);
    uart_puts(" c0 rxd="); uart_putc('0' + ((U0(0x1C) >> 15) & 1u));
    GPIO_FUNC_IN_SEL(12) = CONST1 | (1u << 6);
    delay_ms(2);
    uart_puts(" c1b rxd="); uart_putc('0' + ((U0(0x1C) >> 15) & 1u));
    uart_puts("\r\n");

    /* 双 UART 常量伪帧 */
    for (int i = 0; i < 130; i++) { if ((U1(0x1C) & 0x3FFu) == 0) break; (void)U1(0x00); }
    for (int i = 0; i < 130; i++) { if ((U0(0x1C) & 0x3FFu) == 0) break; (void)U0(0x00); }
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6);
    GPIO_FUNC_IN_SEL(12) = CONST0 | (1u << 6);
    delay_ms(20);
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    GPIO_FUNC_IN_SEL(12) = CONST1 | (1u << 6);
    delay_ms(30);
    unsigned int c1 = U1(0x1C) & 0x3FFu, c0 = U0(0x1C) & 0x3FFu;
    uart_puts("[FILT] synth u1="); put_hex(c1);
    uart_puts(" u0="); put_hex(c0); uart_puts("\r\n");
    if (c1 > 0 && c1 <= 128) {
        uart_puts("u1 d:");
        for (unsigned int k = 0; k < c1 && k < 4; k++) put_hex(U1(0x00) & 0xFFu);
        uart_puts("\r\n");
    }
    if (c0 > 0 && c0 <= 128) {
        uart_puts("u0 d:");
        for (unsigned int k = 0; k < c0 && k < 4; k++) put_hex(U0(0x00) & 0xFFu);
        uart_puts("\r\n");
    }
    /* 还原 U0RXD → GPIO43 */
    GPIO_FUNC_IN_SEL(12) = 43 | (1u << 6);
    uart_puts("[FILT] done\r\n");
    for (;;) { delay_ms(1000); }
}
