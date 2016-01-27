obj-m += mymod.o

all: module user

.PHONY: module
module:
	# $(MAKE) -C /usr/src/linux M=$(PWD) modules
	$(MAKE) -C /home/jck/projects/822/build/linux M=$(PWD) modules

user: user.c
	gcc -o user user.c
	scp user 822:

.PHONY: rmod
rmod:
	scp mymod.ko 822:
	ssh 822 'rmmod mymod; insmod mymod.ko'

clean:
	rm *.ko *.o *.mod.c
