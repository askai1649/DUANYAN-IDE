/* uart_hello8.c v8 - 两阶段协议终战固件: 汇编规范环
 * 背景: 新板 GPIO 矩阵输出路由死, W1TS/W1TC 直驱 pad 活
 * v2-v4 教训链: C 忙等环每次重编译指令流都变 → 测量/延时单位比漂移,
 *   数据依赖分支使每迭代成本随电平漂移 → 解码累积偏移
 * v5 方案: 等沿/延时/RX解码/TX发射 全部共享同一段内联汇编 tick 体
 *   (l32i,extui,xor,and,bnez,addi,bnez 共7条, 编译器不可重排),
 *   测量单位 == 延时单位 == 发射单位, 单位比恒为 1
 * 校准: Pi 发 'U'*8 (bit0=1): 下降沿→首上升沿 = 精确 1 位宽
 * 协议: U*8 → 2s → HELLO-PI (8字节) → ESP 解码+位啜回显
 * 控制台: UART0 (COM4)
 * v8 修复: ①帧同步 bug — bit7=0 时 wait_fall 在位7中心即触发(线已低),
 *   后续字节整体错位 (v2/v6 相同漂移模式的根因); 现每字节后先 wait_rise
 *   越过停止位再等下一轮下降沿 ②TX 停止位驱动为低的 bug (bit8=1)
 *   ③tx 块操作数 12→10, 去地址表改双地址寄存器+moveqz 防约束超限
 * v9 修复: v7/v8 的 rx/tx tick 体 xor x,lvl,lvl 恒 0 → 永不破环且采样恒 0
 *   (实测解码全 0x00); 正解: 延时用 asm_delay 同形纯延时(men=0),
 *   循环后单次读电平 (1 tick 粒度 vs 103 ticks/位可忽略)
 * v10 终因: moveqz 语义记反! Xtensa moveqz ar,as,at 是 at≠0 时才搬移。
 *   h8diag 诊断实测证实延时+采样点全对(位中心电平与 HELLO-PI 完全吻合),
 *   v9 的 moveqz x,x,lvl 在 lvl=1 时反而把 x 清 0 → 解码全 0x00。
 *   改用显式 beqz 分支组位; tx 的 moveqz at,tc,lvl 按正确语义恰好对
 *   (lvl=1→保持W1TS, lvl=0→改W1TC), 不动
 * v11: beqz 版单体块仍全 0x00 (反汇编验证逻辑无误) → 单体块存在未知
 *   时序问题; 回退到 h8diag 实测证明完美的结构: C 逐位循环 +
 *   asm_delay/rd5 (每段延时同一规范环, 实测漂移 <半个位宽)
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
#define GPIO_IN_ADDR       (volatile unsigned int *)(GPIO_BASE + 0x3C)
#define GPIO_OUT_W1TS_ADDR (volatile unsigned int *)(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC_ADDR (volatile unsigned int *)(GPIO_BASE + 0xC)
#define GPIO_OUT_W1TS      R(GPIO_BASE + 0x8)
#define GPIO_OUT_W1TC      R(GPIO_BASE + 0xC)
#define GPIO_ENABLE_W1TS   R(GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC   R(GPIO_BASE + 0x28)
#define IOMUX_PIN(p) R(0x60009000u + 4 + (p) * 4)

#define TX4_HI() (GPIO_OUT_W1TS = (1u << 4))
#define TX4_LO() (GPIO_OUT_W1TC = (1u << 4))

static void uart_putc(char c)
{
    for (volatile int w = 0; w < 100000; w++) {
        if (((UART0_STATUS >> 16) & 0x3FF) < 127) break;
    }
    UART0_FIFO = (unsigned int)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void put_dec(unsigned int v)
{
    char b[12]; int n = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v) { b[n++] = '0' + v % 10; v /= 10; }
    while (n--) uart_putc(b[n]);
}
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

/* ==== 汇编规范环 v6 ====
 * 唯一 tick 体 (7条, 所有时序场合逐字一致):
 *   l32i / extui / xor / and / bnez / addi / bnez
 * men=0 时 and 门控 h=0 → 纯延时; men=1 时 xorv 选命中电平。
 * 测量/延时/收/发全部同一指令流 → 单位比恒 1, 编译器不可重排。
 * 操作数控制在 ≤9 个防 Xtensa 寄存器约束超限。 */

/* 规范环: 返回消耗 tick 数; men=0 纯延时跑满 n, men=1 命中沿即停 */
static unsigned int asm_ticks(volatile unsigned int *ain, unsigned int n,
                              unsigned int men, unsigned int xorv)
{
    unsigned int s = n, v, lvl, x, h;
    __asm__ volatile (
        "1:\n\t"
        "l32i %[v], %[a], 0\n\t"
        "extui %[lvl], %[v], 5, 1\n\t"
        "xor %[x], %[lvl], %[xv]\n\t"
        "and %[h], %[x], %[m]\n\t"
        "bnez %[h], 2f\n\t"
        "addi %[s], %[s], -1\n\t"
        "bnez %[s], 1b\n\t"
        "2:\n"
        : [s] "+r"(s), [v] "=&r"(v), [lvl] "=&r"(lvl),
          [x] "=&r"(x), [h] "=&r"(h)
        : [a] "r"(ain), [xv] "r"(xorv), [m] "r"(men)
        : "memory");
    return n - s;
}
static void asm_delay(volatile unsigned int *ain, unsigned int n)
{
    asm_ticks(ain, n, 0u, 0u);
}
static unsigned int asm_wait_fall(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 1u);
}
static unsigned int asm_wait_rise(volatile unsigned int *ain, unsigned int to)
{
    return asm_ticks(ain, to, 1u, 0u);
}

