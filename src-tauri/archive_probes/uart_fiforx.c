/* uart_fiforx.c - RX FIFO 通路判决 v1:
 * 新疑点: FIFO_RST(bit17/18)不自清零 → FIFO 钉在复位态 → FSM 收帧也进不去/读不出
 * 1) 初始化后回读 CONF0, 若 bit17/18 残留 → 显式清零
 * 2) RXD_CNT(+0x30): pad 信号到达 U1 FSM 的直接证据 (读清, 计数低脉冲)
 * 3) GPIO2 慢伪帧 → 读 RXD_CNT + FSM + cnt
 * 4) U1TX 经 GPIO2 真帧环回 → cnt
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
#define GPIO_IN          R(GPIO_BASE + 0x3C)
#define GPIO_OUT         R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC R(GPIO_BASE + 0x28)
#define GPIO_FUNC_OUT_SEL(pin) R(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)

#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))

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
    uart_puts("\r\n[FIFORX] init\r\n");
    R(0x600C0018u) |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    unsigned int c0 = U1(0x20);
    uart_puts("[FIFORX] conf0 after rst pulse="); put_hex(c0); uart_puts("\r\n");
    /* ★ 显式清 FIFO_RST: 若残留会钉死 FIFO */
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    delay_ms(2);
    uart_puts("[FIFORX] conf0 final="); put_hex(U1(0x20)); uart_puts("\r\n");

    /* RX 挂 GPIO2 (已证输入活), TX 也挂 GPIO2 (自环回) */
    IOMUX_PIN(2) |= (1u << 9) | (1u << 8);
    GPIO_FUNC_IN_SEL(15) = 2 | (1u << 6);

    /* ---- 1) RXD_CNT: 手动拉低 GPIO2, 看 FSM 侧低脉冲计数 ---- */
    (void)U1(0x30);                      /* 读清 */
    GPIO_ENABLE_W1TS = (1u << 2);
    GPIO_OUT &= ~(1u << 2);
    delay_ms(2);
    GPIO_OUT |= (1u << 2);
    delay_ms(2);
    GPIO_OUT &= ~(1u << 2);
    delay_ms(2);
    GPIO_OUT |= (1u << 2);
    delay_ms(5);
    unsigned int rc = U1(0x30);
    uart_puts("[FIFORX] rxd_cnt="); put_hex(rc);
    uart_puts(" fsm="); put_hex(U1(0x6C));
    uart_puts(" cnt="); put_hex(U1(0x1C) & 0x3FFu);
    uart_puts(rc ? " << PAD->FSM OK\r\n" : " << PAD->FSM DEAD\r\n");
    GPIO_ENABLE_W1TC = (1u << 2);

    /* ---- 2) 真帧自环回: GPIO2 输出 U1TXD, 同时供 U1RXD ---- */
    drain();
    (void)U1(0x30);
    GPIO_FUNC_OUT_SEL(2) = 15;
    GPIO_ENABLE_W1TS = (1u << 2);
    delay_ms(5);
    const char *msg = "HELLO";
    for (int i = 0; i < 5; i++) u1_putc(msg[i]);
    delay_ms(30);
    unsigned int cnt = U1(0x1C) & 0x3FFu;
    unsigned int rc2 = U1(0x30);
    unsigned int fsm = U1(0x6C);
    uart_puts("[FIFORX] real: cnt="); put_hex(cnt);
    uart_puts(" rxd_cnt="); put_hex(rc2);
    uart_puts(" fsm="); put_hex(fsm);
    uart_puts(" d=");
    for (unsigned int i = 0; i < cnt && i < 8; i++) uart_putc((char)(U1(0x00) & 0xFFu));
    uart_puts(cnt == 5 ? " << MATCH\r\n" : " << MISS\r\n");
    uart_puts("[FIFORX] done\r\n");
    for (;;) { delay_ms(1000); }
}
