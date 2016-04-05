#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>

#define deepsleep() syscall(325)
#define smunch(pid, bit_pattern) syscall(326, pid, bit_pattern)

int main(int argc, char *argv[])
{
	int pid = atoi(argv[1]);
	unsigned long bit_pattern = 0;
	for (int i=2; i<argc; i++)
		bit_pattern |= sigmask(atoi(argv[i]));
	printf("smunch pid: %d, sigs: %lx;", pid, bit_pattern);
	smunch(pid, bit_pattern);
}
