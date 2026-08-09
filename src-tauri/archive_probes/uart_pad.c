/* uart_pad.c - pad 输入通路验证: GPIO4 软件直驱翻转, GPIO_IN 回读
 * 若回读跟随翻转 → pad 输入正常, 问题在 UART RX 侧
 * 若回读恒 0 → IOMUX/输入缓冲问题
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

#define GPIO_BASE  0x60004000u
#define IOMUX_BASE 0x60009000u
#define GPIO_REG(a) (*(volatile unsigned int *)(a))
#define GPIO_OUT_REG         GPIO_REG(GPIO_BASE + 0x04)
#define GPIO_ENABLE_REG      GPIO_REG(GPIO_BASE + 0x20)
#define GPIO_IN_REG          GPIO_REG(GPIO_BASE + 0x3C)
#define GPIO_ENABLE_W1TS     GPIO_REG(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC     GPIO_REG(GPIO_BASE + 0x28)
#define GPIO_OUT_W1TS        GPIO_REG(GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC        GPIO_REG(GPIO_BASE + 0x0C)

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

    delay_ms(2000);
    uart_puts("\r\n[PAD] GPIO4 drive/read-back test v2\r\n");

    /* IOMUX GPIO4: MCU_SEL=1(GPIO), FUN_IE=1, DRV=2, FUN_PU=bit8 */
    GPIO_REG(IOMUX_BASE + 4 * 4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    uart_puts("[PAD] iomux4="); put_hex(GPIO_REG(IOMUX_BASE + 4 * 4)); uart_puts("\r\n");
    uart_puts("[PAD] gpio_in(all)="); put_hex(GPIO_IN_REG); uart_puts("\r\n");

    for (int round = 0; round < 4; round++) {
        int hi = (round & 1) == 0;
        if (hi) {
            GPIO_ENABLE_W1TS = (1u << 4);
            GPIO_OUT_W1TS = (1u << 4);
        } else {
            GPIO_ENABLE_W1TS = (1u << 4);
            GPIO_OUT_W1TC = (1u << 4);
        }
        delay_ms(20);
        unsigned int gi = GPIO_IN_REG;
        uart_puts("[PAD] round="); uart_putc('0' + round);
        uart_puts(" drive="); uart_putc(hi ? 'H' : 'L');
        uart_puts(" out="); put_hex(GPIO_OUT_REG);
        uart_puts(" en="); put_hex(GPIO_ENABLE_REG);
        uart_puts(" in="); put_hex(gi);
        uart_puts(" read4="); uart_putc('0' + ((gi >> 4) & 1));
        uart_puts((int)(((gi >> 4) & 1) == (unsigned)hi) ? "  OK\r\n" : "  FAIL\r\n");
        delay_ms(300);
    }
    /* 收尾: 释放 GPIO4 为输入 */
    GPIO_ENABLE_W1TC = (1u << 4);
    uart_puts("[PAD] done\r\n");
    for (;;) { delay_ms(1000); }
}
