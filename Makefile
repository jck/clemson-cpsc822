obj-m += mymod.o

default:
	# $(MAKE) -C /usr/src/linux M=$(PWD) modules
	$(MAKE) -C /home/jck/projects/822/build/linux M=$(PWD) modules

clean:
	rm *.ko *.o *.mod.c
