#include <linux/kernel.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE2(smunch, int, pid, unsigned long, bit_pattern)
{
	struct task_struct *p;
	unsigned long flags;
	// use sigset_t instead of direct bitwise ops for portability.
	sigset_t new_set;

	p = pid_task(find_vpid(pid), PIDTYPE_PID);
	siginitset(&new_set, bit_pattern);

	pr_info("smunch: pid=%d; sigmask=%*pbl\n", pid, 64, &bit_pattern);

	if (!p) {
		pr_warn("smunch: No task with pid == %d found\n", pid);
		return -1;
	}

	if (!thread_group_empty(p)) {
		pr_warn("smunch does not apply to multithreaded processes\n");
		return -1;
	}

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

	unlock_task_sighand(p, &flags);

	return 0;
}
