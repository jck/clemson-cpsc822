#include <linux/kernel.h>
#include <linux/syscalls.h>

__sched int sleep_on(wait_queue_head_t *q)
{
    unsigned long flags;
    wait_queue_t wait;

    init_waitqueue_entry(&wait, current);
    __set_current_state(TASK_UNINTERRUPTIBLE);
    spin_lock_irqsave(&q->lock, flags);
    __add_wait_queue(q, &wait);
    spin_unlock(&q->lock);
    schedule();
    spin_lock_irq(&q->lock);
    __remove_wait_queue(q, &wait);
    spin_unlock_irqrestore(&q->lock, flags);
    return 0;
}

DECLARE_WAIT_QUEUE_HEAD(gone);


SYSCALL_DEFINE0(deepsleep)
{
  return sleep_on(&gone);
}
