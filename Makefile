obj-m += kyouko3.o

default:
	-mknod /dev/kyouko3 c $$major 500 $$minor 127
	$(MAKE) -C /usr/src/linux M=$(PWD) modules 2> log
	gcc -std=gnu99 -g -Wall -Wextra user.c 2> ulog #tester.c
	-rmmod kyouko3
	insmod kyouko3.ko


clean:
	-rm *.ko
	-rm *.o
	-rm *.mod.c
	-rm *.out
