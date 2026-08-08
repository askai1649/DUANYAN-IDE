/* uart_mcuscan.c - 扫描 IO_MUX MCU_SEL 0~7, 找出让 GPIO6 pad 输出生效的值
 * 方法: 每个值都 驱High读pad + 驱Low读pad, pad 跟随 = 该值正确
 * (GPIO6 无接线, pad 回读可信: g5in2 已证 FUN_IE bit9 输入通路活)
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
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
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
    uart_puts("\r\n[MCUSCAN] MCU_SEL 0..7 scan on GPIO6\r\n");

    GPIO_FUNC_OUT_SEL(6) = 128;      /* 固定: GPIO矩阵信号128 */
    GPIO_ENABLE_W1TS = (1u << 6);

    for (;;) {
        for (int sel = 0; sel < 8; sel++) {
            /* MCU_SEL=sel, FUN_DRV=2, FUN_IE=1, 带上拉帮助读回 */
            IOMUX_PIN(6) = ((unsigned)sel << 12) | (2u << 10) | (1u << 9);

            GPIO_OUT_W1TS = (1u << 6);
            delay_ms(300);
            int hi = (GPIO_IN_REG >> 6) & 1;
            GPIO_OUT_W1TC = (1u << 6);
            delay_ms(300);
            int lo = (GPIO_IN_REG >> 6) & 1;

            uart_puts("[MCUSCAN] sel="); put_num(sel);
            uart_puts(" hi="); put_num(hi);
            uart_puts(" lo="); put_num(lo);
            if (hi == 1 && lo == 0) uart_puts("  <== WORKS");
            uart_puts("\r\n");
            delay_ms(200);
        }
        delay_ms(3000);
    }
}
