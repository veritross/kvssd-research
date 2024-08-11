obj-m += ramdisk.o
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

format:
	clang-format -i *.c

cscope:
	find . /usr/src/linux-headers-$(shell uname -r | sed -En "s/\-generic//p") \( \
		-name '*.c' \
		-o -name '*.cpp' \
		-o -name '*.cc' \
		-o -name '*.h' \
		-o -name '*.s' \
		-o -name '*S' \) \
		-print > cscope.files # create a cscope.files
	cscope -bqk

tags: cscope
	ctags -L cscope.files


clean:
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean

