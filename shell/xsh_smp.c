/**
 * @file xsh_smp.c
 * Comando SMP: estado y pruebas en core1 (fork minimo).
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <thread.h>
#include <clock.h>
#include <semaphore.h>
#include <memory.h>
#include <percpu.h>
#include <spinlock.h>
#include <riscv.h>
#include <esp32p4.h>
#include <fsutil.h>
#include <fat_filelib.h>
#include <shell.h>

static void c1test(void)
{
    int i;
    tid_typ t = gettid();
    for (i = 0; i < 10; i++)
    {
        kprintf("[core1] i=%d cpu=%d tid=%d cur=%d\r\n",
                i, cpu_id(), t, (int)thrcurrent);
        sleep(1);
    }
    kprintf("[core1] fin prueba tid=%d\r\n", t);
}

static void c1burn(void)
{
    volatile uint32_t n = 0;
    uint32_t start = clktime;
    kprintf("[core1] burn inicio cpu=%d tid=%d\r\n", cpu_id(), gettid());
    while ((clktime - start) < 5)
    {
        n++;
    }
    kprintf("[core1] burn fin cpu=%d n=%u\r\n", cpu_id(), (unsigned)n);
}

static volatile int smp_sleep_done;
static void c1sleep(void)
{
    int i;
    for (i = 0; i < 50; i++) sleep(10);
    smp_sleep_done++;
}

static int stress_sleep(void)
{
    tid_typ t[4];
    int i, waited = 0;
    smp_sleep_done = 0;
    for (i = 0; i < 4; i++)
    {
        t[i] = create_on(1, (void *)c1sleep, 8192, 20, "c1slp", 0);
        if (SYSERR == (int)t[i]) return SYSERR;
        ready(t[i], RESCHED_NO);
    }
    while (smp_sleep_done < 4 && waited < 20000) { sleep(20); waited += 20; }
    printf("smp stress sleep: %d/4 hebras completaron en core1\n", smp_sleep_done);
    return (smp_sleep_done == 4) ? OK : SYSERR;
}

static semaphore smp_sem_a, smp_sem_b;
static volatile int smp_sem_count;
static void c1sem(void)
{
    int i;
    for (i = 0; i < 200; i++)
    {
        wait(smp_sem_a);
        smp_sem_count++;
        signal(smp_sem_b);
    }
    smp_sleep_done++;
}

static int stress_sem(void)
{
    tid_typ t;
    int i, waited = 0;
    smp_sem_count = 0;
    smp_sleep_done = 0;
    smp_sem_a = semcreate(0);
    smp_sem_b = semcreate(0);
    if (SYSERR == smp_sem_a || SYSERR == smp_sem_b) return SYSERR;
    t = create_on(1, (void *)c1sem, 8192, 20, "c1sem", 0);
    if (SYSERR == (int)t) { semfree(smp_sem_a); semfree(smp_sem_b); return SYSERR; }
    ready(t, RESCHED_NO);
    sleep(10);
    for (i = 0; i < 200; i++)
    {
        signal(smp_sem_a);
        wait(smp_sem_b);
    }
    while (smp_sleep_done < 1 && waited < 10000) { sleep(10); waited += 10; }
    semfree(smp_sem_a); semfree(smp_sem_b);
    printf("smp stress sem: count=%d (esperado 200) done=%d\n", smp_sem_count, smp_sleep_done);
    return (smp_sem_count == 200 && smp_sleep_done == 1) ? OK : SYSERR;
}

static tid_typ smp_msg_peer;
static void c1msg(void)
{
    int i;
    for (i = 0; i < 100; i++)
    {
        message m = receive();
        if ((int)m >= 1000 && (int)m < 1100)
        {
            send(smp_msg_peer, (message)((int)m + 1000));
        }
    }
    smp_sleep_done++;
}

static int stress_msg(void)
{
    tid_typ t;
    int i, ok = 0, waited = 0;
    smp_sleep_done = 0;
    /* Drenar mensajes pendientes (p.ej. notificaciones de kill() de pruebas previas). */
    while (recvclr() != NOMSG) { }
    smp_msg_peer = gettid();
    t = create_on(1, (void *)c1msg, 8192, 20, "c1msg", 0);
    if (SYSERR == (int)t) return SYSERR;
    ready(t, RESCHED_NO);
    sleep(10);
    for (i = 0; i < 100; i++)
    {
        if (OK == send(t, (message)(1000 + i)))
        {
            message r = receive();
            if (r == (message)(2000 + i)) ok++;
        }
    }
    while (smp_sleep_done < 1 && waited < 5000) { sleep(10); waited += 10; }
    /* Drenar notificación de salida del hilo trabajador. */
    while (recvclr() != NOMSG) { }
    printf("smp stress msg: %d/100 intercambios OK\n", ok);
    return (ok == 100) ? OK : SYSERR;
}

