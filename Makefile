HOST=$(shell hostname)

ifeq ($(HOST), serenity)
	KDIR = /home/jck/projects/822/build
else
	KDIR = /usr/src/linux
endif

obj-m += kyouko3.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all: module user
r: rmod ruser

.PHONY: module
module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user: user.c
	gcc -std=gnu99 -o user user.c
	rsync user 822:

.PHONY: rmod
rmod: module
	rsync kyouko3.ko 822:
	ssh 822 'rmmod kyouko3; insmod kyouko3.ko'

.PHONY: ruser
ruser: user
	ssh 822 ./user

clean:
	rm *.ko *.o *.mod.c