/* 单次读 GPIO5 电平 */
static unsigned int rd5(volatile unsigned int *ain)
{
    unsigned int v;
    __asm__ volatile ("l32i %[v], %[a], 0\n\t" : [v] "=r"(v) : [a] "r"(ain));
    return (v >> 5) & 1u;
}

/* 位啜收一字节 (8N1): 调用前刚捕到下降沿。
 * v11: h8diag 实测证明完美的结构 — C 逐位循环, 每段延时同一规范环,
 * 位中心单次读电平 */
static unsigned int rx_byte(volatile unsigned int *ain, unsigned int bitd)
{
    unsigned int byte = 0;
    asm_delay(ain, bitd + bitd / 2u);
    if (rd5(ain)) byte |= 1u;
    for (unsigned int b = 1; b < 8u; b++) {
        asm_delay(ain, bitd);
        if (rd5(ain)) byte |= (1u << b);
    }
    return byte;
}
/* 位啜发一字节 (8N1): 调用前外部已驱 start 低。
 * 8 数据位 + 停止位(恒高), 每段延时同一规范环 */
static void tx_byte_body(volatile unsigned int *ain, unsigned int v,
                         unsigned int bitd)
{
    for (unsigned int b = 0; b < 8u; b++) {
        asm_delay(ain, bitd);
        if ((v >> b) & 1u) GPIO_OUT_W1TS = (1u << 4);
        else GPIO_OUT_W1TC = (1u << 4);
    }
    asm_delay(ain, bitd);
    GPIO_OUT_W1TS = (1u << 4);   /* 停止位高 */
}

/* 排空突发残余: 3*bitd 窗口内无下降沿即退出 */
static void drain_burst(unsigned int bitd)
{
    for (;;) {
        if (asm_wait_fall(GPIO_IN_ADDR, bitd * 3u) >= bitd * 3u) return;
        asm_wait_rise(GPIO_IN_ADDR, bitd * 14u);
    }
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
    uart_puts("\r\n[HELLO8] v11 diag-proven C-loop bitbang RX(g5)+TX(g4)\r\n");

    /* GPIO5 输入; GPIO4 纯直驱输出, 空闲 High */
    GPIO_ENABLE_W1TC = (1u << 5);
    IOMUX_PIN(5) |= (1u << 9);
    IOMUX_PIN(4) |= (1u << 9);
    TX4_HI();
    GPIO_ENABLE_W1TS = (1u << 4);

    for (int round = 0; round < 50; round++) {
        uart_puts("[H8] await burst\r\n");
        if (asm_wait_fall(GPIO_IN_ADDR, 30000000u) >= 30000000u) {
            uart_puts("[H8] timeout\r\n");
            continue;
        }
        /* 校准: 'U' bit0=1 → 下降沿→首上升沿 = 1 位宽 (同一 tick 体度量) */
        unsigned int bitd = asm_wait_rise(GPIO_IN_ADDR, 5000000u);
        if (bitd == 0 || bitd >= 5000000u) {
            uart_puts("[H8] calib noedge\r\n");
            continue;
        }
        drain_burst(bitd);
        uart_puts("[H8] calib bitd="); put_dec(bitd); uart_puts("\r\n");

        /* 负载: 8 字节 */
        unsigned char buf[8];
        int got = 0;
        for (int i = 0; i < 8; i++) {
            if (asm_wait_fall(GPIO_IN_ADDR, 15000000u) >= 15000000u) break;
            buf[i] = (unsigned char)rx_byte(GPIO_IN_ADDR, bitd);
            got++;
            /* v8 帧同步: 越过停止位回高, 防 bit7=0 时 wait_fall 误触发 */
            asm_wait_rise(GPIO_IN_ADDR, bitd * 4u);
        }
        uart_puts("[H8] got="); put_dec(got); uart_puts(" data:");
        for (int i = 0; i < got; i++) { uart_putc(' '); put_hex(buf[i]); }
        uart_puts(" '");
        for (int i = 0; i < got; i++)
            uart_putc((buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.');
        uart_puts("'\r\n");

        /* 位啜回显: start低 + 整字节asm定时(含停止位高) + 停止位保持 */
        delay_ms(5);
        for (int i = 0; i < got; i++) {
            TX4_LO();
            tx_byte_body(GPIO_IN_ADDR, buf[i], bitd);
            asm_delay(GPIO_IN_ADDR, bitd);
        }
        uart_puts("[H8] echo sent\r\n");
    }
    for (;;) { delay_ms(1000); }
}
