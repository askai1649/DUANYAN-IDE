/* rmt_scan.c - WS2812 判决门限扫描: 同一红色, 4 相位不同 T0H
 * A: T0H=2(0.16us) B: T0H=3(0.24us) C: T0H=4(0.31us) D: T0H=5(0.39us)
 * 纯红不偏白不变暗的相位 = 正确时序 (sclk 实测 12.71MHz) */
#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define REG(addr) (*(volatile unsigned int *)(addr))
#define RMT_BASE 0x60016000u
#define RMT_RAM  0x60016800u
#define PIN 48u

static void putc_(char c) {
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void drain(void) { while (((UART0_STATUS >> 16) & 0x3FF) != 0) { } }

static unsigned int g_lpc = 500;

static unsigned int cal_loop(void) {
    putc_('C'); putc_('C'); putc_('C'); putc_('C'); putc_('C');
    unsigned int k = 0;
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { k++; }
    return k;
}
static void wait_ms(unsigned int ms) {
    /* lpc 迭代 = 434us → ms 毫秒需 lpc*ms*1000/434 次迭代 */
    unsigned int base = g_lpc * ms;
    unsigned int it = (base / 434u) * 1000u + ((base % 434u) * 1000u) / 434u;
    volatile unsigned int v = 0;
    while (it--) { v++; }
    (void)v;
}

static void send(unsigned int grb, unsigned int t0h, unsigned int t0l,
                 unsigned int t1h, unsigned int t1l) {
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
    const unsigned int BC = (1u << 8) | (1u << 16) | (1u << 6);
    for (int i = 0; i < 24; i++) {
        unsigned int hi = t0h, lo = t0l;
        if ((grb >> (23 - i)) & 1u) { hi = t1h; lo = t1l; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = 128u;
    ram[25] = 0u;
    REG(RMT_BASE + 0x6C) = 1u;
    REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
    REG(RMT_BASE + 0x20) = BC;
    REG(RMT_BASE + 0x20) = BC | (1u << 24);
    REG(RMT_BASE + 0x20) = BC | (1u << 0);
    wait_ms(1);
}

void scan_main(void);
void _start(void) __attribute__((section(".entry"), used, noreturn));
void _start(void) { scan_main(); for (;;) { } }

void scan_main(void) {
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTFEED = 1u;
    RTC_CNTL_WDTCONFIG0 = 0u;
    RTC_CNTL_WDTWPROTECT = 0u;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF = 0x50D83AA1u;
    RTC_CNTL_SWD_CONF = 0x8F1D312Au;
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0u;
    TIMG0_WDTWPROTECT = 0u;
    TIMG0_INT_CLR = 1u;

    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    REG(0x60009000u + PIN * 4u) = (1u << 12) | (3u << 10);
    REG(0x60004000u + 0x554u + PIN * 4u) = 81u;
    REG(RMT_BASE + 0xC0) = (1u << 0) | (1u << 1) | (1u << 3)
                         | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    const unsigned int BC = (1u << 8) | (1u << 16) | (1u << 6);
    REG(RMT_BASE + 0x20) = BC;

    puts_("[DUANYAN-OK] rmt_scan\r\n");
    drain();
    g_lpc = cal_loop();

    for (;;) {
        puts_("PHASE-A T0H=2\r\n"); drain();
        for (int f = 0; f < 2500; f++) send(0x00FF00u, 2u, 14u, 11u, 5u);
        puts_("PHASE-B T0H=3\r\n"); drain();
        for (int f = 0; f < 2500; f++) send(0x00FF00u, 3u, 13u, 11u, 5u);
        puts_("PHASE-C T0H=4\r\n"); drain();
        for (int f = 0; f < 2500; f++) send(0x00FF00u, 4u, 12u, 11u, 5u);
        puts_("PHASE-D T0H=5\r\n"); drain();
        for (int f = 0; f < 2500; f++) send(0x00FF00u, 5u, 11u, 11u, 5u);
    }
}