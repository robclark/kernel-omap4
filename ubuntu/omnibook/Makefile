#
# Makefile -- makefile for the HP OmniBook support module
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# Written by Soós Péter <sp@osb.hu>, 2002-2004
# Modified by Mathieu Bérard <mathieu.berard@crans.org>, 2006-2007
#

#Module informations

MODULE_NAME = omnibook
MODULE_VERSION = 2.20090707
MODULE_BRANCH = trunk

# Out-of-tree configuration
ifndef CONFIG_OMNIBOOK
OMNIBOOK_STANDALONE=y
CONFIG_OMNIBOOK=m

#Uncomment and set to force debug behavior
#NOTE:	Default (commented) behavior is to enable debug in trunk or branch svn 
#	snapshot and to disable it for release
#OMNIBOOK_WANT_DEBUG=n

#comment to disable backlight device support
OMNIBOOK_WANT_BACKLIGHT=y

#Uncomment to force legacy (pre-ACPI system) features support
#OMNIBOOK_WANT_LEGACY=y

endif

ifeq ($(KERNELRELEASE),)
# Support for direct Makefile invocation

DESTDIR	= 
MODDIR	= $(DESTDIR)/lib/modules
KVERS	= $(shell uname -r)
KVER	= $(KVERS)
VMODDIR = $(MODDIR)/$(KVER)
INSTDIR	= extra
#KSRC	= /usr/src/linux
KSRC	= $(VMODDIR)/build
KMODDIR	= $(KSRC)/drivers/misc/omnibook
KDOCDIR	= $(KSRC)/Documentation/omnibook
PWD	= $(shell pwd)
TODAY	= $(shell date +%Y%m%d)
DEPMOD	= /sbin/depmod -aq
RMMOD	= /sbin/modprobe -r
INSMOD	= /sbin/modprobe
INSTALL	= install -m 644
MKDIR	= mkdir -p
RM	= rm -f
FIND	= find

all:		 $(MODULE_NAME).ko

clean:
		make -C $(KSRC) M=$(PWD) clean
		$(RM) -r *~ "#*#" .swp
		$(RM) -r debian/omnibook-source *-stamp
		$(RM) -r Module.symvers Modules.symvers

install:	all
		# Removing module from locations used by previous versions
		$(RM) $(VMODDIR)/kernel/drivers/char/$(MODULE_NAME).ko
		$(RM) $(VMODDIR)/kernel/drivers/misc/$(MODULE_NAME).ko
		make INSTALL_MOD_PATH=$(DESTDIR) INSTALL_MOD_DIR=$(INSTDIR) -C $(KSRC) M=$(PWD) modules_install

unload:
		$(RMMOD) $(MODULE_NAME) || :

load:		install unload
		$(DEPMOD)
		$(INSMOD) $(MODULE_NAME)

uninstall:	unload
		$(FIND) $(VMODDIR) -name "$(MODULE_NAME).ko" -exec $(RM) {} \;
		$(DEPMOD)

$(MODULE_NAME).ko:
		$(MAKE) -C $(KSRC) SUBDIRS=$(PWD) modules

kinstall:
		$(RM) -r $(KMODDIR)
		$(MKDIR) $(KMODDIR)
		$(INSTALL) *.h *.c sections.lds $(KMODDIR)
		$(MKDIR) $(KDOCDIR)
		$(INSTALL) doc/README $(KDOCDIR)
		
kpatch:		kinstall
		(cd $(KSRC); patch -p1 < $(PWD)/misc/omnibook-integration.patch)

version:	
		sed -i "s|^\(MODULE_VERSION = \).*|\1 2.$(TODAY)|" Makefile
		sed -i "s|^\(MODULE_BRANCH = \).*|\1 release|" Makefile
		sed -i "s|^\(2\.\)X\{8\}|\1$(TODAY)|" doc/ChangeLog
		

release:	clean version
		mkdir -p ../$(MODULE_NAME)-2.$(TODAY)
		cp -a *.h *.c *.lds Makefile doc misc ../$(MODULE_NAME)-2.$(TODAY)
		rm -f ../$(MODULE_NAME)-2.$(TODAY).tar ../$(MODULE_NAME)-2.$(TODAY).tar.gz
		(cd ..; tar cvf $(MODULE_NAME)-2.$(TODAY).tar $(MODULE_NAME)-2.$(TODAY); gzip -9 $(MODULE_NAME)-2.$(TODAY).tar)

else
# Support for kernel build system invocation

ifneq ($(MODULE_BRANCH), release)
EXTRA_CFLAGS += -DOMNIBOOK_MODULE_VERSION='"$(MODULE_VERSION)-$(MODULE_BRANCH)"'
else
EXTRA_CFLAGS += -DOMNIBOOK_MODULE_VERSION='"$(MODULE_VERSION)"'
endif

ifeq ($(OMNIBOOK_STANDALONE),y)

ifeq ($(OMNIBOOK_WANT_BACKLIGHT),y)
ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
# we support backlight interface only after 2.6.16
ifeq ($(shell if [ $(SUBLEVEL) -gt 16 ] ; then echo -n 'y'; fi),y)
EXTRA_CFLAGS += -DCONFIG_OMNIBOOK_BACKLIGHT
else
$(warning "Backlight support in only supported for kernel version newer than 2.6.16")
$(warning "Disabling backlight sysfs interface")
endif
endif
endif

ifeq ($(OMNIBOOK_WANT_LEGACY),y)
EXTRA_CFLAGS += -DCONFIG_OMNIBOOK_LEGACY
endif

ifndef CONFIG_ACPI_EC
EXTRA_CFLAGS += -DCONFIG_OMNIBOOK_LEGACY
endif

ifneq ($(MODULE_BRANCH), release)
ifneq ($(OMNIBOOK_WANT_DEBUG),n)	
EXTRA_CFLAGS += -DCONFIG_OMNIBOOK_DEBUG # -Wa -g0
endif
else
ifeq ($(OMNIBOOK_WANT_DEBUG),y)	
EXTRA_CFLAGS += -DCONFIG_OMNIBOOK_DEBUG # -Wa -g0
endif

endif

endif

EXTRA_CFLAGS += -DOMNIBOOK_MODULE_NAME='"$(MODULE_NAME)"'
#EXTRA_LDFLAGS +=  $(src)/sections.lds
EXTRA_LDFLAGS += $(PWD)/ubuntu/omnibook/sections.lds

obj-$(CONFIG_OMNIBOOK) += $(MODULE_NAME).o
omnibook-objs := init.o lib.o ec.o kbc.o pio.o compal.o acpi.o nbsmi.o \
          ac.o battery.o blank.o bluetooth.o cooling.o display.o dock.o \
	  dump.o fan.o fan_policy.o hotkeys.o info.o lcd.o muteled.o \
	  polling.o temperature.o touchpad.o wireless.o throttling.o 

endif # End of kernel build system part

# End of file
