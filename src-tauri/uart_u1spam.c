/* uart_u1spam.c - U1 TX 活性探针: GPIO4 经矩阵 sig15 @9600 持续吐数 */
#define RTC_CNTL_WDTWPROTECT (*(volatile unsigned int *)0x600080B0)
#define RTC_CNTL_WDTCONFIG0  (*(volatile unsigned int *)0x60008098)
#define RTC_CNTL_WDTFEED     (*(volatile unsigned int *)0x600080AC)
#define RTC_CNTL_SWD_WPROT   (*(volatile unsigned int *)0x600080B8)
#define RTC_CNTL_SWD_CONF    (*(volatile unsigned int *)0x600080B4)
#define TIMG0_WDTWPROTECT (*(volatile unsigned int *)0x6001F064)
#define TIMG0_WDTCONFIG0  (*(volatile unsigned int *)0x6001F048)
#define TIMG0_INT_CLR     (*(volatile unsigned int *)0x6001F07C)

#define R(a) (*(volatile unsigned int *)(a))
#define GPIO_ENABLE_W1TS   R(0x60004000u + 0x24)
#define GPIO_FUNC_OUT_SEL(pin) R(0x60004000u + 0x554 + (pin) * 4)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define UART0_FIFO   (*(volatile unsigned int *)0x60000000)
#define UART0_STATUS (*(volatile unsigned int *)0x6000001C)

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
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
    uart_puts("\r\n[U1SPAM] start\r\n");

    PERIP_CLK_EN0 |= (1u << 5);
    U1(0x14) = (40000000u / 9600u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
    IOMUX_PIN(4) = (1u << 12) | (1u << 9) | (2u << 10) | (1u << 8);
    GPIO_FUNC_OUT_SEL(4) = 15;
    GPIO_ENABLE_W1TS = (1u << 4);

    uart_puts("[U1SPAM] u1 inited, spamming @9600\r\n");
    unsigned int cnt = 0;
    for (;;) {
        for (volatile int w = 0; w < 200000; w++) {
            if (((U1(0x1C) >> 16) & 0x3FF) < 127) break;
        }
        U1(0x00) = (unsigned int)'U';
        if ((++cnt & 0x3FF) == 0) {
            uart_puts("[U1SPAM] sent ");
            uart_putc('0' + ((cnt >> 10) & 7));
            uart_puts("k\r\n");
        }
    }
}
