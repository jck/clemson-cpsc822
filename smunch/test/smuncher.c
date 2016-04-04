#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define deepsleep() syscall(325)
#define smunch(pid, bit_pattern) syscall(326, pid, bit_pattern)

int main(int argc, char *argv[])
{
	int pid = atoi(argv[1]);
	smunch(pid, 100);
}
