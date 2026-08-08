/* uart_bitmeas.c v2 - TX 信号源判决 + 救援链路:
 * 阶段A: sig15(U1TXD)→GPIO4+GPIO6, 内测回读测位宽 (T0 偏移已修正: CONF=+0x00, LO=+0x04, UPD=+0x0C)
 * 阶段B: sig6(U0TXD)→GPIO4 救援路由, 控制台文本持续流 120s, Pi 侧 115200 抓包
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
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_FUNC_OUT_SEL(p) R(GPIO_BASE + 0x554 + (p) * 4)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
/* 已验证偏移: CONFIG=+0x00, LO=+0x04, UPDATE=+0x0C */
#define T0CONFIG R(0x6001F000u)
#define T0LO     R(0x6001F004u)
#define T0UPDATE R(0x6001F00Cu)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_u(unsigned int v)
{
    char buf[12]; int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < 11) { buf[i++] = '0' + v % 10; v /= 10; }
    while (i > 0) uart_putc(buf[--i]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static unsigned int t0_now(void) { T0UPDATE = 1; return T0LO; }
static int g6(void) { return (GPIO_IN >> 6) & 1u; }
static void u1_topup(void)
{
    while (((U1(0x1C) >> 16) & 0x3FFu) < 96u) U1(0x00) = (unsigned int)'U';
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

    /* T0: 已验证配置 (分频=1, 递增, 使能); 1 单位=8µs */
    T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);

    delay_ms(300);
    uart_puts("\r\n[BITMEAS2] start, t0 sanity: ");
    unsigned int s0 = t0_now();
    delay_ms(100);
    unsigned int s1 = t0_now();
    put_u(s1 - s0); uart_puts(" (expect ~12 for 100ms)\r\n");

    /* U1 @9600 名义 */
    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 9600u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    /* 阶段A: sig15(U1TXD) → GPIO4 + GPIO6, 内测位宽 */
    uart_puts("[BITMEAS2] phase A: sig15(U1TXD) route GPIO4+GPIO6\r\n");
    IOMUX_PIN(4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    IOMUX_PIN(6) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(6) = 15;
    GPIO_ENABLE_W1TS = (1u << 6);

    u1_topup();
    int found = 0;
    for (volatile int w = 0; w < 20000000; w++) {
        if ((w & 0xFFFF) == 0) u1_topup();
        if (!g6()) { found = 1; break; }
    }
    if (!found) {
        uart_puts("[BITMEAS2] sig15 NO-FALL on GPIO6\r\n");
    } else {
        unsigned int anchor = t0_now();
        uart_puts("[BITMEAS2] sig15 fall seen, gaps (2-bit each, 9600 expect ~26):\r\n");
        for (int k = 0; k < 6; k++) {
            u1_topup();
            unsigned int guard = t0_now();
            while (g6())  { if (t0_now() - guard > 200000u) break; }
            guard = t0_now();
            while (!g6()) { if (t0_now() - guard > 200000u) break; }
            guard = t0_now();
            while (g6())  { if (t0_now() - guard > 200000u) break; }
            unsigned int t = t0_now();
            uart_puts(" gap="); put_u(t - anchor);
            anchor = t;
        }
        uart_puts("\r\n");
    }

    /* 阶段B: sig6(U0TXD) 救援路由 → GPIO4 (保留 GPIO6), 长流 120 轮 */
    uart_puts("[BITMEAS2] phase B: sig6(U0TXD) rescue on GPIO4, streaming 120 rounds @115200\r\n");
    GPIO_FUNC_OUT_SEL(4) = 6;
    GPIO_ENABLE_W1TS = (1u << 4);
    for (int r = 0; r < 120; r++) {
        uart_puts("[RESCUE] round "); put_u(r); uart_puts(" U0TXD-on-GPIO4 active\r\n");
        delay_ms(100);
    }
    uart_puts("[BITMEAS2] done\r\n");
    for (;;) { delay_ms(1000); }
}
