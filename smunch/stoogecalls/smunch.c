//Author: Keerthan Jaic
#include <linux/kernel.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE2(smunch, int, pid, unsigned long, bit_pattern)
{
	struct task_struct *p;
	unsigned long flags;
	// use sigset_t instead of direct bitwise ops for portability.
	sigset_t new_set;

	pr_info("smunch: pid=%d; sigmask=%*pbl\n", pid, 64, &bit_pattern);

	if (!(p = pid_task(find_vpid(pid), PIDTYPE_PID))){
		pr_warn("Task not found for pid=%d", pid);
		return -1;
	}

	// Multithreaded process
	if (!thread_group_empty(p))
		return -1;

	siginitset(&new_set, bit_pattern);

	// Ignore all other signals if kill bit was set
	if (sigismember(&new_set, SIGKILL))
		siginitset(&new_set, sigmask(SIGKILL));

	if (p->exit_state == EXIT_ZOMBIE) {
		pr_info("zombie. all sigs except kill will be ignored\n");
		if (sigismember(&new_set, SIGKILL)) {
			release_task(p);
		}
		return 0;
	}

	if (!lock_task_sighand(p, &flags)) {
		pr_warn("smunch: lock_task_sighand failed for pid: %d\n", pid);
		return -1;
	}

	// Replace shared_pending signals with the new ones
	p->signal->shared_pending.signal = new_set;
	set_tsk_thread_flag(p, TIF_SIGPENDING);
	unlock_task_sighand(p, &flags);
	wake_up_process(p);

	return 0;
}
