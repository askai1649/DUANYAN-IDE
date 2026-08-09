/* uart_sigscan.c - RXD钉死判决 v3: 信号号扫描 + 时钟门回读
 * 背景: prst 实锤——外设复位脉冲无效; FLOW/RS485 本就是 0;
 *       CONST0|EN 挂 sig15 时 RXD 仍恒 1; 但矩阵写回读正确。
 * ★新假设: 本板是 FPGA 实现, 曾有 IOMUX 偏移差 4 的历史 (GPIO4=+0x14),
 *   U1RXD 输入实际接线信号号可能不是 15。TRM: U0RXD=12? U1RXD=15?
 *   本探针: 逐个把 CONST0|EN 挂到 sig 9..23, 看哪个让 U0/U1 RXD 变 0。
 * 同时回读 GPIO_CLOCK_GATE(0x6000462C) 排除矩阵输入时钟门。
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
#define GPIO_CLOCK_GATE R(GPIO_BASE + 0x62C)
#define GPIO_FUNC_IN_SEL(sig)  R(GPIO_BASE + 0x154 + (sig) * 4)
#define PERIP_CLK_EN0      R(0x600C0018u)

#define U0(a) (*(volatile unsigned int *)(0x60000000u + (a)))
#define U1(a) (*(volatile unsigned int *)(0x60010000u + (a)))
#define CONST1 56u
#define CONST0 60u

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
static void put_dec(unsigned int v)
{
    char buf[12];
    int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}

/* UART1 标准初始化 */
static void u1_init(void)
{
    U1(0x14) = (40000000u / 115200u);
    U1(0x78) = (3u << 20) | (1u << 22) | (1u << 24) | (1u << 25);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 17) | (1u << 18);
    delay_ms(2);
    U1(0x20) = (3u << 2) | (2u << 4) | (1u << 28) | (1u << 25);
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
    PERIP_CLK_EN0 |= (1u << 5);
    u1_init();

    uart_puts("\r\n[SIGSCAN] clk_gate="); put_hex(GPIO_CLOCK_GATE);
    uart_puts(" clk_en0="); put_hex(PERIP_CLK_EN0); uart_puts("\r\n");

    /* 初始各 sig 回读, 看默认路由 */
    uart_puts("[SIGSCAN] default sig9-23:");
    for (unsigned int sig = 9; sig <= 23; sig++) {
        uart_puts(" "); put_dec(sig); uart_puts(":"); put_hex(GPIO_FUNC_IN_SEL(sig));
    }
    uart_puts("\r\n");

    /* 逐个挂 CONST0|EN, 读 U1/U0 RXD */
    for (unsigned int sig = 9; sig <= 23; sig++) {
        GPIO_FUNC_IN_SEL(sig) = CONST0 | (1u << 6);
        delay_ms(2);
        unsigned int rb = GPIO_FUNC_IN_SEL(sig);
        unsigned int r1 = (U1(0x1C) >> 15) & 1u;
        unsigned int r0 = (U0(0x1C) >> 15) & 1u;
        uart_puts("[SIGSCAN] sig"); put_dec(sig);
        uart_puts(" rb="); put_hex(rb);
        uart_puts(" u1rxd="); uart_putc('0' + r1);
        uart_puts(" u0rxd="); uart_putc('0' + r0);
        if (r1 == 0 || r0 == 0) uart_puts("  <<<<< HIT");
        uart_puts("\r\n");
        GPIO_FUNC_IN_SEL(sig) = CONST1 | (1u << 6);  /* 还原 */
        delay_ms(1);
    }

    /* 复测确认 sig15 仍钉死, 并试 INV 位 (bit7) 自测矩阵响应 */
    GPIO_FUNC_IN_SEL(15) = CONST0 | (1u << 6) | (1u << 7);  /* 常量0+反相 → 应为1 */
    delay_ms(2);
    uart_puts("[SIGSCAN] sig15 inv rb="); put_hex(GPIO_FUNC_IN_SEL(15));
    uart_puts(" u1rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
    uart_puts("\r\n");
    GPIO_FUNC_IN_SEL(15) = CONST1 | (1u << 6);

    /* U0 同样扫: 把 9..23 挂 CONST0 看 U0 RXD (上面已同测, 此处补 6/12 经典号) */
    unsigned int cands[4] = {6, 12, 15, 18};
    for (int i = 0; i < 4; i++) {
        unsigned int sig = cands[i];
        GPIO_FUNC_IN_SEL(sig) = CONST0 | (1u << 6);
        delay_ms(2);
        uart_puts("[SIGSCAN] cand"); put_dec(sig);
        uart_puts(" u0rxd="); uart_putc('0' + ((U0(0x1C) >> 15) & 1u));
        uart_puts(" u1rxd="); uart_putc('0' + ((U1(0x1C) >> 15) & 1u));
        uart_puts("\r\n");
        GPIO_FUNC_IN_SEL(sig) = CONST1 | (1u << 6);
    }
    /* 还原 U0RXD 默认脚 GPIO43 */
    GPIO_FUNC_IN_SEL(12) = 43 | (1u << 6);

    uart_puts("[SIGSCAN] done\r\n");
    for (;;) { delay_ms(1000); }
}
