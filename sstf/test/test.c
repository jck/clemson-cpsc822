#include <stdio.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#define conf_reqtrace(arg) syscall(325, arg)
#define get_reqtrace(arg) syscall(326, arg)

struct req_trace_entry {
	int pid;
	int serv_time;
	int wait_time;
};

struct req_trace_tbl {
	int count;
	struct req_trace_entry data[20000];
};

void dump_reqtrace()
{
	struct req_trace_tbl trace;
	int pid, serv_time, wait_time;

	get_reqtrace(&trace);
	printf("pid,serv_time,wait_time\n");
	for (int i=0; i<trace.count; i++) {
		pid = trace.data[i].pid;
		serv_time = trace.data[i].serv_time;
		wait_time = trace.data[i].wait_time;
		printf("%d,%d,%d\n", pid, serv_time, wait_time);
	}
}

int main()
{
	conf_reqtrace(1);
	sleep(100);
	dump_reqtrace();
	conf_reqtrace(0);
}
