/* uart_xdrive2.c - 交叉判线: 先强驱 GPIO4 Low 12s, 再强驱 GPIO5 Low 12s, 交替 4 轮
 * Pi 侧扫描全排针, 谁跟随变 Low 就是该 ESP 脚的实际落点
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
    uart_puts("\r\n[XDRIVE2] alternate drive GPIO4/GPIO5 Low, 12s each x4 rounds\r\n");

    /* 两脚都配成 GPIO 直驱 (sig128), 输出使能 */
    IOMUX_PIN(4) = (1u << 12) | (2u << 10);
    GPIO_FUNC_OUT_SEL(4) = 128;
    IOMUX_PIN(5) = (1u << 12) | (2u << 10);
    GPIO_FUNC_OUT_SEL(5) = 128;
    GPIO_ENABLE_W1TS = (1u << 4) | (1u << 5);
    GPIO_OUT_W1TS = (1u << 4) | (1u << 5);   /* 先都拉高 */

    for (int r = 0; r < 4; r++) {
        GPIO_OUT_W1TC = (1u << 4);            /* GPIO4 Low */
        GPIO_OUT_W1TS = (1u << 5);
        uart_puts("[XDRIVE2] GPIO4=LOW GPIO5=HIGH\r\n");
        delay_ms(12000);
        GPIO_OUT_W1TS = (1u << 4);
        GPIO_OUT_W1TC = (1u << 5);            /* GPIO5 Low */
        uart_puts("[XDRIVE2] GPIO4=HIGH GPIO5=LOW\r\n");
        delay_ms(12000);
    }
    uart_puts("[XDRIVE2] done\r\n");
    for (;;) { delay_ms(1000); }
}
