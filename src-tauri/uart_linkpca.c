/* uart_linkpca.c - L1.5 PCA9685 bring-up probe
 * I2C bit-bang SDA=GPIO8 SCL=GPIO9 (quasi-OD, board pull-ups)
 * Boot: scan -> POR regs -> 50Hz prescale rb -> CH0 90deg -> wiggle 95/85/90
 * PCA9685 POR outputs no pulse; servos safe before sequence
 * UART0 diag only; throwaway bring-up fw (reflash l1 after pass)
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)
#define R(a) (*(volatile unsigned int *)(a))
#define GPIO_BASE  0x60004000u
#define GPIO_OUT_W1TS R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)
#define SDA 8u
#define SCL 9u
#define PCA_ADDR 0x40u
#define PCA_MODE1 0x00u
#define PCA_MODE2 0x01u
#define PCA_CH0_ON_L 0x06u
#define PCA_PRESCALE 0xFEu
static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_dec(unsigned int v)
{
    char b[12]; int n = 0;
    if (v == 0) { uart_putc(48); return; }
    while (v) { b[n++] = (char)(48 + v % 10); v /= 10; }
    while (n--) uart_putc(b[n]);
}
static void put_hex2(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    uart_putc(d[(v >> 4) & 15]); uart_putc(d[v & 15]);
}
static void delay_ms(unsigned int ms)
{ volatile unsigned int n = ms * 16000u; while (n--) {} }
static void delay_us(unsigned int us)
{ volatile unsigned int n = us * 16u; while (n--) {} }
static void i2c_d(void) { delay_us(5); }
static void sda_lo(void) { GPIO_OUT_W1TC = (1u << SDA); GPIO_ENABLE_W1TS = (1u << SDA); }
static void sda_hi(void) { GPIO_ENABLE_W1TC = (1u << SDA); }
static void scl_lo(void) { GPIO_OUT_W1TC = (1u << SCL); GPIO_ENABLE_W1TS = (1u << SCL); }
static void scl_hi(void) { GPIO_ENABLE_W1TC = (1u << SCL); }
static unsigned int sda_rd(void) { return (R(GPIO_BASE + 0x3C) >> SDA) & 1u; }
static void i2c_init(void)
{
    IOMUX_PIN(SDA) = (1u << 12) | (3u << 10) | (1u << 9);
    IOMUX_PIN(SCL) = (1u << 12) | (3u << 10) | (1u << 9);
    GPIO_OUT_W1TS = (1u << SDA) | (1u << SCL);
    GPIO_ENABLE_W1TC = (1u << SDA) | (1u << SCL);
}
static void i2c_start(void)
{ sda_hi(); i2c_d(); scl_hi(); i2c_d(); sda_lo(); i2c_d(); scl_lo(); i2c_d(); }
static void i2c_stop(void)
{ sda_lo(); i2c_d(); scl_hi(); i2c_d(); sda_hi(); i2c_d(); }
static unsigned int i2c_wbyte(unsigned int v)
{
    for (int b = 7; b >= 0; b--) {
        if ((v >> b) & 1u) sda_hi(); else sda_lo();
        i2c_d(); scl_hi(); i2c_d(); scl_lo(); i2c_d();
    }
    sda_hi(); i2c_d(); scl_hi(); i2c_d();
    unsigned int ack = sda_rd() ? 0u : 1u;
    scl_lo(); i2c_d();
    return ack;
}
static unsigned int i2c_rbyte(unsigned int ack)
{
    unsigned int v = 0;
    sda_hi();
    for (int b = 7; b >= 0; b--) {
        scl_hi(); i2c_d();
        if (sda_rd()) v |= (1u << b);
        scl_lo(); i2c_d();
    }
    if (ack) sda_lo(); else sda_hi();
    i2c_d(); scl_hi(); i2c_d(); scl_lo(); i2c_d();
    sda_hi();
    return v;
}
static unsigned int pca_w(unsigned int reg, unsigned int val)
{
    i2c_start();
    unsigned int ok = i2c_wbyte(PCA_ADDR << 1);
    ok &= i2c_wbyte(reg);
    ok &= i2c_wbyte(val);
    i2c_stop();
    return ok;
}
static unsigned int pca_r(unsigned int reg)
{
    i2c_start();
    unsigned int ok = i2c_wbyte(PCA_ADDR << 1);
    ok &= i2c_wbyte(reg);
    i2c_start();
    ok &= i2c_wbyte((PCA_ADDR << 1) | 1u);
    unsigned int v = i2c_rbyte(0);
    i2c_stop();
    return ok ? v : 0xFFFFFFFFu;
}
static unsigned int ang_to_off(unsigned int ang)
{
    if (ang > 1800u) ang = 1800u;
    unsigned int px10 = 10000u + (ang * 50u) / 9u;
    return (px10 * 512u) / 25000u;
}
static unsigned int pca_set(unsigned int ch, unsigned int ang)
{
    if (ch > 15u) return 0;
    unsigned int off = ang_to_off(ang);
    unsigned int base = PCA_CH0_ON_L + 4u * ch;
    unsigned int ok = pca_w(base, 0x00u);
    ok &= pca_w(base + 1u, 0x00u);
    ok &= pca_w(base + 2u, off & 0xFFu);
    ok &= pca_w(base + 3u, (off >> 8) & 0xFFu);
    return ok;
}
static unsigned int uart_getc(void)
{
    if ((UART0_STATUS & 0x3FFu) == 0) return 0xFFFFFFFFu;
    return UART0_FIFO & 0xFFu;
}
static void pca_scan(void)
{
    uart_puts("[PCA] scan:");
    unsigned int hits = 0;
    for (unsigned int a = 0x08u; a < 0x78u; a++) {
        i2c_start();
        if (i2c_wbyte(a << 1)) { uart_putc(32); put_hex2(a); hits++; }
        i2c_stop();
    }
    if (!hits) uart_puts(" NONE");
    uart_puts("\r\n");
}
static void pca_regs(void)
{
    unsigned int m1 = pca_r(PCA_MODE1);
    unsigned int m2 = pca_r(PCA_MODE2);
    unsigned int pre = pca_r(PCA_PRESCALE);
    uart_puts("[PCA] regs M1="); put_hex2(m1);
    uart_puts(" M2="); put_hex2(m2);
    uart_puts(" PRE="); put_hex2(pre); uart_puts("\r\n");
}
static void pca_off(unsigned int ch)
{
    unsigned int base = PCA_CH0_ON_L + 4u * ch;
    pca_w(base, 0x00u); pca_w(base + 1u, 0x00u);
    pca_w(base + 2u, 0x00u); pca_w(base + 3u, 0x10u);
}
static void dump_ch(void)
{
    for (unsigned int ch = 0; ch < 6u; ch++) {
        unsigned int base = PCA_CH0_ON_L + 4u * ch;
        uart_puts("ch"); put_dec(ch); uart_puts(" off=");
        put_dec(((pca_r(base + 3u) & 0x1Fu) << 8) | pca_r(base + 2u));
        uart_puts("\r\n");
    }
}
static void do_wiggle(unsigned int ch)
{
    uart_puts("[PCA] wig ch"); put_dec(ch); uart_puts("\r\n");
    pca_set(ch, 950); delay_ms(500);
    pca_set(ch, 850); delay_ms(500);
    pca_set(ch, 900); delay_ms(300);
    uart_puts("[PCA] done ch"); put_dec(ch); uart_puts("\r\n");
}
static void help(void)
{
    uart_puts("[PCA] cmd: s=scan r=regs d=dump 0-5=wiggle q=all900 x=alloff h=help\r\n");
}
void _start(void) __attribute__((section(".entry"), naked));
void _start(void)
{
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
    delay_ms(300);
    uart_puts("\r\n[LINKPCA] PCA9685 bring-up probe (L1.5)\r\n");
    i2c_init();
    pca_scan();
    pca_regs();
    pca_w(PCA_MODE1, 0x10u);
    pca_w(PCA_PRESCALE, 0x79u);
    pca_w(PCA_MODE1, 0x00u);
    delay_ms(5);
    pca_w(PCA_MODE1, 0xA0u);
    unsigned int pre1 = pca_r(PCA_PRESCALE);
    uart_puts("[PCA] pre set=79 rb="); put_hex2(pre1);
    uart_puts(pre1 == 0x79u ? " OK\r\n" : " FAIL\r\n");
    help();
    for (;;) {
        unsigned int c = uart_getc();
        if (c == 0xFFFFFFFFu) { delay_ms(5); continue; }
        if (c >= 48 && c <= 53) do_wiggle(c - 48);
        else if (c == 115) pca_scan();
        else if (c == 114) pca_regs();
        else if (c == 100) dump_ch();
        else if (c == 113) {
            for (unsigned int ch = 0; ch < 6u; ch++) pca_set(ch, 900);
            uart_puts("[PCA] all 900\r\n");
        } else if (c == 120) {
            for (unsigned int ch = 0; ch < 6u; ch++) pca_off(ch);
            uart_puts("[PCA] all off\r\n");
        } else if (c == 104) help();
        else uart_puts("[PCA] ?\r\n");
    }
}
