/* uart_link.c - Mode-B UART1 link firmware (ESP32-S3 side)
 *
 * Physical link: Zero 2W GPIO15(TX) -> S3 GPIO5(RX)
 *                Zero 2W GPIO14(RX) <- S3 GPIO4(TX)   3.3V TTL, GND shared
 * UART0 stays on the USB-UART bridge for debug/flash.
 *
 * Protocol v1 frame:  0xAA 0x55 LEN CMD PAYLOAD[LEN] CRC8
 *   CMD 0x01 S3->Pi  heartbeat (payload: uptime seconds u32 LE)
 *   CMD 0x10 Pi->S3  set LED color (payload: R G B) -> single WS2812 frame
 *   CMD 0x12 Pi->S3  query status  -> ACK frame CMD 0x92 (uptime, fw ver)
 * Every valid frame is ACKed; unknown CMD -> NACK 0x7F.
 * All traffic is mirrored to UART0 for debugging.
 */

#define UART0_FIFO    (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS  (*(volatile unsigned int *)0x6000001C)
#define UART0_CLKDIV  (*(volatile unsigned int *)0x60000014)

/* ---- UART1 (base 0x60010000) ---- */
#define UART1_BASE    0x60010000
#define UART1_FIFO    (*(volatile unsigned int *)(UART1_BASE + 0x00))
#define UART1_CLKDIV  (*(volatile unsigned int *)(UART1_BASE + 0x14))
#define UART1_STATUS  (*(volatile unsigned int *)(UART1_BASE + 0x1C))
#define UART1_CONF0   (*(volatile unsigned int *)(UART1_BASE + 0x20))

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

/* ---- RMT / WS2812 (proven values from led_rmt.c) ---- */
#define RMT_BASE   0x60016000
#define RMT_RAM_CH0 0x60016800
#define RMT_CH0CONF0  REG(RMT_BASE + 0x20)
#define RMT_INT_CLR   REG(RMT_BASE + 0x7C)
#define RMT_SYS_CONF  REG(RMT_BASE + 0xC0)

#define IDLE_OUT_EN  (1u << 6)
#define CONF_UPDATE  (1u << 24)
#define T0H 4u
#define T0L 8u
#define T1H 8u
#define T1L 4u

/* ---------------- UART0 (debug) ---------------- */

static void uart0_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) { }
    UART0_FIFO = (unsigned int)c;
}

static void uart0_puts(const char *s)
{
    while (*s) uart0_putc(*s++);
}

static void uart0_hex2(unsigned int v)
{
    uart0_putc("0123456789ABCDEF"[(v >> 4) & 0xF]);
    uart0_putc("0123456789ABCDEF"[v & 0xF]);
}

static void uart0_u32(unsigned int v)
{
    for (int sh = 28; sh >= 0; sh -= 4)
        uart0_putc("0123456789ABCDEF"[(v >> sh) & 0xF]);
}

/* ---------------- timebase ---------------- */

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
    for (int i = 0; i < 10; i++) uart0_putc('~');
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

/* ---------------- UART1 (link to Zero 2W) ---------------- */

static void uart1_init(void)
{
    /* enable UART1 peripheral clock (bit 5, default already 1) */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);

    /* copy UART0's clkdiv: ROM set it for the real UART source clock,
     * so UART1 gets exactly 115200 regardless of the 1/8 clock tree */
    UART1_CLKDIV = UART0_CLKDIV;

    /* 8N1, no flow control (CONF0 reset value is fine, write explicitly) */
    UART1_CONF0 = 0x0000001C;   /* bit[1:0]=0 8bit, parity off, 1 stop */

    /* TX: GPIO4 via GPIO matrix, signal 15 = U1TXD */
    REG(IOMUX_BASE + 4 * 4) = (1u << 12) | (3u << 10);     /* pad: GPIO func */
    REG(GPIO_BASE + 0x554 + 4 * 4) = 15;                    /* out sel = U1TXD */

    /* RX: GPIO5, enable pad input, then route signal 15 = U1RXD into GPIO5 */
    REG(IOMUX_BASE + 5 * 4) = (1u << 12) | (3u << 10) | (1u << 3); /* FUN_IE */
    REG(GPIO_BASE + 0x130 + 5 * 4) = 15;                    /* in sel = U1RXD */
}

static void uart1_putc(unsigned char c)
{
    while (((UART1_STATUS >> 16) & 0x3FF) >= 127) { }
    UART1_FIFO = (unsigned int)c;
}

/* returns -1 if no byte waiting */
static int uart1_getc(void)
{
    if ((UART1_STATUS & 0x3FF) == 0) return -1;
    return (int)(UART1_FIFO & 0xFF);
}

/* ---------------- protocol ---------------- */

static unsigned char crc8(const unsigned char *p, unsigned int n)
{
    unsigned char crc = 0;
    for (unsigned int i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (unsigned char)((crc << 1) ^ 0x07)
                               : (unsigned char)(crc << 1);
    }
    return crc;
}

static void send_frame(unsigned char cmd, const unsigned char *pl, unsigned int len)
{
    unsigned char buf[70];
    buf[0] = 0xAA; buf[1] = 0x55; buf[2] = (unsigned char)len; buf[3] = cmd;
    for (unsigned int i = 0; i < len; i++) buf[4 + i] = pl[i];
    buf[4 + len] = crc8(buf + 2, 2 + len);
    for (unsigned int i = 0; i < 5 + len; i++) uart1_putc(buf[i]);
}

/* ---------------- WS2812 (single frame, proven) ---------------- */