static volatile int smp_mem_fail;
static volatile int smp_mem_done;
static void c1mem(void)
{
    int i;
    for (i = 0; i < 200; i++)
    {
        void *p = memget(256);
        if ((int)p == SYSERR) smp_mem_fail++;
        else { memset(p, 0xA5, 256); if (memfree(p, 256) == SYSERR) smp_mem_fail++; }
        if ((i & 7) == 0) yield();
    }
    smp_mem_done++;
}

static int stress_mem(void)
{
    tid_typ t[2];
    int i, j, waited = 0;
    smp_mem_fail = 0; smp_mem_done = 0;
    for (i = 0; i < 2; i++)
    {
        t[i] = create_on(1, (void *)c1mem, 8192, 20, "c1mem", 0);
        if (SYSERR == (int)t[i]) return SYSERR;
        ready(t[i], RESCHED_NO);
    }
    for (j = 0; j < 200; j++)
    {
        void *p = memget(256);
        if ((int)p == SYSERR) smp_mem_fail++;
        else { memset(p, 0x5A, 256); if (memfree(p, 256) == SYSERR) smp_mem_fail++; }
        if ((j & 7) == 0) yield();
    }
    while (smp_mem_done < 2 && waited < 15000) { sleep(10); waited += 10; }
    printf("smp stress mem: fallos=%d (hebras core1 terminadas %d/2)\n", smp_mem_fail, smp_mem_done);
    return (smp_mem_fail == 0 && smp_mem_done == 2) ? OK : SYSERR;
}

static volatile int smp_fat_done;
static volatile int smp_fat_fail;
static void fatxc_worker(int arg)
{
    char path[24];
    unsigned char buf[512];
    void *f;
    unsigned off, i;
    int bad = 0;
    int seed = (arg >> 8) & 0xFF;
    sprintf(path, "/sxc%d.fat", seed);
    fl_remove(path);
    f = fl_fopen(path, "w");
    if (f == NULL) bad++;
    else
    {
        for (off = 0; off < 4096; off += 512)
        {
            for (i = 0; i < 512; i++) buf[i] = (unsigned char)((off + i + seed * 17) & 0xFF);
            if (fl_fwrite(buf, 1, 512, f) != 512) { bad++; break; }
        }
        fl_fflush(f); fl_fclose(f);
    }
    f = fl_fopen(path, "r");
    if (f == NULL) bad++;
    else
    {
        fl_fseek(f, 0, SEEK_END);
        if (fl_ftell(f) != 4096) bad++;
        fl_fseek(f, 0, SEEK_SET);
        for (off = 0; off < 4096 && bad == 0; off += 512)
        {
            if (fl_fread(buf, 1, 512, f) != 512) { bad++; break; }
            for (i = 0; i < 512; i++)
                if (buf[i] != (unsigned char)((off + i + seed * 17) & 0xFF)) { bad++; break; }
        }
        fl_fclose(f);
    }
    if (fl_remove(path) != 0) bad++;
    { irqmask im = disable(); if (bad) smp_fat_fail++; smp_fat_done++; restore(im); }
}

static int stress_fat(void)
{
    tid_typ t1[2], t0[2];
    int i, waited = 0;
    if (!fs_mounted()) { printf("smp stress fat: no hay volumen montado\n"); return SYSERR; }
    smp_fat_done = 0; smp_fat_fail = 0;
    for (i = 0; i < 2; i++)
    {
        t1[i] = create_on(1, (void *)fatxc_worker, SHELL_CMDSTK, SHELL_CMDPRIO, "fatc1", 1, (i + 10) << 8);
        t0[i] = create_on(0, (void *)fatxc_worker, SHELL_CMDSTK, SHELL_CMDPRIO, "fatc0", 1, (i + 20) << 8);
        if (SYSERR == (int)t1[i] || SYSERR == (int)t0[i]) return SYSERR;
    }
    for (i = 0; i < 2; i++) { ready(t1[i], RESCHED_NO); ready(t0[i], RESCHED_NO); }
    while (smp_fat_done < 4 && waited < 30000) { sleep(20); waited += 20; }
    printf("smp stress fat: %d/4 hebras, fallos=%d\n", smp_fat_done, smp_fat_fail);
    return (smp_fat_done == 4 && smp_fat_fail == 0) ? OK : SYSERR;
}


/* ---- stress mixto: FAT + mem + sem/msg + compute concurrentes en ambos núcleos ---- */
static volatile int smx_done, smx_fail;
static semaphore smx_sem;

