/**
 * @file core1smp.c
 * Entrada SMP de core1, idle e IPI de replanificación.
 */
#include <percpu.h>
#include <thread.h>
#include <queue.h>
#include <interrupt.h>
#include <platform.h>
#include <clock.h>
#include <esp32p4.h>
#include <riscv.h>

#define HP_SYSTEM_CPU_INT_FROM_CPU_0_REG (*(volatile uint32_t *)(HP_SYS_BASE + 0x10))
#define HP_SYSTEM_CPU_INT_FROM_CPU_1_REG (*(volatile uint32_t *)(HP_SYS_BASE + 0x14))
#define ETS_FROM_CPU_INTR0_SOURCE 100
#define ETS_FROM_CPU_INTR1_SOURCE 101
#define IPI_CLIC_LINE0 28
#define IPI_CLIC_LINE1 29

#ifndef IRQ_SOFTWARE
#define IRQ_SOFTWARE IRQ_M_SOFT
#endif

extern void core1_idle(void);
extern int resched(void);
extern volatile uint32 core1_ticks;
extern interrupt clkhandler(void);

/* ---- SYSTIMER tick para core1 (port de /home/daniel/lk-esp32p4) ----------
 * Registros del systimer_dev_t del P4 (offsets verificados en LK):
 *   conf 0x00, U0_OP 0x04, TARGET1_HI 0x24, TARGET1_LO 0x28,
 *   TARGET1_CONF 0x38, COMP1_LOAD 0x54, INT_ENA 0x64, INT_CLR 0x6C.
 * Fuente ETS SYSTIMER_TARGET1 = 54 -> linea CLIC 30.  La linea se configura
 * SHV (hardware-vectored) como las demas de Xinu: el CLIC salta por mtvt al
 * trampolin p4_irq_entry, que despacha a interruptVector[30] -> nuestro
 * handler.  (Con attr=0 non-vectored, que usa LK, Xinu cae en unhandled trap.)
 */
#define ST_BASE         0x500E2000UL
#define ST_CONF         (*(volatile uint32_t *)(ST_BASE + 0x00))
#define ST_U0_OP        (*(volatile uint32_t *)(ST_BASE + 0x04))
#define ST_T1_HI        (*(volatile uint32_t *)(ST_BASE + 0x24))
#define ST_T1_LO        (*(volatile uint32_t *)(ST_BASE + 0x28))
#define ST_T1_CONF      (*(volatile uint32_t *)(ST_BASE + 0x38))
#define ST_C1_LOAD      (*(volatile uint32_t *)(ST_BASE + 0x54))
#define ST_INT_ENA      (*(volatile uint32_t *)(ST_BASE + 0x64))
#define ST_INT_CLR      (*(volatile uint32_t *)(ST_BASE + 0x6C))
#define ST_T1_BIT       (1U << 1)
#define ST_T1_WORK_EN   (1U << 23)
#define ST_U0_WORK_EN   (1U << 30)
#define ST_CLK_EN       (1U << 31)
#define ST_T1_SRC       54U
#define ST_CLIC_LINE    30U
#define ST_CLK_HZ       16000000U
#define HP_SYS2         0x500E6000UL
/* clic_t: interrupt[] alineado a 0x1000, 4 bytes/linea (ip,ie,attr,ct) */
#define CLIC_LINE_BASE(n) ((volatile uint8_t *)(P4_CLIC_BASE + 0x1000UL + 4UL*(n)))
#define CLIC_INTERRUPT_IE(n)   (CLIC_LINE_BASE(n)[1])
#define CLIC_INTERRUPT_ATTR(n) (CLIC_LINE_BASE(n)[2])

volatile uint32 core1_timer_irq_count;

static inline uint64_t st_now(void)
{
    ST_U0_OP = (1U << 30);
    for (int i = 0; i < 10000; i++)
        if (ST_U0_OP & (1U << 29)) break;
    return ((uint64_t)(*(volatile uint32_t *)(ST_BASE + 0x40) & 0xFFFFFu) << 32)
         | (uint64_t)*(volatile uint32_t *)(ST_BASE + 0x44);
}

static void st_arm(uint64_t delta)
{
    uint64_t target = st_now() + (delta ? delta : 1);
    ST_T1_HI = (uint32_t)((target >> 32) & 0xFFFFFu);
    ST_T1_LO = (uint32_t)(target & 0xFFFFFFFFu);
    ST_C1_LOAD = 1;
}

static interrupt core1_timer_handler(void)
{
    ST_INT_CLR = ST_T1_BIT;
    core1_timer_irq_count++;
    st_arm(ST_CLK_HZ / CLKTICKS_PER_SEC);
    clkhandler();   /* rama core1: resched + cpu_ticks, sin tocar clktotal */
    return;
}

