#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define deepsleep() syscall(325)
#define deepkill(pid, sig) syscall(326, pid, sig)


int zombiegen()
{
	int pid;

	switch(pid=fork()) {
	case 0:
		printf("try to kill pid %d\n",getpid());
		exit(0);
	default:
		printf("without killing %d\n",getpid());
		while(1){
			sleep(20);
		}
	}

	return 0;
}





int main()
{
	return zombiegen();
}