static void smx_worker(int arg)
{
    int id = arg & 0xFF;
    int core = (arg >> 8) & 0xFF;
    int i, bad = 0;
    char path[24];
    unsigned char buf[256];

    sprintf(path, "/smx%d.dat", id);

    for (i = 0; i < 40; i++)
    {
        /* memoria */
        void *p = memget(128 + (i & 0x3F));
        if ((int)p == SYSERR) { bad++; }
        else { memset(p, (id + i) & 0xFF, 128 + (i & 0x3F)); memfree(p, 128 + (i & 0x3F)); }

        /* sincronización cross-core */
        if ((i & 3) == 0) { signal(smx_sem); wait(smx_sem); }

        /* FAT: escritura/lectura/verificación corta */
        if ((i & 7) == 0 && fs_mounted())
        {
            void *f = fl_fopen(path, "w");
            int j;
            if (!f) { bad++; }
            else {
                for (j = 0; j < 256; j++) buf[j] = (unsigned char)((id + i + j) & 0xFF);
                if (fl_fwrite(buf, 1, 256, f) != 256) bad++;
                fl_fflush(f); fl_fclose(f);
            }
            f = fl_fopen(path, "r");
            if (!f) { bad++; }
            else {
                if (fl_fread(buf, 1, 256, f) != 256) bad++;
                else {
                    for (j = 0; j < 256; j++)
                        if (buf[j] != (unsigned char)((id + i + j) & 0xFF)) { bad++; break; }
                }
                fl_fclose(f);
            }
        }

        if ((i & 1) == 0) yield(); else sleep(1);
    }

    if (fs_mounted()) fl_remove(path);
    { irqmask im = disable(); if (bad) smx_fail++; smx_done++; restore(im); }
    (void)core;
}

static int stress_mixed(void)
{
    tid_typ t[4];
    int i, waited = 0;
    if (!fs_mounted()) { printf("smp stress mixed: no hay volumen montado\n"); return SYSERR; }
    smx_done = 0; smx_fail = 0;
    smx_sem = semcreate(1);
    if (SYSERR == smx_sem) return SYSERR;

    /* 2 hebras en core0, 2 en core1, ejecutando carga mixta simultánea */
    for (i = 0; i < 2; i++)
    {
        t[i] = create_on(0, (void *)smx_worker, SHELL_CMDSTK, SHELL_CMDPRIO, "smx0", 1, (i) | (0 << 8));
        t[i+2] = create_on(1, (void *)smx_worker, SHELL_CMDSTK, SHELL_CMDPRIO, "smx1", 1, (i + 10) | (1 << 8));
        if (SYSERR == (int)t[i] || SYSERR == (int)t[i+2]) { semfree(smx_sem); return SYSERR; }
    }
    for (i = 0; i < 4; i++) ready(t[i], RESCHED_NO);

    while (smx_done < 4 && waited < 60000) { sleep(20); waited += 20; }
    semfree(smx_sem);
    printf("smp stress mixed: %d/4 hebras, fallos=%d\n", smx_done, smx_fail);
    return (smx_done == 4 && smx_fail == 0) ? OK : SYSERR;
}


static void drain_msgs(void)
{
    while (recvclr() != NOMSG) { }
}

static int run_stress(void)
{
    int fails = 0;
    printf("=== SMP stress ===\n");
    drain_msgs();
    if (stress_sleep() != OK) { printf("  [FAIL] sleep\n"); fails++; } else { printf("  [OK] sleep\n"); }
    drain_msgs();
    if (stress_sem()   != OK) { printf("  [FAIL] sem\n");   fails++; } else { printf("  [OK] sem\n"); }
    drain_msgs();
    if (stress_msg()   != OK) { printf("  [FAIL] msg\n");   fails++; } else { printf("  [OK] msg\n"); }
    drain_msgs();
    if (stress_mem()   != OK) { printf("  [FAIL] mem\n");   fails++; } else { printf("  [OK] mem\n"); }
    drain_msgs();
    if (stress_fat()   != OK) { printf("  [FAIL] fat\n");   fails++; } else { printf("  [OK] fat\n"); }
    drain_msgs();
    if (stress_mixed() != OK) { printf("  [FAIL] mixed\n"); fails++; } else { printf("  [OK] mixed\n"); }
    drain_msgs();
    printf("=== resultado: %s (%d fallos) ===\n", fails == 0 ? "OK" : "FAIL", fails);
    return fails == 0 ? 0 : 1;
}

static volatile int sem1_done;
static semaphore sem1_sem;
static void c1sem1(void)
{
    wait(sem1_sem);
    sem1_done = 1;
}

