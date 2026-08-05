/* t0h_scan.c - 脉宽实测 + T0H 全扫描
 * 1) SYSTIC(0x600C0064) 实测 T0H 脉冲宽度, 换算 sclk
 * 2) 相位 P1..P8: T0H=1..8, 每相位纯红 3 秒 (T0L=17-T0H, T1H=17, T1L=5) */
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
#define SYSTIC_LO (*(volatile unsigned int *)0x600C0064)
#define SYSTIC_CONF (*(volatile unsigned int *)0x600C005C)
#define REG(addr) (*(volatile unsigned int *)(addr))
#define RMT_BASE 0x60016000u
#define RMT_RAM  0x60016800u
#define GPIO_IN1 0x60004040u
#define PIN 48u
#define BC ((1u << 8) | (1u << 16) | (1u << 6))

static void putc_(char c) {
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void drain(void) { while (((UART0_STATUS >> 16) & 0x3FF) != 0) { } }
static void putu(unsigned int v) {
    char b[12]; int i = 0;
    if (v == 0) { putc_('0'); return; }
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) putc_(b[--i]);
}

static unsigned int g_lpc = 500;
static unsigned int cal_loop(void) {
    putc_('C'); putc_('C'); putc_('C'); putc_('C'); putc_('C');
    unsigned int k = 0;
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { k++; }
    return k;
}
/* 等 UART 排空 ms 毫秒: 每 86.8us 排空一字符 => lpc/5 迭代/字符 */
static void drain_ms(unsigned int ms) {
    unsigned int chars = ms * 1000u / 87u;
    unsigned int per = g_lpc / 5u;
    while (chars--) {
        volatile unsigned int it = per;
        while (it--) { }
        while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
    }
}

static inline unsigned int pin_high(void) {
    return (REG(GPIO_IN1) >> 16) & 1u;
}

/* 配置一帧: 全部比特按 (t0h,t0l) 或 (t1h,t1l), 此处发纯红 0x00FF00 */
static void arm_frame(unsigned int t0h, unsigned int t0l,
                      unsigned int t1h, unsigned int t1l) {
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
    unsigned int grb = 0x00FF00u;
    for (int i = 0; i < 24; i++) {
        unsigned int hi = t0h, lo = t0l;
        if ((grb >> (23 - i)) & 1u) { hi = t1h; lo = t1l; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = 128u;
    ram[25] = 0u;
}
static void trigger(void) {
    REG(RMT_BASE + 0x6C) = 1u;
    REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
    REG(RMT_BASE + 0x20) = BC;
    REG(RMT_BASE + 0x20) = BC | (1u << 24);
    REG(RMT_BASE + 0x20) = BC | (1u << 0);
}

/* 用 SYSTIC 实测高电平脉宽 (发送全1帧 0xFFFFFF, 首个高段 = T1H 脉冲) */
static unsigned int measure_high_ticks(unsigned int t1h) {
    /* 发全1帧, 帧内前24段都是 (t1h,t1l); 抓首个高段 */
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
    for (int i = 0; i < 24; i++) { ram[i] = t1h | (1u << 15) | (5u << 16); }
    ram[24] = 128u;
    ram[25] = 0u;
    REG(RMT_BASE + 0x6C) = 1u;
    REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
    REG(RMT_BASE + 0x20) = BC;
    REG(RMT_BASE + 0x20) = BC | (1u << 24);
    unsigned int guard = 4000000u;
    while (pin_high() && guard--) { }        /* 确保从低开始 */
    REG(RMT_BASE + 0x20) = BC | (1u << 0);   /* 开始发送 */
    guard = 4000000u;
    while (!pin_high() && guard--) { }       /* 等上升沿 */
    unsigned int t0 = SYSTIC_LO;
    guard = 4000000u;
    while (pin_high() && guard--) { }        /* 等下降沿 */
    unsigned int t1 = SYSTIC_LO;
    return t1 - t0;   /* CPU 定时器 ticks (1 tick = 1 CPU 周期) */
}

void main2(void);
void _start(void) __attribute__((section(".entry"), used, noreturn));
void _start(void) { main2(); for (;;) { } }

void main2(void) {
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
    SYSTIC_CONF = (SYSTIC_CONF & ~0xFFu) | 0x01u | (1u << 30); /* step=1, SYSTIC_CLK_EN=bit30 */
    REG(0x60009000u + PIN * 4u) = (1u << 12) | (3u << 10);
    REG(0x60004000u + 0x554u + PIN * 4u) = 81u;
    REG(RMT_BASE + 0xC0) = (1u << 0) | (1u << 1) | (1u << 3)
                         | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    REG(RMT_BASE + 0x20) = BC;

    puts_("[DUANYAN-OK] t0h_scan\r\n");
    drain();

    /* T0H 全扫描: P1..P8, 每相位纯红 3 秒; 每轮开头重打测量值 */
    for (;;) {
        g_lpc = cal_loop();
        puts_("LPC="); putu(g_lpc); puts_("\r\n");
        drain();

        /* CPU 频率实测: 5 字符(434us)内 SYSTIC 走多少 tick */
        {
            unsigned int s0 = SYSTIC_LO;
            putc_('M'); putc_('M'); putc_('M'); putc_('M'); putc_('M');
            while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
            unsigned int s1 = SYSTIC_LO;
            puts_("SYSTIC434="); putu(s1 - s0); puts_("\r\n");
            drain();
        }

        /* 脉宽实测 x3 */
        for (int m = 0; m < 3; m++) {
            unsigned int ticks = measure_high_ticks(17u);
            puts_("T1H17ticks="); putu(ticks); puts_("\r\n");
            drain();
            unsigned int per = g_lpc / 5u;
            volatile unsigned int it = per * 10;
            while (it--) { }
        }

        for (unsigned int t0h = 1; t0h <= 8; t0h++) {
            puts_("P"); putu(t0h); puts_(" T0H="); putu(t0h); puts_("\r\n");
            drain();
            arm_frame(t0h, 17u - t0h, 17u, 5u);
            /* 3 秒: 用标定循环, 每 100ms 重发一帧 */
            for (int seg = 0; seg < 30; seg++) {
                trigger();
                unsigned int it = (g_lpc * 100u) / 434u * 1000u;
                volatile unsigned int v = 0;
                while (it--) { v++; }
                (void)v;
            }
        }
    }
}