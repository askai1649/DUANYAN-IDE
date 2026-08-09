/* uart_mini.c - 最小二分探针: 关狗 → UART0 打印 → GPIO47 心跳 */
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

#define GPIO_OUT_W1TS (*(volatile unsigned int *)0x60004008)
#define GPIO_OUT_W1TC (*(volatile unsigned int *)0x6000400C)
#define GPIO_ENABLE_W1TS (*(volatile unsigned int *)0x60004024)
#define IOMUX_GPIO47 (*(volatile unsigned int *)(0x60009000 + 47 * 4))

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0x3FF) >= 127) {}
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
    /* 阶段1: 关狗 */
    RTC_CNTL_WDTWPROTECT = 0x50D83AA1u;
    RTC_CNTL_WDTCONFIG0 = 0;
    RTC_CNTL_WDTFEED = 1;
    RTC_CNTL_SWD_WPROT = 0x8F1D312Au;
    RTC_CNTL_SWD_CONF |= (1u << 31);
    TIMG0_WDTWPROTECT = 0x50D83AA1u;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_INT_CLR = 0xFFFFFFFFu;
    uart_puts("[MINI] WDT off\r\n");

    /* 阶段2: UART0 打印循环 */
    for (int i = 0; i < 5; i++) {
        uart_puts("[MINI] ALIVE ");
        uart_putc('0' + i);
        uart_puts("\r\n");
        delay_ms(200);
    }

    /* 阶段3: GPIO47 心跳 (验证执行到末段) */
    IOMUX_GPIO47 = (1u << 12) | (3u << 10);
    GPIO_ENABLE_W1TS = (1u << 47);
    uart_puts("[MINI] HEARTBEAT START\r\n");
    for (;;) {
        GPIO_OUT_W1TS = (1u << 47);
        delay_ms(300);
        GPIO_OUT_W1TC = (1u << 47);
        delay_ms(300);
        uart_puts("[MINI] HB\r\n");
    }
}