static int stress_sem1(void)
{
    tid_typ t;
    int waited = 0;
    sem1_done = 0;
    sem1_sem = semcreate(0);
    if (SYSERR == sem1_sem) return SYSERR;
    t = create_on(1, (void *)c1sem1, 8192, 20, "c1sm1", 0);
    if (SYSERR == (int)t) { semfree(sem1_sem); return SYSERR; }
    ready(t, RESCHED_NO);
    sleep(20);
    signal(sem1_sem);
    while (sem1_done < 1 && waited < 5000) { sleep(10); waited += 10; }
    semfree(sem1_sem);
    printf("smp stress sem1: done=%d\n", sem1_done);
    return (sem1_done == 1) ? OK : SYSERR;
}

static volatile int sem2_done;
static semaphore sem2_sem;
static void c1sem2(void)
{
    sleep(20);
    signal(sem2_sem);
}

static int stress_sem2(void)
{
    tid_typ t;
    int waited = 0;
    sem2_done = 0;
    sem2_sem = semcreate(0);
    if (SYSERR == sem2_sem) return SYSERR;
    t = create_on(1, (void *)c1sem2, 8192, 20, "c1sm2", 0);
    if (SYSERR == (int)t) { semfree(sem2_sem); return SYSERR; }
    ready(t, RESCHED_NO);
    wait(sem2_sem);
    sem2_done = 1;
    while (waited < 0) waited++; /* no-op */
    semfree(sem2_sem);
    printf("smp stress sem2: done=%d\n", sem2_done);
    return (sem2_done == 1) ? OK : SYSERR;
}

static void usage(void) { printf("Uso: smp <status|test|burn|stress [sleep|sem|msg|mem|fat|mixed|bg]|help>\n"); }

shellcmd xsh_smp(int nargs, char *args[])
{
    if (nargs < 2) { usage(); return 1; }
    if (strncmp(args[1], "status", 7) == 0)
    {
        extern volatile uint32 core1_ticks;
        extern volatile uint32 core1_timer_irq_count;
        printf("core0: current=%d readyq=%d nest=%d resdef=%d\n",
               percpu[0].current, percpu[0].readyq, percpu[0].nest, percpu[0].resdef);
        printf("core1: current=%d readyq=%d nest=%d resdef=%d idle=%d ticks=%u irq=%u\n",
               percpu[1].current, percpu[1].readyq, percpu[1].nest, percpu[1].resdef,
               percpu[1].idle, (unsigned)core1_ticks, (unsigned)core1_timer_irq_count);
        return 0;
    }
    if (strncmp(args[1], "test", 5) == 0)
    {
        tid_typ tid = create_on(1, (void *)c1test, 8192, 20, "c1test", 0);
        if (SYSERR == (int)tid) { printf("smp: no se pudo crear hebra\n"); return 1; }
        ready(tid, RESCHED_NO);
        printf("smp: hebra %d creada en core1\n", tid);
        return 0;
    }
    if (strncmp(args[1], "burn", 5) == 0)
    {
        tid_typ tid = create_on(1, (void *)c1burn, 8192, 20, "c1burn", 0);
        if (SYSERR == (int)tid) { printf("smp: no se pudo crear hebra\n"); return 1; }
        ready(tid, RESCHED_NO);
        printf("smp: hebra cpu-bound %d creada en core1 (5s)\n", tid);
        return 0;
    }
    if (strncmp(args[1], "stress", 7) == 0)
    {
        if (nargs >= 3 && strncmp(args[2], "bg", 2) == 0)
        {
            tid_typ t = create_on(0, (void *)run_stress, 16384, 20, "smpstr", 0);
            if (SYSERR == (int)t) { printf("no tid\n"); return 1; }
            ready(t, RESCHED_NO);
            printf("stress bg tid=%d\n", t);
            return 0;
        }
        if (nargs >= 3)
        {
            if (strncmp(args[2], "sleep", 5) == 0) return (stress_sleep()==OK)?0:1;
            if (strncmp(args[2], "sem2", 4) == 0)  return (stress_sem2()==OK)?0:1;
            if (strncmp(args[2], "sem1", 4) == 0)  return (stress_sem1()==OK)?0:1;
            if (strncmp(args[2], "sem", 3) == 0)   return (stress_sem()==OK)?0:1;
            if (strncmp(args[2], "msg", 3) == 0)   return (stress_msg()==OK)?0:1;
            if (strncmp(args[2], "mem", 3) == 0)   return (stress_mem()==OK)?0:1;
            if (strncmp(args[2], "fat", 3) == 0)   return (stress_fat()==OK)?0:1;
            if (strncmp(args[2], "mixed", 5) == 0) return (stress_mixed()==OK)?0:1;
        }
        return run_stress();
    }
    usage();
    return 0;
}
