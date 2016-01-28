obj-m += mymod.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all: module user

.PHONY: module
module:
	# $(MAKE) -C /usr/src/linux M=$(PWD) modules
	$(MAKE) -C /home/jck/projects/822/build/linux M=$(PWD) modules

user: user.c
	gcc -o user user.c
	scp user 822:

.PHONY: rmod
rmod: module
	scp mymod.ko 822:
	ssh 822 'rmmod mymod; insmod mymod.ko'

.PHONY: ruser
ruser: user
	ssh 822 ./user

clean:
	rm *.ko *.o *.mod.c
