/* led_rgb_test.c - RED test under three RMT clock hypotheses
 *
 * The system clock tree appears to run at ~1/8 of nominal (TIMG timer
 * measured at 10 MHz instead of 80 MHz). If the RMT "80 MHz" source is
 * actually 10 MHz, all our pulse durations are 8x too long, every bit
 * reads as '1', and the LED always latches 0xFFFFFF = WHITE no matter
 * what color we send. That exactly matches the symptom.
 *
 * This firmware sends PURE RED under three clock hypotheses, 4 s each:
 *   phase 1: assume 80 MHz (T0H=24 ticks)  <- current config
 *   phase 2: assume 10 MHz (T0H=3  ticks)
 *   phase 3: assume 40 MHz (T0H=12 ticks)
 * Whichever phase turns the LED red reveals the true RMT clock.
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

/* ---- RMT ---- */
#define RMT_BASE   0x60016000
#define RMT_RAM_CH0 0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_INT_ST    REG(RMT_BASE + 0x74)
#define RMT_INT_ENA   REG(RMT_BASE + 0x78)
#define RMT_INT_CLR   REG(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)

#define TX_START     (1u << 0)
#define MEM_RD_RST   (1u << 1)
#define APB_MEM_RST  (1u << 2)
#define IDLE_OUT_EN  (1u << 6)
#define CONF_UPDATE  (1u << 24)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
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
    while ((timer_now() - start) < ticks && cap--) { }
}

static void wait_us(unsigned int us)
{
    wait_ticks(timer_now(), us * ticks_per_us);
}

static void calibrate(void)
{
    unsigned int t0 = timer_now();
    for (int i = 0; i < 10; i++) uart_putc('~');
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) { }
    unsigned int ticks = timer_now() - t0;
    ticks_per_us = ticks / 955;
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
    RMT_CH0CONF0 = (1u << 8) | (1u << 16) | IDLE_OUT_EN;
    RMT_CH0CONF0 |= CONF_UPDATE;
    RMT_INT_ENA |= 1;
}

static void rmt_setup_gpio48(void)
{
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;   /* RMT_SIG_OUT0_IDX */
}

/* send one GRB frame with explicit tick durations */
static void ws2812_show(unsigned int grb,
                        unsigned int t0h, unsigned int t0l,
                        unsigned int t1h, unsigned int t1l)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;

    for (int i = 0; i < 24; i++) {
        unsigned int hi = t0h, lo = t0l;
        if ((grb >> (23 - i)) & 1) { hi = t1h; lo = t1l; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = 0; /* end marker */

    RMT_INT_CLR = 1;
    RMT_CH0CONF0 |= MEM_RD_RST | APB_MEM_RST;
    RMT_CH0CONF0 |= CONF_UPDATE;
    RMT_CH0CONF0 |= TX_START;

    unsigned int cap = 1000000;
    while (!(RMT_INT_ST & 1) && cap--) { }
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();

    uart_puts("\r\n[DUANYAN-OK] rgb-test boot\r\n");

    rmt_init();
    rmt_setup_gpio48();

    /* WS2812 GRB order: red=0x00FF00, green=0xFF0000, blue=0x0000FF */
    for (;;) {
        uart_puts("[DUANYAN-OK] RED test (10MHz timing)\r\n");
        for (int n = 0; n < 10; n++) {
            ws2812_show(0x00FF00, 3, 9, 7, 5);
            wait_us(400000);
        }

        uart_puts("[DUANYAN-OK] GREEN test (10MHz timing)\r\n");
        for (int n = 0; n < 10; n++) {
            ws2812_show(0xFF0000, 3, 9, 7, 5);
            wait_us(400000);
        }

        uart_puts("[DUANYAN-OK] BLUE test (10MHz timing)\r\n");
        for (int n = 0; n < 10; n++) {
            ws2812_show(0x0000FF, 3, 9, 7, 5);
            wait_us(400000);
        }
    }
}
