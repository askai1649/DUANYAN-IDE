/* uart_hardpad.c - 强驱 GPIO4 交替, 同时读回 GPIO_IN(0x6000403C) bit4 看 pad 实际电平
 * 判据: 若 pad 读到 High 则线至少半通; 若 pad 恒 Low 则完全断开/被拉死
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
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_IN_REG        R(GPIO_BASE + 0x3C)
#define GPIO_FUNC_OUT_SEL(p) R(GPIO_BASE + 0x554 + (p) * 4)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

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
    const char *d = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(d[(v >> i) & 15]);
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

    delay_ms(300);
    uart_puts("\r\n[HARDPAD] drive GPIO4 alt + readback pad level\r\n");

    IOMUX_PIN(4) = (1u << 12) | (2u << 10);
    GPIO_FUNC_OUT_SEL(4) = 128;
    GPIO_ENABLE_W1TS = (1u << 4);

    for (int round = 0; round < 12; round++) {
        GPIO_OUT_W1TC = (1u << 4);
        delay_ms(200);
        uart_puts("[HARDPAD] drive=LOW  pad4=");
        put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts("\r\n");
        delay_ms(2800);
        GPIO_OUT_W1TS = (1u << 4);
        delay_ms(200);
        uart_puts("[HARDPAD] drive=HIGH pad4=");
        put_hex((GPIO_IN_REG >> 4) & 1);
        uart_puts("\r\n");
        delay_ms(2800);
    }
    uart_puts("[HARDPAD] done\r\n");
    for (;;) { delay_ms(1000); }
}