static void ws2812_show(unsigned int grb)
{
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    const unsigned int BASE_CONF = (1u << 8) | (1u << 16) | IDLE_OUT_EN;

    for (int i = 0; i < 24; i++) {
        unsigned int hi = T0H, lo = T0L;
        if ((grb >> (23 - i)) & 1) { hi = T1H; lo = T1L; }
        ram[i] = hi | (1u << 15) | (lo << 16);
    }
    ram[24] = 500 | (0u << 15) | 0;
    ram[25] = 0;

    RMT_INT_CLR = 1;
    RMT_CH0CONF0 = BASE_CONF | (1u << 1) | (1u << 2);
    RMT_CH0CONF0 = BASE_CONF;
    RMT_CH0CONF0 = BASE_CONF | CONF_UPDATE;
    RMT_CH0CONF0 = BASE_CONF | (1u << 0);
    wait_us(60);
    wait_us(40);
}

static void rmt_init(void)
{
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    RMT_SYS_CONF = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4)
                 | (1u << 24) | (1u << 26) | (1u << 31);
    RMT_CH0CONF0 = (1u << 8) | (1u << 16) | IDLE_OUT_EN;
    RMT_CH0CONF0 |= CONF_UPDATE;

    /* route RMT ch0 (signal 81) to GPIO48 */
    REG(IOMUX_BASE + 48 * 4) = (1u << 12) | (3u << 10);
    REG(GPIO_BASE + 0x554 + 48 * 4) = 81;
}

/* ---------------- main ---------------- */

void _start(void) __attribute__((section(".entry"), used));

void _start(void)
{
    disable_watchdogs();
    TIMG0_T0CONFIG = (1u << 13) | (1u << 30) | (1u << 31);
    calibrate();

    uart0_puts("\r\n[DUANYAN-OK] uart-link boot tpu=");
    uart0_putc('0' + ticks_per_us / 10);
    uart0_putc('0' + ticks_per_us % 10);
    uart0_puts("\r\n");

    uart1_init();
    rmt_init();
    ws2812_show(0x000020);   /* dim blue: link firmware alive */

    uart0_puts("[DUANYAN-OK] UART1 link up, waiting for Zero 2W\r\n");

    unsigned int boot_tick = timer_now();
    unsigned int next_beat = boot_tick + ticks_per_us * 1000000; /* 1 s */

    /* rx state machine */
    unsigned char rx[70];
    unsigned int rn = 0;
    unsigned int sync = 0;

    for (;;) {
        /* ---- heartbeat every 1 s ---- */
        if ((int)(timer_now() - next_beat) >= 0) {
            next_beat += ticks_per_us * 1000000;
            unsigned int up = (timer_now() - boot_tick) / (ticks_per_us * 1000000);
            unsigned char pl[4];
            pl[0] = (unsigned char)(up & 0xFF);
            pl[1] = (unsigned char)((up >> 8) & 0xFF);
            pl[2] = (unsigned char)((up >> 16) & 0xFF);
            pl[3] = (unsigned char)((up >> 24) & 0xFF);
            send_frame(0x01, pl, 4);
            uart0_puts("[DUANYAN-OK] beat up=");
            uart0_u32(up);
            uart0_puts("\r\n");
        }

        /* ---- rx parsing ---- */
        int b = uart1_getc();
        if (b < 0) continue;
        unsigned char c = (unsigned char)b;

        if (sync == 0) {
            if (c == 0xAA) { rx[0] = c; sync = 1; }
            continue;
        }
        if (sync == 1) {
            if (c == 0x55) { rx[1] = c; sync = 2; }
            else sync = 0;
            continue;
        }
        rx[rn + 2] = c;   /* fill from LEN onward */
        rn++;
        if (rn == 1) {
            if (c > 64) sync = 0;              /* LEN sanity */
            continue;
        }
        unsigned int len = rx[2];
        if (rn >= 3u + len + 1u) {             /* got CMD+PAYLOAD+CRC */
            unsigned char calc = crc8(rx + 2, 2 + len);
            unsigned int crc_pos = 3 + len;
            if (calc == rx[crc_pos]) {
                unsigned char cmd = rx[3];
                uart0_puts("[DUANYAN-OK] rx cmd=0x");
                uart0_hex2(cmd);
                uart0_puts(" len=");
                uart0_hex2(len);
                uart0_puts("\r\n");

                if (cmd == 0x10 && len == 3) {
                    /* set LED: payload R G B -> GRB */
                    unsigned int grb = ((unsigned int)rx[5] << 16)
                                     | ((unsigned int)rx[4] << 8)
                                     | (unsigned int)rx[6];
                    ws2812_show(grb);
                    ws2812_show(grb);
                    send_frame(0x90, 0, 0);     /* ACK */
                } else if (cmd == 0x12) {
                    unsigned int up = (timer_now() - boot_tick)
                                    / (ticks_per_us * 1000000);
                    unsigned char pl[5];
                    pl[0] = (unsigned char)(up & 0xFF);
                    pl[1] = (unsigned char)((up >> 8) & 0xFF);
                    pl[2] = (unsigned char)((up >> 16) & 0xFF);
                    pl[3] = (unsigned char)((up >> 24) & 0xFF);
                    pl[4] = 1;                  /* fw version 1 */
                    send_frame(0x92, pl, 5);    /* status ACK */
                } else {
                    send_frame(0x7F, &cmd, 1);  /* NACK unknown */
                }
            } else {
                uart0_puts("[DUANYAN-OK] rx CRC bad\r\n");
            }
            sync = 0; rn = 0;
        }
    }
}