/* core0: habilita clocks y programa el primer disparo */
void core1_timer_init_primary(void)
{
    volatile uint32_t *clk2 = (volatile uint32_t *)(HP_SYS2 + 0x1c);
    volatile uint32_t *pclk = (volatile uint32_t *)(HP_SYS2 + 0x98);
    *clk2 |= (1U << 23);
    *pclk |= (1U << 30);
    ST_CONF |= (ST_CLK_EN | ST_U0_WORK_EN | ST_T1_WORK_EN);
    ST_INT_CLR = ST_T1_BIT;
    ST_INT_ENA &= ~ST_T1_BIT;
    ST_T1_CONF = 0;
    ST_C1_LOAD = 1;
    interruptVector[ST_CLIC_LINE] = core1_timer_handler;
    ST_INT_CLR = ST_T1_BIT;
    ST_INT_ENA |= ST_T1_BIT;
    st_arm(ST_CLK_HZ / CLKTICKS_PER_SEC);
}

/* core1: rutea fuente->linea y configura CLIC SHV */
void core1_timer_init_secondary(void)
{
    extern void route_irq(int source, int line);
    route_irq(ST_T1_SRC, ST_CLIC_LINE);
    /* SHV como las demas lineas de Xinu */
    CLIC_INTERRUPT_ATTR(ST_CLIC_LINE) = 0x01;
    CLIC_INTERRUPT_IE(ST_CLIC_LINE) = 1;
}

/* IPI por HP_SYSTEM CPU_INT_FROM_CPU: despierta/preempta al núcleo destino. */
void send_ipi(int cpu)
{
    if (cpu == 1)
    {
        HP_SYSTEM_CPU_INT_FROM_CPU_1_REG = 1;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }
    else if (cpu == 0)
    {
        HP_SYSTEM_CPU_INT_FROM_CPU_0_REG = 1;
        __asm__ volatile("fence rw, rw" ::: "memory");
    }
}

/* Handler de IPI: limpia la señal de CPU origen y replanifica. */
interrupt ipihandler(void)
{
    if (cpu_id() == 1)
    {
        HP_SYSTEM_CPU_INT_FROM_CPU_1_REG = 0;
    }
    else
    {
        HP_SYSTEM_CPU_INT_FROM_CPU_0_REG = 0;
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
    resched();
}

static inline uint64_t mtime64(void)
{
    uint32_t hi, lo;
    do
    {
        hi = P4_MTIME_HI;
        lo = P4_MTIME_LO;
    } while (hi != P4_MTIME_HI);
    return ((uint64_t)hi << 32) | lo;
}

void core1_idle(void)
{
    irqmask im;

    /* El tick de core1 lo genera la IRQ del SYSTIMER target1 (ver
     * core1_timer_init_*), que llama a clkhandler() en core1 y ahi hace
     * resched de sus tareas.  Ya no hay sondeo de mtime: con la IRQ el core1
     * es preemptivo incluso si una tarea esta en un bucle cpu-bound.
     * Este idle solo atiende IPIs/need_resched/readyq mientras no hay nada
     * que ejecutar. */
    for (;;)
    {
        im = disable();
        if (percpu[1].need_resched)
        {
            percpu[1].need_resched = 0;
            restore(im);
            resched();
            continue;
        }
        if (nonempty(percpu[1].readyq))
        {
            restore(im);
            resched();
            continue;
        }
        restore(im);
        __asm__ volatile("nop; nop; nop; nop");
    }
}

void core1_smp_entry(void)
{
    struct percpu *c = &percpu[1];
    extern void clkupdate1(ulong cycles);

    c->nest = 0;
    c->resdef = 0;
    c->idle = 1;
    c->current = 1;

    thrtab[1].state = THRCURR;
    thrtab[1].intmask = 0;
    thrtab[1].cpu_last = 1;

    /* Tick local en core1 (CLINT mtimecmp hart1 / IRQ_TIMER). */
    interruptVector[IRQ_TIMER] = clkhandler;
    clkupdate1(platform.clkfreq / CLKTICKS_PER_SEC);
    enable_irq(IRQ_TIMER);
    /* Solo MTIE (bit 7).  MEIE se habilito temporalmente para probar el
     * SYSTIMER pero no sirvio y puede entregar IRQs externas sin handler al
     * hart1 (irq_missing -> for(;;) = core1 colgado), asi que se quita. */
    __asm__ volatile("csrs mie, %0" :: "r"(1UL << 7));

    /* IPI por HP_SYSTEM FROM_CPU (ruta en matriz CLIC). */
    extern void route_irq(int source, int line);
    extern void enable_irq_line(int line);
    route_irq(ETS_FROM_CPU_INTR1_SOURCE, IPI_CLIC_LINE1);
    interruptVector[IPI_CLIC_LINE1] = ipihandler;
    enable_irq_line(IPI_CLIC_LINE1);

    /* Tick SYSTIMER del core1: ruta fuente 54 -> CLIC 30 y handler SHV. */
    core1_timer_init_secondary();

    enable();

    core1_idle();
}
