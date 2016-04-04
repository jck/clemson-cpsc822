#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define deepsleep() syscall(325)
#define smunch(pid, bit_pattern) syscall(326, pid, bit_pattern)

int main()
{
	smunch(100, 100);
}
