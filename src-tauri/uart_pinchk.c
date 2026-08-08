/* uart_pinchk.c - 完好引脚对照测试: 在已知完好的 GPIO43/44 (UART0控制台脚)
 * 上做自驱自读 + RXD 探针, 若这里全通 → 4/5/16/17/18 脚数字 IO 疑似物理损坏
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
#define UART1_STATUS (*(volatile unsigned int *)0x6001001C)
#define UART1_CONF0  (*(volatile unsigned int *)0x60010020)
#define UART1_CLKDIV (*(volatile unsigned int *)0x60010014)
#define UART1_CLK_CONF (*(volatile unsigned int *)0x60010078)
#define SYSTEM_PERIP_CLK_EN0 (*(volatile unsigned int *)0x600C0018)
#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_IN0     GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_IN1     GPIO_REG(GPIO_BASE + 0x40)
#define GPIO_ENABLE_W1TS GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_OUT_W1TS    GPIO_REG(GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC    GPIO_REG(GPIO_BASE + 0x0C)
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

    delay_ms(5000);
    uart_puts("\r\n[PINCHK] known-good pin self-test v1\r\n");

    uart_puts("[PINCHK] IN0="); put_hex(GPIO_IN0);
    uart_puts(" IN1="); put_hex(GPIO_IN1);
    uart_puts(" (bit43="); uart_putc('0' + ((GPIO_IN1 >> 11) & 1u));
    uart_puts(" bit44="); uart_putc('0' + ((GPIO_IN1 >> 12) & 1u));
    uart_puts(")\r\n");

    /* UART1 备好, RXD 信号 15 后面反复改来源 */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 5);
    UART1_CLKDIV = (40000000u / 115200u);
    UART1_CLK_CONF = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    UART1_CONF0 = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);

    /* === 测试 1: U1RXD ← GPIO44 (ROM 已配好的控制台 RX, 空闲应为 1) === */
    GPIO_FUNC_IN_SEL(15) = 44 | (1u << 6);
    delay_ms(2);
    uart_puts("[PINCHK] rxd<-pin44: RXD=");
    uart_putc('0' + ((UART1_STATUS >> 15) & 1u));
    uart_puts("\r\n");

    /* === 测试 2: GPIO43 自驱自读 (备份原配置) === */
    unsigned int save_iomux43 = GPIO_REG(IOMUX_BASE + 4 + 43 * 4);
    unsigned int save_outsel43 = GPIO_FUNC_OUT_SEL(43);
    uart_puts("[PINCHK] orig iomux43="); put_hex(save_iomux43);
    uart_puts(" outsel43="); put_hex(save_outsel43);
    uart_puts("\r\n");

    GPIO_REG(IOMUX_BASE + 4 + 43 * 4) = (1u << 12) | (1u << 9) | (2u << 10);
    GPIO_FUNC_OUT_SEL(43) = 128;
    GPIO_REG(GPIO_BASE + 0x30) = (1u << 11); /* ENABLE1_W1TS bit(43-32) */
    GPIO_FUNC_IN_SEL(15) = 43 | (1u << 6);
    for (int t = 0; t < 4; t++) {
        unsigned int v = (t & 1);
        if (v) {
            GPIO_OUT_W1TC = 0; GPIO_REG(GPIO_BASE + 0x18) = (1u << 11); /* OUT1_W1TC */
        } else {
            GPIO_REG(GPIO_BASE + 0x14) = (1u << 11); /* OUT1_W1TS */
        }
        delay_ms(3);
        uart_puts("[PINCHK] t"); uart_putc('0' + t);
        uart_puts(" drive="); uart_putc('0' + (1 - v));
        uart_puts(" OUT1="); put_hex(GPIO_REG(GPIO_BASE + 0x10));
        uart_puts(" IN43="); uart_putc('0' + ((GPIO_IN1 >> 11) & 1u));
        uart_puts(" RXD="); uart_putc('0' + ((UART1_STATUS >> 15) & 1u));
        uart_puts("\r\n");
    }

    /* 恢复 GPIO43 (U0TXD): 先等 UART0 FIFO 排空, 避免尾巴乱码 */
    while (((UART0_STATUS >> 16) & 0x3FF) != 0) {}
    delay_ms(2);
    GPIO_REG(GPIO_BASE + 0x34) = (1u << 11); /* ENABLE1_W1TC */
    GPIO_FUNC_OUT_SEL(43) = save_outsel43;
    GPIO_REG(IOMUX_BASE + 4 + 43 * 4) = save_iomux43;
    delay_ms(5);
    uart_puts("[PINCHK] restored, console alive check\r\n");

    /* === 测试 3: U1RXD ← GPIO5 (Pi TXD, 空闲应为 1; 0 = Pi未发/接线异常) === */
    GPIO_REG(IOMUX_BASE + 4 + 5 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_IN_SEL(15) = 5 | (1u << 6);
    delay_ms(2);
    uart_puts("[PINCHK] rxd<-pin5: RXD=");
    uart_putc('0' + ((UART1_STATUS >> 15) & 1u));
    uart_puts(" IN0.bit5="); uart_putc('0' + ((GPIO_IN0 >> 5) & 1u));
    uart_puts("\r\n");

    uart_puts("[PINCHK] done\r\n");
    for (;;) { delay_ms(1000); }
}
