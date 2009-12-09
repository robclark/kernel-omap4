KERNELSRC?=/lib/modules/`uname -r`/build
KERNELVERSION=$(shell awk -F\" '/REL/ {print $$2}' $(shell grep -s -l REL $(KERNELSRC)/include/linux/version.h $(KERNELSRC)/include/linux/utsrelease.h))
KERNELMAJOR=$(shell echo $(KERNELVERSION)|head -c3)

CONFIG_FSAM7400?=m
obj-$(CONFIG_FSAM7400) += fsam7400.o

EXTRA_CFLAGS+=-c -Wall -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common
INCLUDE=-I$(KERNELSRC)/include

ifeq ($(KERNELMAJOR), 2.6)
KERNEL26 := 1
TARGET := fsam7400.ko
else
TARGET := fsam7400.o
endif

SOURCE := fsam7400.c

all: $(TARGET)

help:
	@echo Possible targets:
	@echo -e all\\t- default target, builds kernel module
	@echo -e install\\t- copies module binary to /lib/modules/$(KERNELVERSION)/extra/
	@echo -e clean\\t- removes all binaries and temporary files

fsam7400.ko: $(SOURCE) 
	$(MAKE) -C $(KERNELSRC) SUBDIRS=$(PWD) modules

fsam7400.o: $(SOURCE)
	$(CC) $(INCLUDE) $(EXTRA_CFLAGS) -DDEBUG -DMODVERSIONS -DMODULE -D__KERNEL__ -o $(TARGET) $(SOURCE)

clean:
	@echo -n "sweeping directory... "
	@rm -f *~ *.o *.ko .fsam7400* *.mod.c *symvers .tmp_versions/*
	@if [ -d .tmp_versions ]; then rmdir .tmp_versions; fi
	@echo "done"

load:	$(TARGET)
	insmod $(TARGET)

unload:
	rmmod fsam7400

install: $(TARGET)
	mkdir -p /lib/modules/$(KERNELVERSION)/extra
	cp -v $(TARGET) /lib/modules/$(KERNELVERSION)/extra/
	depmod -a
