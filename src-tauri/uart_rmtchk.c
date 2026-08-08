/* uart_rmtchk.c - 新板 RMT 死因探针
 * 现象: TX_START 后 INT_RAW bit0 (CH0_TX_END) 永不触发, anchors=-1
 * 侦查: ①GPIO48 直驱点灯验灯珠 ②dump 关键寄存器 ③TX 后轮询
 *      STATUS/INT_RAW 时序曲线, 看状态机是否动弹 ④复位位 toggle
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
#define GPIO_BASE 0x60004000u
#define RMT_BASE  0x60016000u
#define RMT_RAM_CH0 0x60016800u
#define SYSTEM_PERIP_CLK_EN0 R(0x600C0018)
#define SYSTEM_PERIP_RST_EN0 R(0x600C001C)

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
static void put_dec(unsigned int v)
{
    char b[12]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + v % 10; v /= 10; }
    while (n--) uart_putc(b[n]);
}
static void delay_ms(unsigned int ms)
{
    volatile unsigned int n = ms * 16000u;
    while (n--) {}
}
static void reg_show(const char *name, unsigned int addr)
{
    uart_puts(name); uart_puts("="); put_hex(R(addr)); uart_puts("\r\n");
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
    uart_puts("\r\n[RMTCHK] new-board RMT autopsy\r\n");

    /* ① GPIO48 直驱点灯: 灯珠/IOMUX/直驱通路 */
    R(0x60009000u + 4 + 48 * 4) = (1u << 12) | (3u << 10);
    R(GPIO_BASE + 0x24) = (1u << 16);          /* OUT1 组 bit16: enable GPIO48 */
    uart_puts("[C1] direct red 2s\r\n");
    R(GPIO_BASE + 0x14) = (1u << 16);          /* OUT1_W1TS */
    delay_ms(2000);
    R(GPIO_BASE + 0x18) = (1u << 16);          /* OUT1_W1TC */
    delay_ms(500);

    /* ② RMT 寄存器 dump (含复位位状态) */
    SYSTEM_PERIP_CLK_EN0 |= (1u << 9);
    uart_puts("[C2] regs:\r\n");
    reg_show(" CLK_EN0", 0x600C0018);
    reg_show(" RST_EN0", 0x600C001C);
    reg_show(" SYS_CONF", RMT_BASE + 0xC0);
    reg_show(" CH0CONF0", RMT_BASE + 0x20);
    reg_show(" CH0STATUS", RMT_BASE + 0x64);
    reg_show(" INT_RAW", RMT_BASE + 0x60);
    reg_show(" INT_ENA", RMT_BASE + 0x68);

    /* ③ 复位 toggle: RST_EN0 bit9 置1再清0 */
    SYSTEM_PERIP_RST_EN0 |= (1u << 9);
    delay_ms(10);
    SYSTEM_PERIP_RST_EN0 &= ~(1u << 9);
    delay_ms(10);
    uart_puts("[C3] after rst toggle:\r\n");
    reg_show(" CH0CONF0", RMT_BASE + 0x20);
    reg_show(" SYS_CONF", RMT_BASE + 0xC0);

    /* ④ 装载短帧(4 个 item)发射, 轮询 INT_RAW/STATUS 曲线 */
    R(RMT_BASE + 0xC0) = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 4)
                       | (1u << 22) | (1u << 24) | (1u << 26) | (1u << 31);
    R(RMT_BASE + 0x20) = (1u << 8) | (1u << 16) | (1u << 6);
    R(RMT_BASE + 0x20) |= (1u << 24);
    R(RMT_BASE + 0x68) |= 1u;
    R(GPIO_BASE + 0x554 + 48 * 4) = 81;        /* pin48 <- RMT sig81 */
    volatile unsigned int *ram = (volatile unsigned int *)RMT_RAM_CH0;
    for (int i = 0; i < 4; i++) ram[i] = 4u | (1u << 15) | (8u << 16);
    ram[4] = 128;   /* 结束标记 */
    ram[5] = 0;
    R(RMT_BASE + 0x6C) = 1u;                   /* INT_CLR */
    R(RMT_BASE + 0x20) = ((1u << 8) | (1u << 16) | (1u << 6)) | (1u << 1) | (1u << 2);
    R(RMT_BASE + 0x20) = (1u << 8) | (1u << 16) | (1u << 6);
    R(RMT_BASE + 0x20) = ((1u << 8) | (1u << 16) | (1u << 6)) | (1u << 24);
    R(RMT_BASE + 0x20) = ((1u << 8) | (1u << 16) | (1u << 6)) | (1u << 0);
    uart_puts("[C4] started, polling:\r\n");
    for (int t = 0; t < 20; t++) {
        delay_ms(5);
        uart_puts(" t="); put_dec(t);
        uart_puts(" RAW="); put_hex(R(RMT_BASE + 0x60));
        uart_puts(" ST="); put_hex(R(RMT_BASE + 0x64));
        uart_puts(" CONF="); put_hex(R(RMT_BASE + 0x20));
        uart_puts("\r\n");
    }
    uart_puts("[C5] done\r\n");
    for (;;) { delay_ms(1000); }
}
