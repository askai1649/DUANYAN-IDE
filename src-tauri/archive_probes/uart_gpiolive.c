/* uart_gpiolive.c - GPIO_IN 活体总检 + 备用输入候选验证
 * 背景: ILB 判决——UART1 RX 解码器活 (内部环回 MATCH), 死段 = 矩阵输入→RXD
 *       (sig9..23 全挂 CONST0 都拉不动 RXD, FPGA 布线缺陷)。
 * 救援路线: ①找活着的 GPIO 输入脚, 软件位啜 RX (TX 正常, 可成半双工主链路)
 *           ②试其它矩阵输入 (U1CTS sig16 / U0CTS sig9) 是否也断
 * 方法: 每个脚 IOMUX FUN_IE + 上拉/下拉切换, 读 GPIO_IN 是否跟随。
 *       跟随 → 输入级活。不驱动 pad, 纯内部上下拉判别。
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
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define IOMUX_PIN(n)       R(0x60009044u + (n) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u

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

/* 单脚活体检测: FUN_IE + FUN_WPU/FUN_WPD 切换, 读 GPIO_IN 跟随否 */
static void probe_pin(unsigned int pin)
{
    unsigned int iomux = IOMUX_PIN(pin);
    IOMUX_PIN(pin) = (iomux & ~(0x7u << 12)) | (1u << 9) | (1u << 7); /* MCU_SEL=GPIO, FUN_IE, WPU */
    delay_ms(2);
    unsigned int hi = (GPIO_IN >> pin) & 1u;
    IOMUX_PIN(pin) = (iomux & ~(0x7u << 12)) | (1u << 9) | (1u << 8); /* WPD */
    delay_ms(2);
    unsigned int lo = (GPIO_IN >> pin) & 1u;
    IOMUX_PIN(pin) = iomux;   /* 还原 */
    uart_puts("[LIVE] pin"); put_dec(pin);
    uart_puts(" pu="); uart_putc('0' + hi);
    uart_puts(" pd="); uart_putc('0' + lo);
    if (hi == 1 && lo == 0) uart_puts("  <<< LIVE");
    else uart_puts("  dead/stuck");
    uart_puts("\r\n");
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
    uart_puts("\r\n[LIVE] GPIO_IN 活体总检\r\n");
    uart_puts("[LIVE] gpio_in raw="); put_hex(GPIO_IN); uart_puts("\r\n");

    /* 主链路相关脚 + 候选救援脚 (避开 strapping/特殊脚, 分段连续扫) */
    for (unsigned int p = 2; p <= 21; p++) {
        GPIO_ENABLE_W1TC = (1u << p);
        probe_pin(p);
    }
    for (unsigned int p = 38; p <= 48; p++) {
        GPIO_ENABLE_W1TC = (1u << p);
        probe_pin(p);
    }

    /* 备用矩阵输入判决: U1CTS(sig16)/U0CTS(sig9) 挂常量看 STATUS 位是否跟
     * STATUS: CTSN=bit14 */
    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    GPIO_FUNC_IN_SEL(16) = CONST0 | (1u << 6);   /* U1CTS ← 常量0 */
    delay_ms(2);
    unsigned int c0 = (U1(0x1C) >> 14) & 1u;
    GPIO_FUNC_IN_SEL(16) = CONST1 | (1u << 6);
    delay_ms(2);
    unsigned int c1 = (U1(0x1C) >> 14) & 1u;
    uart_puts("[LIVE] u1cts c0="); uart_putc('0' + c0);
    uart_puts(" c1="); uart_putc('0' + c1);
    uart_puts((c0 == 0 && c1 == 1) ? "  <<< CTS-LIVE" : "  cts-dead");
    uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(16) = CONST1 | (1u << 6);

    uart_puts("[LIVE] done\r\n");
    for (;;) { delay_ms(1000); }
}
