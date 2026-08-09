/* rmt_intchk.c - 纯目视判定正确偏移的TX_END是否真实触发
 * 红=2ms锚内TX_END置位 蓝=未置位 绿=轮询guard耗尽 */
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

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}
static void anchor_1char(void)
{
    uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
}
static void wait_anchors(int n) { for (int i = 0; i < n; i++) anchor_1char(); }

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

static void fill_and_start(void)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 24; i++) ram[i] = 4u | (1u << 15) | (8u << 16);
    ram[24] = 500;
    ram[25] = 0;
    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | (1u << 24);
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
}

static void show(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
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
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
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

    for (;;) {
        /* 测试1: TX_START后轮询正确INT_RAW, 2ms(23锚)内是否置位 */
        fill_and_start();
        int seen = 0;
        for (int k = 0; k < 23 && !seen; k++) {
            anchor_1char();
            if (RMT_INT_RAW & 1u) seen = 1;
        }
        if (seen) {
            show(0x00FF00);   /* 红: TX_END真实触发 */
        } else {
            show(0x0000FF);   /* 蓝: 2ms未置位 */
        }
        wait_anchors(35);     /* ~3ms显示 */

        /* 测试2: 纯CPU轮询(不发锚字符), guard耗尽=绿 */
        fill_and_start();
        unsigned int guard = 400000;
        while (!(RMT_INT_RAW & 1u) && guard--) { }
        if (guard == 0) {
            show(0xFF0000);   /* 绿: guard耗尽 */
        } else {
            show(0x00FF00);   /* 红: 快速置位 */
        }
        wait_anchors(35);
    }
}
