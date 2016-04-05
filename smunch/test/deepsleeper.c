#include <stdio.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#define deepsleep() syscall(325)  // ...or whatever number is next

int main()
{
    printf("goodnight, Irene\n");
    deepsleep();
    printf("oops ... woke up!\n");
}
