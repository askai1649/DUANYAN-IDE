/* uart_memclk.c - RX 核心时钟诊断:
 * 1) UART1 全关键寄存器回读 (与 UART0 对照)
 * 2) RXFIFO_WR_ADDR 读两次看是否走动 → MEM_CLK/SCLK 是否在跑
 * 3) SCLK_SEL 换 PLL_F80M(1) 再试合成帧
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

#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define SYSTEM_PERIP_RST_EN0 (*(volatile unsigned int *)0x600C0020)

#define GPIO_BASE  0x60004000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_FUNC_IN_SEL(sig) GPIO_REG(GPIO_BASE + 0x154 + (sig) * 4)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
/* UART 寄存器偏移: FIFO 0, CLKDIV 0x14, STATUS 0x1C, CONF0 0x20, CONF1 0x24,
 * INT_RAW 0x28, MEM_RX_STATUS(b 内偏移 0x64)? S3: RXFIFO_WR_ADDR 在 +0x64? 用 MEM_RX_STATUS 0x64 试读 */

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
static void dump2(const char *name, unsigned int base_u0_off)
{
    uart_puts(name);
    uart_puts(" U0="); put_hex(U0(base_u0_off));
    uart_puts(" U1="); put_hex(U1(base_u0_off));
    uart_puts("\r\n");
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

    delay_ms(1000);
    uart_puts("\r\n[MEMCLK] ===== reg readback =====\r\n");
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    delay_ms(10);

    uart_puts("RST_EN0="); put_hex(SYSTEM_PERIP_RST_EN0); uart_puts("\r\n");
    dump2("FIFO(0x00)", 0x00);
    dump2("CLKDIV(0x14)", 0x14);
    dump2("STATUS(0x1C)", 0x1C);
    dump2("CONF0(0x20)", 0x20);
    dump2("CONF1(0x24)", 0x24);
    dump2("CLK_CONF(0x78)", 0x78);
    dump2("REG_0x64", 0x64);
    dump2("REG_0x68", 0x68);

    uart_puts("[MEMCLK] ===== wr-addr motion =====\r\n");
    unsigned int w1 = U1(0x64), w2 = U1(0x68);
    for (volatile int d = 0; d < 200000; d++) {}
    unsigned int w3 = U1(0x64), w4 = U1(0x68);
    uart_puts("0x64: "); put_hex(w1); uart_puts("->"); put_hex(w3);
    uart_puts("  0x68: "); put_hex(w2); uart_puts("->"); put_hex(w4); uart_puts("\r\n");

    uart_puts("[MEMCLK] ===== synth @ sclk_sel=1(PLL_F80M) =====\r\n");
    U1(0x78) = (1u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x14) = (80000000u / 115200u);   /* 若 PLL 未启则波特率飘, 但合成帧不依赖外部时钟精度 */
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(1);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    for (int i = 0; i < 130; i++) {
        if ((U1(0x1C) & 0x3FFu) == 0) break;
        (void)U1(0x00);
    }
    GPIO_FUNC_IN_SEL(15) = (1u << 5);   /* 恒高 */
    delay_ms(50);
    GPIO_FUNC_IN_SEL(15) = 0;            /* 拉低 */
    delay_ms(30);
    GPIO_FUNC_IN_SEL(15) = (1u << 5);    /* 拉高 */
    delay_ms(50);
    unsigned int c = U1(0x1C) & 0x3FFu;
    uart_puts("synth2 fifo="); put_hex(c);
    if (c > 0 && c <= 128) {
        uart_puts(" frames:");
        for (unsigned int k = 0; k < c && k < 8; k++) put_hex(U1(0x00) & 0xFFu);
        uart_puts(" << CORE-ALIVE");
    }
    uart_puts("\r\n[MEMCLK] done\r\n");
    for (;;) { delay_ms(1000); }
}
