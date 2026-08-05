/* rxmeas.c - RMT RX 捕获 GPIO48, 用 5 个 UART 字符(精确434us)做窗尺
 * 捕获窗口内高电平 tick 总数 = sclk*434us
 * 80M->353, 40M->176, 12.7M->56, 9M->39, 2.64M->11 */
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
static inline unsigned int fifo_cnt(void) {
    return (UART0_STATUS >> 16) & 0x3FF;
}
static inline unsigned int tx_done(void) {
    return ((UART0_STATUS >> 14) & 1u);   /* UART_TXFIFO_EMPTY */
}

static unsigned int g_lpc = 350;
static unsigned int cal_loop(void) {
    putc_('C'); putc_('C'); putc_('C'); putc_('C'); putc_('C');
    unsigned int k = 0;
    while (fifo_cnt() != 0) { k++; }
    return k;
}
static void delay_us(unsigned int us) {
    unsigned int it = (g_lpc * us) / 434u;
    volatile unsigned int v = 0;
    while (it--) { v++; }
    (void)v;
}

static void send_t(unsigned int grb, unsigned int t0h, unsigned int t0l,
                   unsigned int t1h, unsigned int t1l) {
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
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
    delay_us(400);
}

/* 帧时长测量: 9400 tick 长帧, 数迭代直到 tx_end, 用 LPC(每字符) 换算字符数 */
static unsigned int g_chars = 0;
static unsigned int g_done_flag = 0;
static void dump_st(const char *tag) {
    puts_(tag);
    putc_(':'); putu(REG(RMT_BASE + 0x60) & 0xFFu);
    putc_(','); putu(REG(RMT_BASE + 0x64) & 0xFFu);
    putc_(','); putu(REG(RMT_BASE + 0x74) & 0xFFu);
    putc_(','); putu(REG(RMT_BASE + 0x78) & 0xFFu);
    puts_("\r\n");
    drain();
}
static void measure_frame(void) {
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
    for (int i = 0; i < 47; i++) {
        ram[i] = 100u | (1u << 15) | (100u << 16);
    }
    ram[47] = 0u;
    REG(RMT_BASE + 0x6C) = 1u;
    REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
    REG(RMT_BASE + 0x20) = BC;
    REG(RMT_BASE + 0x20) = BC | (1u << 24);
    REG(RMT_BASE + 0x54) = 1u;              /* INT_ENA ch0 tx_end */
    REG(RMT_BASE + 0x20) = BC | (1u << 0);
    { volatile unsigned long d = 0; while (d < 600ul) d++; }   /* 帧进行中 */
    dump_st("B");
    unsigned long it = 0;
    unsigned int done = 0;
    while (!done) {
        it++;
        if (REG(RMT_BASE + 0x60) & 1u) { done = 1; break; }        /* INT_ST ch0_tx_end */
        if (((REG(RMT_BASE + 0x74) >> 4) & 7u) == 0u && it > (unsigned long)g_lpc) { done = 2; break; } /* CHSTATUS 空闲 */
        if (it / (unsigned long)g_lpc >= 400u) { it = 0xFFFFFFF0ul; break; } /* 超时 */
    }
    g_chars = (unsigned int)(it / (unsigned long)g_lpc);
    g_done_flag = done;
    dump_st("A");
}

void x_main(void);
void _start(void) __attribute__((section(".entry"), used, noreturn));
void _start(void) { x_main(); for (;;) { } }

