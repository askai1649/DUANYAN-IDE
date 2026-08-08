/* uart_livescan.c - GPIO_IN 活体总检 v2: 自驱跟随判别 (本板唯一可信判据)
 * 背景: ILB 判决 RX 解码器活, 死段=矩阵输入→RXD; 救援需要活的 GPIO 输入脚
 *       做软件位啜 RX。gpiolive 证明内部上下拉在本 FPGA 无效 (全脚 pu=pd),
 *       ingate 证明自驱跟随判别可信 (GPIO2 完美跟随)。
 * 方法: 每脚 FUN_IE + 输出使能, OUT 交替 0/1, 读 GPIO_IN 该位是否跟随。
 * 禁区: 26-32(flash) 43/44(控制台) 19/20(USB) 0/1/3(strap) 一律不碰。
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
#define GPIO_IN            R(GPIO_BASE + 0x3C)
#define GPIO_OUT           R(GPIO_BASE + 0x04)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_BASE 0x60009000u
#define IOMUX_PIN(p) R(IOMUX_BASE + 4 + (p) * 4)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_hex(unsigned int v)
{
    const char *h = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) uart_putc(h[(v >> (i * 4)) & 0xF]);
}
static void put_dec(unsigned int v)
{
    char buf[12];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

static void selfdrive(unsigned int pin)
{
    unsigned int iomux = IOMUX_PIN(pin);
    IOMUX_PIN(pin) = iomux | (1u << 9);        /* FUN_IE */
    GPIO_ENABLE_W1TS = (1u << pin);
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        unsigned int want = (i & 1) ? 1u : 0u;
        if (want) GPIO_OUT |= (1u << pin); else GPIO_OUT &= ~(1u << pin);
        delay_ms(2);
        if (((GPIO_IN >> pin) & 1u) != want) { ok = 0; break; }
    }
    GPIO_ENABLE_W1TC = (1u << pin);
    GPIO_OUT &= ~(1u << pin);
    IOMUX_PIN(pin) = iomux;
    uart_puts("[LIVE2] pin"); put_dec(pin);
    uart_puts(ok ? "  <<< LIVE" : "  dead");
    uart_puts("\r\n");
}

static int banned(unsigned int p)
{
    if (p == 0 || p == 1 || p == 3) return 1;      /* strap */
    if (p == 4 || p == 5) return 1;                 /* 已接 Pi 主链路, 避免外线竞争 */
    if (p == 19 || p == 20) return 1;               /* USB */
    if (p >= 22 && p <= 25) return 1;               /* 未测段, 保守 */
    if (p >= 26 && p <= 32) return 1;               /* flash/PSRAM */
    if (p == 43 || p == 44) return 1;               /* 控制台 */
    if (p == 45 || p == 46) return 1;               /* strap/VDD */
    return 0;
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

    delay_ms(1000);
    uart_puts("\r\n[LIVE2] 自驱跟随扫描\r\n");
    for (unsigned int p = 0; p <= 48; p++) {
        if (banned(p)) continue;
        selfdrive(p);
    }
    uart_puts("[LIVE2] done\r\n");
    for (;;) { delay_ms(1000); }
}
