/**
 * @file ready.c
 *
 */
/* Embedded Xinu, Copyright (C) 2009.  All rights reserved. */

#include <thread.h>
#include <queue.h>
#include <percpu.h>
#include <interrupt.h>

extern void send_ipi(int cpu);

/**
 * @ingroup threads
 *
 * Make a thread eligible for CPU service.
 * @param tid target thread
 * @param resch if RESCHED_YES, reschedules
 * @return OK if thread has been added to readylist, else SYSERR
 */
int ready(tid_typ tid, bool resch)
{
    register struct thrent *thrptr;
    int home;

    if (isbadtid(tid))
    {
        return SYSERR;
    }

    irqmask im;

    thrptr = &thrtab[tid];
    home = thrptr->cpu_home;
    if (home < 0 || home > 1) home = 0;

    im = disable();
    thrptr->state = THRREADY;
    insert(tid, percpu[home].readyq, thrptr->prio);
    restore(im);

    if (home != cpu_id())
    {
        percpu[home].need_resched = 1;
        send_ipi(home);
    }
    else if (resch == RESCHED_YES)
    {
        resched();
    }
    return OK;
}
