/* uart_rxsrc.c - RXD 信号源终审 v5: 真边沿源 + 反相自测
 * 背景: ILB=RX解码器活; sigscan=常量源拉不动 RXD; livescan=pad→GPIO_IN 活
 *       (pin2/6..18/21/33..35/41 LIVE)。矛盾: GPIO_IN 活但 RXD 钉死。
 * 本探针: ①GPIO2 自驱产生真方波 → 挂 sig6/sig12/sig15, 读 U0/U1 RXD 是否跟随
 *         ②RXD_INV(CONF0 bit19) 反相自测: 若 RXD 恒 1 被反相成 0 → 通路其实在,
 *           只是源被钉 1; 若仍 1 → 反相器都不生效, RXD 是死寄存器
 *         ③顺带 LOOPBACK 时读 RXD 看 bit14 是否真把 TXD 接进来 (对照活通路)
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
#define GPIO_OUT           R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

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
static void put_dec(unsigned int v)
{
    char buf[12];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

/* 挂 sig ← GPIO2, 交替驱动 GPIO2, 采 8 点 RXD 序列 */
static void wave_chk(unsigned int sig)
{
    GPIO_FUNC_IN_SEL(sig) = 2 | (1u << 6);
    delay_ms(2);
    uart_puts("[RXSRC] sig"); put_dec(sig); uart_puts(" seq:");
    for (int i = 0; i < 8; i++) {
        if (i & 1) GPIO_OUT |= (1u << 2); else GPIO_OUT &= ~(1u << 2);
        delay_ms(3);
        uart_putc('0' + ((U1(0x1C) >> 15) & 1u));   /* U1 RXD */
    }
    uart_puts("\r\n[RSRC] sig"); put_dec(sig); uart_puts(" u0seq:");
    for (int i = 0; i < 8; i++) {
        if (i & 1) GPIO_OUT |= (1u << 2); else GPIO_OUT &= ~(1u << 2);
        delay_ms(3);
        uart_putc('0' + ((U0(0x1C) >> 15) & 1u));   /* U0 RXD */
    }
    uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(sig) = CONST1 | (1u << 6);
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
    uart_puts("\r\n[RSRC] RXD 信号源终审\r\n");

    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    /* GPIO2 准备: FUN_IE + 输出使能 */
    IOMUX_PIN(2) |= (1u << 9);
    GPIO_ENABLE_W1TS = (1u << 2);

    wave_chk(6);
    wave_chk(12);
    wave_chk(15);

    /* RXD_INV 反相自测 */
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);
    unsigned int base = (U1(0x1C) >> 15) & 1u;
    U1(0x20) |= (1u << 19);     /* RXD_INV */
    delay_ms(2);
    unsigned int inv = (U1(0x1C) >> 15) & 1u;
    uart_puts("[RSRC] inv-test base="); uart_putc('0' + base);
    uart_puts(" inv="); uart_putc('0' + inv);
    uart_puts(" conf0="); put_hex(U1(0x20)); uart_puts("\r\n");
    U1(0x20) &= ~(1u << 19);

    /* LOOPBACK 对照: 内部环回时 RXD 应跟随 TXD 空闲 1; 发 break 拉低看 RXD */
    U1(0x20) |= (1u << 14);
    delay_ms(2);
    unsigned int lb_idle = (U1(0x1C) >> 15) & 1u;
    /* 写 FIFO 一批 0x00 (全0数据+break效果), TXD 拉低期间采 RXD */
    for (int i = 0; i < 4; i++) U1(0x00) = 0x00;
    delay_ms(1);
    unsigned int lb_tx0 = (U1(0x1C) >> 15) & 1u;
    uart_puts("[RSRC] lb idle="); uart_putc('0' + lb_idle);
    uart_puts(" lb-txd0="); uart_putc('0' + lb_tx0);
    uart_puts(" rxfifo="); put_hex(U1(0x1C) & 0x3FFu); uart_puts("\r\n");
    U1(0x20) &= ~(1u << 14);
    for (int i = 0; i < 130; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }

    GPIO_ENABLE_W1TC = (1u << 2);
    GPIO_FUNC_IN_SEL(12) = 43 | (1u << 6);   /* 还原 U0RXD */
    uart_puts("[RSRC] done\r\n");
    for (;;) { delay_ms(1000); }
}
