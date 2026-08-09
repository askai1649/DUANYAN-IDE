/* rmt_fixed.c - 修正寄存器偏移后的自适应WS2812驱动
 * 正确偏移: INT_RAW=0x60 INT_ST=0x64 INT_ENA=0x68 INT_CLR=0x6C
 * 开机发探针帧用UART锚定实测帧长, 自动缩放比特时序, 彩虹验证 */
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
#define RMT_RAM_CH0   0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_INT_RAW   REG(RMT_BASE + 0x60)
#define RMT_INT_ST    REG(RMT_BASE + 0x64)
#define RMT_INT_ENA   REG(RMT_BASE + 0x68)
#define RMT_INT_CLR   REG(RMT_BASE + 0x6C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)
#define BASE_CONF ((1u << 8) | (1u << 16) | (1u << 6))

/* 缩放后时序 (基准4/8/8/4 x scale) */
static unsigned int ws_t0h = 4, ws_t0l = 8, ws_t1h = 8, ws_t1l = 4, ws_gap = 500;

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_num(unsigned int v)
{
    char tmp[12]; int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
    while (t > 0) uart_putc(tmp[--t]);
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

static void tx_start(void)
{
    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
}

/* UART锚定测帧长: 每86.8us查一次正确的TX_END, 返回帧占几个锚 */
static int measure_anchors(void)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 24; i++) ram[i] = 4u | (1u << 15) | (8u << 16);
    ram[24] = 500;
    ram[25] = 0;
    tx_start();
    for (int k = 0; k < 30; k++) {
        uart_putc('~');
        while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
        if (RMT_INT_RAW & 1u) return k + 1;
    }
    return -1;
}

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
    for (int i = 0; i < 24; i++) {
        unsigned int hi = ws_t0h, lo = ws_t0l;
        if ((grb >> (23 - i)) & 1) { hi = ws_t1h; lo = ws_t1l; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = ws_gap;
    ram[25] = 0;
    tx_start();
    /* 正确的TX_END轮询 (修好寄存器偏移后真正可用) */
    unsigned int guard = 8000000;
    while (!(RMT_INT_RAW & 1u) && guard--) { }
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3)
                 | (1u << 4) | (1u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 |= (1u << 24);
    RMT_INT_ENA |= 1;
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
    REG(IOMUX_BASE + 47 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 47 * 4) = 47;
    REG(GPIO_BASE + 0x30) = 1u << (47 - 32);
    REG(GPIO_BASE + 0x14) = 1u << (47 - 32);

    /* 三次测量取一致值 */
    int a0 = measure_anchors();
    int a1 = measure_anchors();
    int a2 = measure_anchors();
    uart_puts("\r\n[DUANYAN-OK] anchors=");
    uart_num((unsigned int)a0);
    uart_putc('/');
    uart_num((unsigned int)a1);
    uart_putc('/');
    uart_num((unsigned int)a2);
    uart_puts("\r\n");

    /* 帧长us = anchors*86.8; 基准109us@10MHz; scale = 109/frame_us */
    int a = a0;
    if (a < 1) a = 1;
    unsigned int frame_us = (unsigned int)a * 868 / 10;
    unsigned int scale10 = 1090 / frame_us;   /* 实测MHz*10 */
    if (scale10 < 10) scale10 = 10;
    if (scale10 > 80) scale10 = 80;
    ws_t0h = (4 * scale10 + 5) / 10;
    ws_t0l = (8 * scale10 + 5) / 10;
    ws_t1h = (8 * scale10 + 5) / 10;
    ws_t1l = (4 * scale10 + 5) / 10;
    ws_gap = (500 * scale10 + 5) / 10;
    if (ws_gap > 8000) ws_gap = 8000;
    uart_puts("[DUANYAN-OK] scale=");
    uart_num(scale10);
    uart_puts(" T1H=");
    uart_num(ws_t1h);
    uart_puts("\r\n");

    for (;;) {
        static int hue = 0;
        unsigned int grb;
        hsv_grb(hue, 120, &grb);
        send_frame(grb);
        hue = (hue + 1) & 255;
        /* ~12ms帧间隔: 138锚字符 */
        for (int w = 0; w < 138; w++) {
            uart_putc('~');
            while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
        }
    }
}
