#include <stdio.h>
#include <sys/signal.h>
#include <unistd.h>

void my_handler(int signum)
{
	if (signum == SIGUSR1) {
		printf("received SIGUSR1 \n");
	}
	if (signum == SIGUSR2) {
		printf("received SIGUSR2 \n");
	}
}

int main ()
{
	int pid, ret;
	switch(pid=fork()) {
	case 0:
		signal(SIGUSR1, my_handler);
		signal(SIGUSR2, my_handler);
		printf("child pid: %d\n", getpid());
		printf("child is playing\n");
		while (1) {
			sleep(1);
		}
		break;
	default:
		printf("parent pid: %d\n", getpid());
		printf("parent is going to sleep\n");
		while (1) {
			sleep(10);
		}
	}

}
