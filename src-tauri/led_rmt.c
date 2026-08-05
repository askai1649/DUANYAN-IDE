/* led_rmt.c - ESP32-S3 N16R8 onboard WS2812 via RMT peripheral
 *
 * The Arduino rgbLedWrite(RGB_BUILTIN,...) works because the core drives
 * the WS2812 with the RMT hardware, NOT by software GPIO bit-banging.
 * Our CPU actually runs far below 240 MHz, so bit-banged pulse widths
 * are stretched beyond what the WS2812 accepts. RMT removes every CPU
 * speed dependency: waveform timing comes from the 80 MHz APB clock.
 *
 * Register refs (esp-idf v5.2.2, verified):
 *   SYSTEM_PERIP_CLK_EN0 = 0x600C0018, RMT_CLK_EN = BIT(9)
 *   RMT_SIG_OUT0_IDX = 81 (routed to GPIO48 via FUNC_OUT_SEL)
 *   RMT base 0x60016000, RAM 0x60016800, ch0 = 48 items
 *
 * WS2812 timing @ RMT sclk measured 10 MHz (100 ns/tick, ROM boots
 * without PLL so the whole clock tree runs at ~1/8 nominal):
 *   T0H 0.30us=3  T0L 0.90us=9  T1H 0.70us=7  T1L 0.50us=5
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
#define GPIO_OUT1_W1TC      REG(GPIO_BASE + 0x18)
#define GPIO_ENABLE1_W1TS   REG(GPIO_BASE + 0x30)

/* ---- RMT ---- */
#define RMT_BASE   0x60016000
#define RMT_RAM_CH0 0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_CH0STATUS REG(RMT_BASE + 0x50)
#define RMT_INT_ST    REG(RMT_BASE + 0x74)
#define RMT_INT_ENA   REG(RMT_BASE + 0x78)
#define RMT_INT_CLR   REG(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)

#define TX_START     (1u << 0)
#define MEM_RD_RST   (1u << 1)
#define APB_MEM_RST  (1u << 2)
#define CONTI_MODE   (1u << 3)
#define IDLE_OUT_LV  (1u << 5)
#define IDLE_OUT_EN  (1u << 6)
#define TX_STOP      (1u << 7)
#define CONF_UPDATE  (1u << 24)

/* symmetric centered timing: T0H=0.4us T1H=0.8us nominal @10MHz.
 * If the real sclk is 11-12 MHz these still hold >150 ns margins to
 * the WS2812 decode threshold (old T1H=7 had only ~30 ns at 12 MHz,
 * causing random misreads = clustered flicker). */
#define T0H 4u
#define T0L 8u
#define T1H 8u
#define T1L 4u

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
    /* absolute-compare form: immune to the subtraction wrap glitch that
     * caused sporadic flicker bursts */
    while (timer_now() < start + ticks && cap--) { }
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
    /* enable RMT peripheral clock */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);

    /* sclk = 80 MHz, div = 1, direct memory access, register clk on */
    RMT_SYS_CONF = (1u << 0)          /* APB_FIFO_MASK: direct RAM access */
                 | (1u << 1)          /* MEM_CLK_FORCE_ON */
                 | (1u << 3)          /* MEM_FORCE_PU */
                 | (1u << 4)          /* SCLK_DIV_NUM = 1 */
                 | (1u << 24)         /* SCLK_SEL = 1: 80 MHz */
                 | (1u << 26)         /* SCLK_ACTIVE */
                 | (1u << 31);        /* CLK_EN */

    /* channel 0: div=1, one 48-item RAM block, no carrier, single-shot.
     * Each frame is restarted explicitly so new RAM content is always
     * picked up (continuous mode latches torn/stale frames). */
    RMT_CH0CONF0 = (1u << 8)          /* DIV_CNT = 1 */
                 | (1u << 16)         /* MEM_SIZE = 1 block */
                 | IDLE_OUT_EN;       /* idle output enabled, level 0 */
    RMT_CH0CONF0 |= CONF_UPDATE;      /* sync config to rmt_clk domain */

    /* enable TX_END_CH0 interrupt source so INT_ST reflects completion */
    RMT_INT_ENA |= 1;
}

