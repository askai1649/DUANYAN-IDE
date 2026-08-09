/* uart_multidrv.c - 多脚排雷: 逐个强驱 GPIO5/6/7/18 Low->High, 读回 pad 电平
 * pad 回读: drive=HIGH 时读 1=悬空正常, 读 0=被外部拉到 GND (线错接)
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
static void put_num(unsigned int v)
{
    char b[12]; int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[i++] = '0' + v % 10; v /= 10; }
    while (i--) uart_putc(b[i]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

static void put_hex(unsigned int v)
{
    const char *d = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) uart_putc(d[(v >> i) & 15]);
}
static void test_pin(int p)
{
    /* MCU_SEL=2(GPIO) + FUN_DRV=2 + FUN_IE=bit9(必须开才能回读pad) */
    IOMUX_PIN(p) = (1u << 12) | (2u << 10) | (1u << 9);
    GPIO_FUNC_OUT_SEL(p) = 128;
    GPIO_ENABLE_W1TS = (1u << p);

    GPIO_OUT_W1TC = (1u << p);
    delay_ms(300);
    uart_puts("[MDRV] pin="); put_num(p);
    uart_puts(" LOW  pad="); put_num((GPIO_IN_REG >> p) & 1);
    uart_puts(" en="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x20));
    uart_puts(" out="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x04));
    uart_puts("\r\n");
    delay_ms(700);

    GPIO_OUT_W1TS = (1u << p);
    delay_ms(300);
    uart_puts("[MDRV] pin="); put_num(p);
    uart_puts(" HIGH pad="); put_num((GPIO_IN_REG >> p) & 1);
    uart_puts(" en="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x20));
    uart_puts(" out="); put_hex(*(volatile unsigned int *)(GPIO_BASE + 0x04));
    uart_puts("\r\n");
    delay_ms(700);

    /* 释放: 输出关闭, 回到悬空输入 */
    *(volatile unsigned int *)(GPIO_BASE + 0x28) = (1u << p); /* ENABLE_W1TC */
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
    uart_puts("\r\n[MDRV] multi-pin drive test loop\r\n");

    for (;;) {
        test_pin(5);
        test_pin(6);
        test_pin(7);
        test_pin(18);
        delay_ms(2000);
    }
}
