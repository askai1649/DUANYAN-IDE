/* sclk_measure.c - UART锚定实测RMT帧时长, 一锤定音确定sclk频率
 * 方法: 发帧后轮询GPIO48输入电平, 数上升沿(=24个比特)与总高电平时长
 * 用纯CPU循环计数, 再用UART单字符86.8us锚定标定循环速率 */
#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_T0CONFIG  (*(volatile unsigned int *)0x6001F000)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define REG(addr) (*(volatile unsigned int *)(addr))
#define RMT_BASE 0x60016000u
#define RMT_RAM  0x60016800u
#define GPIO_IN  0x60004040u  /* GPIO_IN1_REG: GPIO32-63, GPIO48=bit16 */
#define PIN 48u

static void putc_(char c) {
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(unsigned int v) {
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) putc_(t[--n]);
}
static void drain(void) { while (((UART0_STATUS >> 16) & 0x3FF) != 0) { } }

/* 校准CPU轮询速率: 写5字符等排空=434us, 数空转循环次数 */
static unsigned int cal_loop(void) {
    putc_('C'); putc_('C'); putc_('C'); putc_('C'); putc_('C');
    unsigned int k = 0;
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { k++; }
    return k;  /* 循环数 / 434us */
}

static void send_frame(void) {
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
    const unsigned int BC = (1u << 8) | (1u << 16) | (1u << 6);
    for (int i = 0; i < 24; i++) {
        ram[i] = 3u | (1u << 15) | (9u << 16);
    }
    ram[24] = 500u;
    ram[25] = 0u;
    REG(RMT_BASE + 0x6C) = 1u;
    REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
    REG(RMT_BASE + 0x20) = BC;
    REG(RMT_BASE + 0x20) = BC | (1u << 24);
    REG(RMT_BASE + 0x20) = BC | (1u << 0);
}

void sclk_main(void);
void _start(void) __attribute__((section(".entry"), used, noreturn));
void _start(void) { sclk_main(); for (;;) { } }

void sclk_main(void) {
    /* 看门狗全关 */
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

    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);   /* RMT 时钟使能 = bit9 */
    /* GPIO48 输出配置 (IOMUX + 信号81路由) */
    REG(0x60009000u + PIN * 4u) = (1u << 12) | (3u << 10);
    REG(0x60004000u + 0x554u + PIN * 4u) = 81u;
    /* SYS_CONF 与已验证的 HAL 完全一致 */
    REG(RMT_BASE + 0xC0) = (1u << 0) | (1u << 1) | (1u << 3)
                         | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    const unsigned int BC = (1u << 8) | (1u << 16) | (1u << 6);
    REG(RMT_BASE + 0x20) = BC;

    puts_("[DUANYAN-OK] sclk_measure\r\n");
    drain();
    unsigned int lpc = cal_loop();  /* 循环数/434us */
    puts_("LPC="); num_(lpc); puts_("\r\n"); drain();

    for (int rep = 0; rep < 3; rep++) {
        for (int i = 0; i < 400000; i++) { __asm__ volatile("nop"); }
        /* 单帧拉长测量: 每比特 hi=3000/lo=3000 ticks, 24比特=144000 ticks
         * @10MHz=14.4ms @40MHz=3.6ms, 粗采样也能精确测出 ticks/us */
        const unsigned int BC = (1u << 8) | (1u << 16) | (1u << 6);
        {
            volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM;
            for (int i = 0; i < 24; i++) {
                ram[i] = 3000u | (1u << 15) | (3000u << 16);
            }
            ram[24] = 0u;
            ram[25] = 0u;
            REG(RMT_BASE + 0x6C) = 1u;
            REG(RMT_BASE + 0x20) = BC | (1u << 1) | (1u << 2);
            REG(RMT_BASE + 0x20) = BC;
            REG(RMT_BASE + 0x20) = BC | (1u << 24);
            REG(RMT_BASE + 0x20) = BC | (1u << 0);
        }
        /* 不等上升沿: 触发瞬间即帧起点, 头部多计的少量低电平迭代
         * 只引入 <1% 误差, 而等上升沿会系统性截掉帧头 */
        /* 量总活跃时长: 低电平连续 2000 次采样(≈1.7ms)才判结束,
         * 远大于最长单段(3000 ticks), 不会在帧内误停 */
        unsigned int act = 0, lowr = 0;
        for (;;) {
            unsigned int lvl = (REG(GPIO_IN) >> 16) & 1u;
            act++;
            if (!lvl) { lowr++; if (lowr > 2000) break; } else { lowr = 0; }
            if (act > 40000000) break;
        }
        act -= lowr;  /* 去掉尾部低电平计数 */
        /* 总时长 us_x10 = act*4340/lpc, 32位分段防溢出 */
        unsigned int us_x10 = (act / lpc) * 4340u + ((act % lpc) * 4340u) / lpc;
        /* sclk = 144000 ticks / 时长us (单位 ticks/us = MHz) */
        unsigned int mhz_x100 = 1440000000u / (us_x10 == 0 ? 1 : us_x10);
        puts_("M"); num_((unsigned int)rep);
        puts_(" act="); num_(act);
        puts_(" tot_usx10="); num_(us_x10);
        puts_(" mhzx100="); num_(mhz_x100);
        puts_("\r\n");
        drain();
        for (int i = 0; i < 2000000; i++) { __asm__ volatile("nop"); }
    }
    for (;;) { __asm__ volatile("nop"); }
}