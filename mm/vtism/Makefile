
obj-y := vtism.o

page_classify-objs := page_classify.o kvm.o
page_migrate-objs := page_demotion.o

sysctl-objs := sysctl/vtismctl.o sysctl/pcm.o

vtism-objs := main.o $(sysctl-objs) workqueue.o $(page_classify-objs) $(page_migrate-objs)

EXTRA_CFLAGS += -Wno-unused-function

PWD := $(shell pwd)

vtism_dir = ~/vtism
kernel_dir = $(vtism_dir)

all:
	make -C $(kernel_dir) M=$(PWD) modules


clean:
	make -C $(kernel_dir) M=$(PWD) clean

in:
	sudo dmesg -C
	sudo insmod vtism.ko
	sudo dmesg

rm:
	sudo rmmod vtism