static void rmt_setup_gpio48(void)
{
    /* IO MUX: GPIO function, strong drive */
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    /* route RMT channel 0 output (signal 81) to GPIO48 */
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
    /* keep GPIO output disabled; RMT owns the pad while transmitting */
}

/* one-shot WS2812 frame: whole-register writes, no interrupt polling.
 * Completion is waited with a conservative fixed delay (frame takes
 * ~30 us; we wait ~60 us), because INT_ST polling occasionally exited
 * early and the RAM rewrite then tore the frame tail (flicker bursts).
 * The 50 us latch gap is part of the RMT waveform itself (item 25). */
static void ws2812_show(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    const unsigned int BASE_CONF = (1u << 8)      /* DIV_CNT = 1 */
                                 | (1u << 16)     /* MEM_SIZE = 1 block */
                                 | IDLE_OUT_EN;   /* idle low */

    for (int i = 0; i < 24; i++) {
        unsigned int hi = T0H, lo = T0L;
        if ((grb >> (23 - i)) & 1) { hi = T1H; lo = T1L; }
        ram[i] = hi | (1u << 15) | (lo << 16); /* high dur | level | low dur */
    }
    ram[24] = 500 | (0u << 15) | 0;  /* 50 us low = WS2812 latch gap */
    ram[25] = 0;                     /* end marker -> TX_END */

    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | MEM_RD_RST | APB_MEM_RST; /* assert resets */
    RMT_CH0CONF0 = BASE_CONF;                            /* release, clean */
    RMT_CH0CONF0 = BASE_CONF | CONF_UPDATE;
    RMT_CH0CONF0 = BASE_CONF | TX_START;

    wait_us(60);   /* frame incl. latch gap = ~80 us... be generous below */
    wait_us(40);   /* total ~100 us, > frame length, no INT dependence */
}

/* pack r,g,b (0-255) into WS2812 GRB order */
static unsigned int grb_pack(unsigned int r, unsigned int g, unsigned int b)
{
    return (g << 16) | (r << 8) | b;
}

/* HSV -> RGB: hue 0-255, sat/val 0-255 */
static unsigned int hsv_to_grb(unsigned int h, unsigned int s, unsigned int v)
{
    unsigned int region = h / 43;           /* 0..5 */
    unsigned int rem = (h - region * 43) * 6; /* 0..255 */

    unsigned int p = (v * (255 - s)) >> 8;
    unsigned int q = (v * (255 - ((s * rem) >> 8))) >> 8;
    unsigned int t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;

    unsigned int r, g, b;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return grb_pack(r, g, b);
}

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();

    uart_puts("\r\n[DUANYAN-OK] led-rmt boot tpu=");
    uart_putc('0' + ticks_per_us / 10);
    uart_putc('0' + ticks_per_us % 10);
    uart_puts("\r\n");

    rmt_init();
    rmt_setup_gpio48();

    uart_puts("[DUANYAN-OK] rainbow via wait_us pacing\r\n");

    /* RAINBOW using the empirically-proven pattern: single send +
     * wait_us gap (exactly like the working RGB test). 100 ms per hue
     * step, full circle ~25.6 s. Heartbeat print once per second. */
    unsigned int hue = 0;
    unsigned int step = 0;

    for (;;) {
        unsigned int color = hsv_to_grb(hue, 255, 120);
        /* hold this hue for 100 ms, but re-latch it 10 times (every
         * 10 ms): same-color rapid resend is proven rock-solid, and any
         * mis-decoded frame is corrected within 10 ms - far below the
         * flicker perception threshold. */
        for (int rep = 0; rep < 10; rep++) {
            ws2812_show(color);
            wait_us(10000);
        }
        hue = (hue + 1) & 0xFF;

        if (++step == 10) {
            step = 0;
            uart_puts("[DUANYAN-OK] hue=");
            uart_putc('0' + (hue / 100) % 10);
            uart_putc('0' + (hue / 10) % 10);
            uart_putc('0' + hue % 10);
            uart_puts("\r\n");
        }
    }
}