void x_main(void) {
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
    REG(0x60004000u + 0x554u + PIN * 4u) = 81u;   /* TX: RMT_SIG_OUT0 */
    REG(0x60004030u) = (1u << 16);                /* ENABLE1_W1TS: GPIO48 输出使能 */
    /* 手动翻转回读探针: T=ab a=拉高后读入 b=拉低后读入, 期望 10 */
    {
        char a, b;
        REG(0x60004014u) = (1u << 16);            /* OUT1_W1TS: 拉高 */
        volatile unsigned long d = 0; while (d < 2000ul) d++;
        a = (REG(0x60004040u) & (1u << 16)) ? '1' : '0';
        REG(0x60004018u) = (1u << 16);            /* OUT1_W1TC: 拉低 */
        volatile unsigned long d2 = 0; while (d2 < 2000ul) d2++;
        b = (REG(0x60004040u) & (1u << 16)) ? '1' : '0';
        puts_("T="); putc_(a); putc_(b); puts_("\r\n");
        drain();
    }
    REG(RMT_BASE + 0xC0) = (1u << 0) | (1u << 1) | (1u << 3)
                         | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    REG(RMT_BASE + 0x20) = BC;                    /* idle_lv=0, idle_out_en=1(bit16): 帧间拉低复位 */
    puts_("CF="); putu(REG(RMT_BASE + 0x20)); puts_("\r\n");
    drain();

    puts_("[DUANYAN-OK] rxmeas\r\n");
    drain();
    g_lpc = cal_loop();
    puts_("LPC="); putu(g_lpc); puts_("\r\n");
    drain();

    /* FIFO 语义探针: 1 字符 从写入到 FIFO 空 = 几个迭代 */
    {
        while (fifo_cnt() != 0) { }
        putc_('#');
        unsigned int k = 0;
        while (fifo_cnt() != 0) { k++; }
        puts_("O1="); putu(k); puts_("\r\n");
        drain();
    }

    for (int r = 0; r < 1; r++) {
        measure_frame();
        puts_("N="); putu(g_chars); putc_('/'); putu(g_done_flag); puts_("\r\n");
        drain();
    }

    /* GPIO48 电平探针: 帧前空闲电平(期望 lo 占大头 = 空闲低) */
    {
        unsigned int hi = 0, lo = 0;
        for (unsigned int i = 0; i < 200000u; i++) {
            if (REG(0x60004040u) & (1u << 16)) hi++; else lo++;
        }
        puts_("IDLE HI="); putu(hi); putc_('/'); putu(lo); puts_("\r\n");
        drain();
    }

    /* GPIO 轮询测帧时长: 9400tick 长帧(50%占空), hi 计数 -> 帧时长 */
    unsigned int t0h, t0l, t1h, t1l;
    {
        volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
        for (int i = 0; i < 47; i++) ram[i] = 100u | (1u << 15) | (100u << 16);
        ram[47] = 0u;
        REG(RMT_BASE + 0x6C) = 1u;
        REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
        REG(RMT_BASE + 0x20) = BC;
        REG(RMT_BASE + 0x20) = BC | (1u << 24);
        REG(RMT_BASE + 0x20) = BC | (1u << 0);
        unsigned int hi = 0, lo = 0;
        for (unsigned int i = 0; i < 200000u; i++) {
            if (REG(0x60004040u) & (1u << 16)) hi++; else lo++;
        }
        puts_("F HI="); putu(hi); putc_('/'); putu(lo); puts_("\r\n");
        /* hi 个迭代 ≈ hi*434/LPC us (同型循环), 帧时长 = 2*hi_us (50%占空)
         * sclk = 9400/帧时长: 80M->帧118us, 9M->1044us, 2.64M->3560us */
        unsigned int fr_us = (hi * 434u / g_lpc) * 2u;
        puts_("FR="); putu(fr_us); puts_("\r\n");
        drain();
        if (fr_us < 300u) {
            t0h = 25u; t0l = 71u; t1h = 70u; t1l = 26u;   /* ~80MHz: 周期 96tick=1.2us */
            puts_("FAST\r\n");
        } else if (fr_us < 2000u) {
            t0h = 3u; t0l = 13u; t1h = 11u; t1l = 5u;     /* ~9MHz: 周期 16tick (static8 已验证) */
            puts_("MID\r\n");
        } else {
            t0h = 1u; t0l = 3u; t1h = 3u; t1l = 1u;       /* ~2.64MHz */
            puts_("SLOW\r\n");
        }
        drain();
    }

    /* 判决测试: 红蓝慢交替 500ms x10s + 白->红渐变 */
    for (;;) {
        for (int k = 0; k < 10; k++) {
            send_t(0x00FF00u, t0h, t0l, t1h, t1l); delay_us(500000);
            send_t(0x0000FFu, t0h, t0l, t1h, t1l); delay_us(500000);
        }
        for (unsigned int t = 0; t <= 63u; t++) {
            unsigned int g = ((63u - t) << 18);           /* G 字节 252->0, 移到 bits23..16 */
            unsigned int rr = (t << 10);                   /* R 字节 0->252, 移到 bits15..8 */
            send_t(g | rr, t0h, t0l, t1h, t1l);
            delay_us(60000);
        }
    }
}