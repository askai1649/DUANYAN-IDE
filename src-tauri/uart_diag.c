/* uart_diag.c - UART1 RX 通路逐级定位探针
 * S1: 寄存器读回 (CLKDIV/CONF0/IOMUX/OUT_SEL/IN_SEL)
 * S2: TX FIFO 排空测试 (写5字节后看 TXFIFO_CNT 是否排空 → 移位器+时钟)
 * S3: GPIO4/5 pad 电平 (GPIO_IN 0x3C) → 物理引脚状态
 * S4: 环回收包 → RXFIFO_CNT
 */
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)
#define UART0_CONF0  (*(volatile unsigned int *)0x60000020)
#define UART0_CONF1  (*(volatile unsigned int *)0x60000024)
#define UART0_CLKDIV (*(volatile unsigned int *)0x60000014)
#define UART0_CLK_CONF (*(volatile unsigned int *)0x60000078)
#define UART0_MEM_CONF (*(volatile unsigned int *)0x60000060)

#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

#define UART1_FIFO   (*(volatile unsigned int *)0x60010000)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define UART1_MEM_CONF (*(volatile unsigned int *)0x60010060)
#define UART1_MEM_TX_ST (*(volatile unsigned int *)0x60010064)
#define UART1_MEM_RX_ST (*(volatile unsigned int *)0x60010068)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_IN_REG        GPIO_REG(GPIO_BASE + 0x3C)
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

    delay_ms(4000);
    uart_puts("\r\n[DIAG] UART1 RX path probe v5 - U0 vs U1\r\n");
    uart_puts("[U0] conf0="); put_hex(UART0_CONF0);
    uart_puts(" conf1="); put_hex(UART0_CONF1);
    uart_puts(" clkdiv="); put_hex(UART0_CLKDIV);
    uart_puts(" clkconf="); put_hex(UART0_CLK_CONF);
    uart_puts(" memconf="); put_hex(UART0_MEM_CONF);
    uart_puts(" status="); put_hex(UART0_STATUS); uart_puts("\r\n");
    uart_puts("[U1] conf0="); put_hex(UART1_CONF0);
    uart_puts(" clkdiv="); put_hex(UART1_CLKDIV);
    uart_puts(" clkconf="); put_hex(UART1_CLK_CONF);
    uart_puts(" memconf="); put_hex(UART1_MEM_CONF);
    uart_puts(" status="); put_hex(UART1_STATUS); uart_puts("\r\n");

    /* 初始化 */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    uart_puts("[DIAG] rst_en0="); put_hex(*(volatile unsigned int *)0x600C0020);
    uart_puts(" clk_en0="); put_hex(SYSTEM_PERIP_CLK_EN0); uart_puts("\r\n");
    UART1_CLKDIV = (40000000u / 115200u);   /* 分频347, FRAG=0 */
    /* CLK_CONF: DIV_NUM=0(不再分频) + SEL=3(XTAL) + SCLK_EN + TX/RX_SCLK_EN */
    uart_puts("[DIAG] clkconf_before="); put_hex(UART1_CLK_CONF); uart_puts("\r\n");
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    uart_puts("[DIAG] clkconf_after="); put_hex(UART1_CLK_CONF); uart_puts("\r\n");
    /* RST_CORE 脉冲: 写1再写0, 复位 TX/RX 内核 */
    UART1_CLK_CONF |= (1u << 23);
    UART1_CLK_CONF &= ~(1u << 23);
    UART1_MEM_CONF |= (1u << 28);       /* MEM_FORCE_PU */
    UART1_CONF0 = (3u << 12) | (1u << 17) | (1u << 18);
    delay_ms(1);
    UART1_CONF0 = (3u << 12);
    GPIO_REG(IOMUX_BASE + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_FUNC_IN_SEL(15) = 4 | (1u << 6);
    delay_ms(10);

    uart_puts("[S1] clkdiv="); put_hex(UART1_CLKDIV);
    uart_puts(" conf0="); put_hex(UART1_CONF0); uart_puts("\r\n");
    uart_puts("[S1] iomux4="); put_hex(GPIO_REG(IOMUX_BASE + 4 * 4));
    uart_puts(" outsel4="); put_hex(GPIO_FUNC_OUT_SEL(4));
    uart_puts(" insel15="); put_hex(GPIO_FUNC_IN_SEL(15)); uart_puts("\r\n");
    uart_puts("[S1] status="); put_hex(UART1_STATUS); uart_puts("\r\n");

    /* S2: TX 排空测试 */
    for (int i = 0; i < 128; i++) {     /* 先清 RX */
        if ((UART1_STATUS & 0x3FFu) == 0) break;
        (void)UART1_FIFO;
    }
    for (int i = 0; i < 5; i++) UART1_FIFO = (unsigned int)"HELLO"[i];
    uart_puts("[S2] txfifo+0="); put_hex((UART1_STATUS >> 16) & 0x3FFu);
    uart_puts(" memtx="); put_hex(UART1_MEM_TX_ST); uart_puts("\r\n");
    delay_ms(50);
    uart_puts("[S2] txfifo+50ms="); put_hex((UART1_STATUS >> 16) & 0x3FFu); uart_puts("\r\n");
    delay_ms(200);
    uart_puts("[S2] txfifo+250ms="); put_hex((UART1_STATUS >> 16) & 0x3FFu); uart_puts("\r\n");

    /* S3: pad 电平 */
    unsigned int gi = GPIO_IN_REG;
    uart_puts("[S3] gpio4="); uart_putc('0' + ((gi >> 4) & 1));
    uart_puts(" gpio5="); uart_putc('0' + ((gi >> 5) & 1)); uart_puts("\r\n");

    /* S4: 收包 */
    delay_ms(200);
    uart_puts("[S4] rxfifo="); put_hex(UART1_STATUS & 0x3FFu);
    uart_puts(" data=");
    unsigned int cnt = UART1_STATUS & 0x3FFu;
    if (cnt > 8) cnt = 8;
    for (unsigned int i = 0; i < cnt; i++) uart_putc((char)(UART1_FIFO & 0xFF));
    uart_puts("\r\n[DIAG] done\r\n");
    for (;;) { delay_ms(1000); }
}
