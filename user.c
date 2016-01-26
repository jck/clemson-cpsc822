#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

int main() {
  printf("user code: open\n");
  int fd = open("/dev/kyouko3", O_RDWR);
  printf("user code: close\n");
  close(fd);
}
