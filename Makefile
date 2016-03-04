HOST=$(shell hostname)

ifeq ($(HOST), serenity)
	KDIR = /home/jck/projects/822/build
else
	KDIR = /usr/src/linux
endif

obj-m += kyouko3.o
# ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all: module demos tests
r: rmod rtest
rd: rmod rdemo

.PHONY: module
module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

demos: user.c
	gcc -std=gnu99 -o demos user.c

tests: user.c
	gcc -std=gnu99 -DTESTING -o tests user.c

.PHONY: rmod
rmod: module
	rsync kyouko3.ko 822:
	ssh 822 'rmmod kyouko3; insmod kyouko3.ko dyndbg=+pmfl'

.PHONY: rtest
rtest: tests
	rsync tests 822:
	ssh 822 ./tests

.PHONY: rdemo
rdemo: demos
	rsync demos 822:
	ssh 822 ./demos

clean:
	rm -f *.ko *.o *.mod.c
