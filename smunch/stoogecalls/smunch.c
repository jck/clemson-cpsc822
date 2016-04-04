#include <linux/kernel.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE2(smunch, int, pid, unsigned long, bit_pattern)
{
	pr_info("smunch pid: %d; signals: %lx", pid, bit_pattern);
	return 0;
}
