KDIR := /home/jajenks2/linux-5.15.165

obj-m += mp1.o

all:
	make -C $(KDIR) M=$(PWD) modules
	gcc -o userapp userapp.c

clean:
	make -C $(KDIR) M=$(PWD) clean
	$(RM) userapp
