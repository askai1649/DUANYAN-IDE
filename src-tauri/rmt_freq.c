/* rmt_freq.c - 实测 RMT sclk 真实频率
 *
 * 原理: 帧含 24*12 + 500 + 2 = 1090 个 RMT tick (24 比特每比特 12 tick
 * + 500 tick 锁存间隙 + 结束标记)。用已校准 timer 给 TX_START→TX_END
 * 计时, 得真实 us 数, sclk_MHz ≈ 1090 / us。
 * 判据: ~100us → 10MHz (时序 4/8/8/4 正确); ~13us → 80MHz (时序需×8)
 */

#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)

#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)

#define TIMG0_T0CONFIG  (*(volatile unsigned int *)0x6001F000)
#define TIMG0_T0LO      (*(volatile unsigned int *)0x6001F004)
#define TIMG0_T0UPDATE  (*(volatile unsigned int *)0x6001F00C)
#define TIMG0_WDTCONFIG0 (*(volatile unsigned int *)0x6001F048)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_INT_CLR   (*(volatile unsigned int *)0x6001F07C)

#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)

#define REG(addr) (*(volatile unsigned int *)(addr))
#define GPIO_BASE 0x60004000
#define IOMUX_BASE 0x60009000

#define RMT_BASE   0x60016000
#define RMT_RAM_CH0 0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_INT_ST    REG(RMT_BASE + 0x74)
#define RMT_INT_ENA   REG(RMT_BASE + 0x78)
#define RMT_INT_CLR   REG(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_num(unsigned int v)
{
    char tmp[12]; int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (t > 0) uart_putc(tmp[--t]);
}

static unsigned int ticks_per_us = 10;

static unsigned int timer_now(void)
{
    TIMG0_T0UPDATE = 0;
    return TIMG0_T0LO;
}

static void wait_ticks(unsigned int start, unsigned int ticks)
{
    unsigned int cap = (ticks + 10) * 100 + 100000;
    while (timer_now() < start + ticks && cap--) { }
}

static void wait_us(unsigned int us)
{
    wait_ticks(timer_now(), us * ticks_per_us);
}

static void calibrate(void)
{
    unsigned int t0 = timer_now();
    for (int i = 0; i < 1000; i++) uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
    unsigned int ticks = timer_now() - t0;
    ticks_per_us = ticks / 86800;
    if (ticks_per_us == 0) ticks_per_us = 10;
}

static void disable_watchdogs(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_WDTWPROTECT = 0;
    RTC_CNTL_SWD_WPROT = 0x8F1D312A;
    RTC_CNTL_SWD_CONF |= (1u << 30);
    RTC_CNTL_SWD_WPROT = 0;
    TIMG0_WDTWPROTECT = 0x50D83AA1;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0x1;
    TIMG0_WDTWPROTECT = 0;
}

static void rmt_init(void)
{
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3)
                 | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = (1u << 8) | (1u << 16) | (1u << 6);
    RMT_CH0CONF0 |= (1u << 24);
    RMT_INT_ENA |= 1;

    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
}

/* 发一帧 0x00FF00 (纯红), 时序 4/8/8/4, 返回发帧耗时 ticks */
static unsigned int send_timed(void)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    const unsigned int BASE_CONF = (1u << 8) | (1u << 16) | (1u << 6);
    unsigned int grb = 0x00FF00;

    for (int i = 0; i < 24; i++) {
        unsigned int hi = 4, lo = 8;
        if ((grb >> (23 - i)) & 1) { hi = 8; lo = 4; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = 500;
    ram[25] = 0;

    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);
    unsigned int t0 = timer_now();
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
    unsigned int guard = 5000000;
    while (!(RMT_INT_ST & 1u) && guard--) { }
    unsigned int t1 = timer_now();
    return t1 - t0;
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();
    rmt_init();

    /* GPIO47 供电使能 */
    REG(IOMUX_BASE + 47 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 47 * 4) = 47;
    REG(GPIO_BASE + 0x30) = 1u << (47 - 32);
    REG(GPIO_BASE + 0x14) = 1u << (47 - 32);

    uart_puts("\r\n[DUANYAN-OK] rmt-freq probe tpu=");
    uart_num(ticks_per_us);
    uart_puts("\r\n");

    for (;;) {
        /* 取 8 次测量 */
        for (int r = 0; r < 8; r++) {
            unsigned int dt = send_timed();
            uart_puts("[DUANYAN-OK] frame ticks=");
            uart_num(dt);
            uart_puts(" us=");
            uart_num(dt / ticks_per_us);
            uart_puts("\r\n");
            wait_us(200000);
        }
        /* 长停 3 秒红灯 (20ms 刷新), 供肉眼确认颜色正确 */
        for (int i = 0; i < 150; i++) {
            send_timed();
            wait_us(20000);
        }
    }
}
