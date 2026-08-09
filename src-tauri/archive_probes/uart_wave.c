/* uart_wave.c - 波形探针: TX 发送期间高速采样 GPIO4 电平
 * 若发送时 pad 上出现 Low 采样 → TX 信号确实到达 pad
 * 若全程 High → 矩阵输出没驱动到 pad (OE/矩阵通路问题)
 * 若全程 Low → pad 被拉死
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

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_IN_READ       GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TS   GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_OUT_W1TS      GPIO_REG(GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC      GPIO_REG(GPIO_BASE + 0x0C)
#define GPIO_FUNC_OUT_SEL(pin) GPIO_REG(GPIO_BASE + 0x554 + (pin) * 4)
#define GPIO_FUNC_IN_SEL(sig)  GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
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
    char buf[10]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + v % 10; v /= 10; }
    while (n) uart_putc(buf[--n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
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

    delay_ms(5000);
    uart_puts("\r\n[WAVE] GPIO4 TX waveform probe v2 (直驱二分)\r\n");

    /* === 阶段 0: 纯直驱测试 (UART/矩阵未介入) === */
    GPIO_REG(IOMUX_BASE + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 128;         /* 直连 GPIO_OUT 寄存器 */
    GPIO_OUT_W1TS = (1u << 4);          /* OUT = 1 */
    GPIO_ENABLE_W1TS = (1u << 4);       /* OE = 1 */
    delay_ms(2);
    uart_puts("[WAVE] direct drive High -> pin4=");
    uart_putc('0' + ((GPIO_IN_READ >> 4) & 1u));
    GPIO_OUT_W1TC = (1u << 4);          /* OUT = 0 */
    delay_ms(2);
    uart_puts(" Low -> pin4=");
    uart_putc('0' + ((GPIO_IN_READ >> 4) & 1u));
    GPIO_ENABLE_W1TC = (1u << 4);       /* OE = 0 高阻 */
    delay_ms(2);
    uart_puts(" HiZ -> pin4=");
    uart_putc('0' + ((GPIO_IN_READ >> 4) & 1u));
    uart_puts("\r\n");

    /* === 阶段 1: 矩阵输出 U1TXD, 不启用 UART, 看 pad 电平 === */
    /* === 阶段 2: UART1 初始化 (与 uart_loop v5 一致) === */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    GPIO_REG(IOMUX_BASE + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6);
    delay_ms(10);
    uart_puts("[WAVE] matrix U1TXD (UART idle) -> pin4=");
    uart_putc('0' + ((GPIO_IN_READ >> 4) & 1u));
    uart_puts("\r\n");

    for (int round = 0; round < 3; round++) {
        /* 空闲电平 */
        unsigned int idle = (GPIO_IN_READ >> 4) & 1u;
        uart_puts("[WAVE] idle pin4=");
        uart_putc('0' + idle);

        /* 塞满 TX FIFO (128 字节 0x55 = 交替波形), 发送时长 ~11ms */
        for (int i = 0; i < 127; i++) UART1_FIFO = 0x55;

        /* 发送期间高速采样: 数 Low 样本 */
        unsigned int lo = 0, total = 0;
        for (unsigned int i = 0; i < 200000u; i++) {
            unsigned int v = GPIO_IN_READ;
            if (!(v & (1u << 4))) lo++;
            total++;
            /* FIFO 将空时补数据, 保持连续发送 */
            if (((UART1_STATUS >> 16) & 0x3FFu) < 64 && i < 150000u)
                UART1_FIFO = 0x55;
        }
        uart_puts(" txwindow low=");
        put_dec(lo);
        uart_puts("/");
        put_dec(total);
        unsigned int rxc = UART1_STATUS & 0x3FFu;
        uart_puts(" rxfifo=");
        put_dec(rxc);
        uart_puts("\r\n");
        /* 读空 RX */
        for (unsigned int i = 0; i < 128; i++) {
            if ((UART1_STATUS & 0x3FFu) == 0) break;
            (void)UART1_FIFO;
        }
        /* 等 TX 发完 */
        delay_ms(50);
        delay_ms(800);
    }
    uart_puts("[WAVE] done\r\n");
    for (;;) { delay_ms(1000); }
}
