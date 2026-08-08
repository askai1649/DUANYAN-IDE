/* uart_rxrst.c - RX 核心唤醒 v1:
 * 现状: pad->GPIO_IN 活, 矩阵常量->U0 FSM 活, U1TX 活; 唯独 U1RX FSM 对任何输入零反应
 * 手段: 回读 CLK_CONF → RX_RST_CORE(bit27) 脉冲 → RST_CORE(bit23) 脉冲
 *       → 重测常量伪帧(RXD_CNT+cnt) → 真帧自环回
 *       若仍死: SCLK_SEL 换 RTC 8M(2) 再试
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
#define GPIO_OUT         R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC R(GPIO_BASE + 0x28)
#define GPIO_FUNC_OUT_SEL(pin) R(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)

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
static void u1_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((U1(0x1C) >> 16) & 0x3FFu) < 127) break;
    }
    U1(0x00) = (unsigned int)c;
}
static void drain(void)
{
    for (int i = 0; i < 130; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }
}
/* 常量伪帧 + RXD_CNT + FSM 判决 */
static void synth_test(const char *tag)
{
    drain();
    (void)U1(0x30);
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    delay_ms(20);
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6);
    delay_ms(20);
    unsigned int rc = U1(0x30);
    unsigned int f1 = U1(0x6C);
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    delay_ms(30);
    unsigned int cnt = U1(0x1C) & 0x3FFu;
    uart_puts(tag);
    uart_puts(" rcnt="); put_hex(rc);
    uart_puts(" fsm="); put_hex(f1);
    uart_puts(" fifo="); put_hex(cnt);
    if (cnt > 0 && cnt <= 128) {
        uart_puts(" d:");
        for (unsigned int k = 0; k < cnt && k < 4; k++) put_hex(U1(0x00) & 0xFFu);
    }
    uart_puts((cnt > 0) ? " << RX-ALIVE\r\n" : "\r\n");
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
    uart_puts("\r\n[RXRST] init\r\n");
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    uart_puts("[RXRST] clkconf="); put_hex(U1(0x78));
    uart_puts(" insel15="); put_hex(GPIO_FUNC_IN_SEL(15)); uart_puts("\r\n");

    synth_test("[RXRST] before-rst:");

    /* RX 核心复位脉冲: RX_RST_CORE(b27) 先 1 后 0 */
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25) | (1u << 27);
    delay_ms(2);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    delay_ms(2);
    /* 整体 RST_CORE(b23) 脉冲 */
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25) | (1u << 23);
    delay_ms(2);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    delay_ms(5);
    uart_puts("[RXRST] clkconf2="); put_hex(U1(0x78)); uart_puts("\r\n");
    synth_test("[RXRST] after-rst:");

    /* 换 SCLK_SEL=2 (RTC 8M), CLKDIV=8M/115200≈69 */
    U1(0x78) = (2u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x14) = (8000000u / 115200u);
    delay_ms(5);
    synth_test("[RXRST] sclk8m:");

    /* 真帧自环回 (GPIO2 出+入) */
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x14) = (40000000u / 115200u);
    IOMUX_PIN(2) |= (1u << 9) | (1u << 8);
    GPIO_FUNC_IN_SEL(15) = 2 | (1u << 6);
    GPIO_FUNC_OUT_SEL(2) = 15;
    GPIO_ENABLE_W1TS = (1u << 2);
    drain();
    const char *msg = "HELLO";
    for (int i = 0; i < 5; i++) u1_putc(msg[i]);
    delay_ms(30);
    unsigned int cnt = U1(0x1C) & 0x3FFu;
    uart_puts("[RXRST] real cnt="); put_hex(cnt); uart_puts(" d=");
    for (unsigned int i = 0; i < cnt && i < 8; i++) uart_putc((char)(U1(0x00) & 0xFFu));
    uart_puts(cnt == 5 ? " << MATCH\r\n" : " << MISS\r\n");
    GPIO_ENABLE_W1TC = (1u << 2);

    /* U0 对照: 常量接 U0RXD(sig6), 读 U0 RXD_CNT —— 区分矩阵/常量坏 vs U1 RX 时钟死 */
    (void)R(0x60000030u);                /* U0 RXD_CNT 读清 */
    GPIO_FUNC_IN_SEL(6) = CONST1 | (1u << 6);
    delay_ms(10);
    GPIO_FUNC_IN_SEL(6) = CONST0 | (1u << 6);
    delay_ms(20);
    unsigned int u0rc = R(0x60000030u);
    GPIO_FUNC_IN_SEL(6) = CONST1 | (1u << 6);
    uart_puts("[RXRST] u0 rcnt="); put_hex(u0rc);
    uart_puts(" u0 fsm="); put_hex(R(0x6000006Cu));
    uart_puts(u0rc ? " << U0-CLK-ALIVE\r\n" : " << U0-CLK-DEAD\r\n");
    uart_puts("[RXRST] done\r\n");
    for (;;) { delay_ms(1000); }
}
