/* rmt_anchor.c - UART锚定测量RMT帧真实时长(不依赖INT_ST/校准)
 * 1个串口字符=86.8us(精确锚). TX_START后每发1字符读一次
 * CH0STATUS/INT_RAW/INT_ST, 帧若未发完状态会持续变化 */
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
#define RMT_CH0STATUS REG(RMT_BASE + 0x28)
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
static void uart_hex(unsigned int v)
{
    const char *H = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) uart_putc(H[(v >> (i * 4)) & 15]);
}
/* 精确86.8us锚: 写1字符并等FIFO彻底排空 */
static void anchor_1char(void)
{
    uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
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

static void send_frame(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    const unsigned int BASE_CONF = (1u << 8) | (1u << 16) | (1u << 6);
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
    RMT_CH0CONF0 = (1u << 8) | (1u << 16) | (1u << 6);
    RMT_CH0CONF0 |= (1u << 24);
    RMT_INT_ENA |= 1;
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;

    REG(IOMUX_BASE + 47 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 47 * 4) = 47;
    REG(GPIO_BASE + 0x30) = 1u << (47 - 32);
    REG(GPIO_BASE + 0x14) = 1u << (47 - 32);

    uart_puts("\r\n[DUANYAN-OK] rmt-anchor probe\r\n");

    for (;;) {
        /* 帧: 红0x00FF00. @10MHz应109us, @40MHz应27us, @80MHz应14us */
        send_frame(0x00FF00);
        for (int k = 0; k < 5; k++) {
            anchor_1char();   /* 精确86.8us */
            uart_puts("[DUANYAN-OK] k=");
            uart_putc((char)('0' + k));
            uart_puts(" ST=");
            uart_hex(RMT_CH0STATUS);
            uart_puts(" RAW=");
            uart_putc((char)('0' + (RMT_INT_RAW & 1)));
            uart_puts(" IST=");
            uart_putc((char)('0' + (RMT_INT_ST & 1)));
            uart_puts("\r\n");
        }
        /* 红灯保持2秒供肉眼确认 */
        for (int i = 0; i < 100; i++) {
            send_frame(0x00FF00);
            for (int w = 0; w < 20; w++) anchor_1char();
        }
    }
}
