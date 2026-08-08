/* uart_loop.c - UART1 内部环回自检: RX 输入取自自己的 TX 引脚 GPIO4
 * 若读回与发送一致 → UART1 模块+波特率正确, 问题在外部接线/树莓派侧
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

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)
#define GPIO_ENABLE_W1TS       GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_READ       GPIO_REG(GPIO_BASE + 0x20)

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
static void u1_putc(char c)
{
    while (((UART1_STATUS >> 16) & 0x3FF) >= 127) {}
    UART1_FIFO = (unsigned int)c;
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

    delay_ms(5000);
    uart_puts("\r\n[LOOP] UART1 self-loopback test v6 (IOMUX偏移修正)\r\n");

    /* UART1 初始化 */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    /* CLKDIV 一次性写全: 分频347(XTAL 40MHz → 115k) + SCLK_SEL=3(XTAL)
     * + SCLK_EN(b22) + TX_SCLK_EN(b24) + RX_SCLK_EN(b25) + MEM_CLK_EN(b28) */
    UART1_CLKDIV = (40000000u / 115200u);   /* XTAL 40MHz / 347 ≈ 115273 */
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25); /* DIV_NUM=0, XTAL, 全使能 */
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);  /* BIT_NUM[3:2]=3(8位)+STOP[5:4]=2+FIFO复位 */
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);  /* 8N1+MEM_CLK_EN+CLK_EN; 环回走 GPIO4 pad */
    uart_puts("[LOOP] clkdiv=");
    put_hex(UART1_CLKDIV);
    uart_puts(" conf0=");
    put_hex(UART1_CONF0);
    uart_puts("\r\n");
    GPIO_REG(IOMUX_BASE + 4 + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8); /* MCU_SEL=1+FUN_IE+DRV2+上拉 (GPIO4=+0x14) */
    GPIO_FUNC_OUT_SEL(4) = 15;          /* GPIO4 = U1TXD */
    GPIO_ENABLE_W1TS = (1u << 4);       /* ★输出使能: 不开这个 pad 是高阻, TX 信号出不去 */
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6); /* U1RXD ← GPIO4 (自己的TX) */
    uart_puts("[LOOP] iomux4=");
    put_hex(GPIO_REG(IOMUX_BASE + 4 + 4 * 4));
    uart_puts(" outsel4=");
    put_hex(GPIO_FUNC_OUT_SEL(4));
    uart_puts(" en=");
    put_hex(GPIO_ENABLE_READ);
    uart_puts(" insel15=");
    put_hex(GPIO_FUNC_IN_SEL(15));
    uart_puts("\r\n");
    delay_ms(10);

    for (int round = 0; round < 5; round++) {
        /* 清 FIFO (读空) */
        for (int i = 0; i < 128; i++) {
            if ((UART1_STATUS & 0x3FFu) == 0) break;
            (void)UART1_FIFO;
        }
        /* 发送 5 字节 */
        const char *msg = "HELLO";
        for (int i = 0; i < 5; i++) u1_putc(msg[i]);
        delay_ms(20);  /* 5字节@115200 ≈ 0.43ms, 留足裕量 */
        unsigned int cnt = UART1_STATUS & 0x3FFu;
        uart_puts("[LOOP] round=");
        uart_putc('0' + round);
        uart_puts(" rxfifo=");
        uart_putc('0' + (cnt > 9 ? 9 : cnt));
        uart_puts(" data=");
        int ok = (cnt == 5);
        for (unsigned int i = 0; i < cnt && i < 8; i++) {
            uart_putc((char)(UART1_FIFO & 0xFF));
        }
        uart_puts(ok ? "  << MATCH\r\n" : "  << MISMATCH\r\n");
        delay_ms(800);
    }
    uart_puts("[LOOP] done\r\n");
    for (;;) { delay_ms(1000); }
}
