obj-m += mymod.o

all: module user

.PHONY: module
module:
	# $(MAKE) -C /usr/src/linux M=$(PWD) modules
	$(MAKE) -C /home/jck/projects/822/build/linux M=$(PWD) modules

user: user.c
	gcc -o user user.c

clean:
	rm *.ko *.o *.mod.c
