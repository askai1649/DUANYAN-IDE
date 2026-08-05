/* rmt_scaled.c - 按40MHz sclk缩放后的时序三相位彩虹对照
 * 时序: T0H=16(0.4us) T0L=32(0.8us) T1H=32(0.8us) T1L=16(0.4us)
 * gap=2000(50us). 三相位各400帧, 帧间隔20锚字符(1.7ms)
 * A=红 B=绿 C=彩虹流转. 稳定判据靠肉眼+串口标记同步 */
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
#define GPIO_BASE 0x60004000
#define IOMUX_BASE 0x60009000
#define RMT_BASE   0x60016000
#define RMT_RAM_CH0 0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_INT_RAW   REG(RMT_BASE + 0x70)
#define RMT_INT_ST    REG(RMT_BASE + 0x74)
#define RMT_INT_ENA   REG(RMT_BASE + 0x78)
#define RMT_INT_CLR   REG(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void anchor_1char(void)
{
    uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
}
static void wait_anchor(int chars)
{
    for (int i = 0; i < chars; i++) anchor_1char();
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

/* 40MHz缩放时序 */
#define T0H 16u
#define T0L 32u
#define T1H 32u
#define T1L 16u
#define GAP 2000u

static void hsv_grb(int hue, int val, unsigned int *out)
{
    int r = 0, g = 0, b = 0;
    int p = hue & 255;
    if (p < 85) { r = 255 - p * 3; g = 0; b = p * 3; }
    else {
        if (p < 170) { p -= 85; r = 0; g = p * 3; b = 255 - p * 3; }
        else { p -= 170; r = p * 3; g = 255 - p * 3; b = 0; }
    }
    r = (r * val) / 255; g = (g * val) / 255; b = (b * val) / 255;
    *out = ((unsigned int)g << 16) | ((unsigned int)r << 8) | (unsigned int)b;
}

static void send_frame(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    const unsigned int BASE_CONF = (1u << 8) | (1u << 16) | (1u << 6);
    for (int i = 0; i < 24; i++) {
        unsigned int hi = T0H, lo = T0L;
        if ((grb >> (23 - i)) & 1) { hi = T1H; lo = T1L; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = GAP;
    ram[25] = 0;
    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
    wait_anchor(1);  /* 帧约27us, 86.8us锚足够发完 */
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3)
                 | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = (1u << 8) | (1u << 16) | (1u << 6);
    RMT_CH0CONF0 |= (1u << 24);
    RMT_INT_ENA |= 1;
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
    REG(IOMUX_BASE + 47 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 47 * 4) = 47;
    REG(GPIO_BASE + 0x30) = 1u << (47 - 32);
    REG(GPIO_BASE + 0x14) = 1u << (47 - 32);

    uart_puts("\r\n[DUANYAN-OK] rmt-scaled 40MHz timings\r\n");

    for (;;) {
        unsigned int grb;
        /* 相位A: 纯红 */
        uart_puts("\r\n[DUANYAN-OK] PHASE-A RED\r\n");
        for (int i = 0; i < 400; i++) {
            send_frame(0x00FF00);
            wait_anchor(19);
        }
        /* 相位B: 纯绿 */
        uart_puts("\r\n[DUANYAN-OK] PHASE-B GREEN\r\n");
        for (int i = 0; i < 400; i++) {
            send_frame(0xFF0000);
            wait_anchor(19);
        }
        /* 相位C: 彩虹流转 */
        uart_puts("\r\n[DUANYAN-OK] PHASE-C RAINBOW\r\n");
        for (int i = 0; i < 400; i++) {
            hsv_grb(i & 255, 120, &grb);
            send_frame(grb);
            wait_anchor(19);
        }
    }
}
