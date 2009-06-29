ndiswrapper-objs := crt.o hal.o iw_ndis.o loader.o ndis.o ntoskernel.o ntoskernel_io.o \
	pe_linker.o pnp.o proc.o rtl.o wrapmem.o wrapndis.o wrapper.o usb.o

EXPORTS = crt_exports.h hal_exports.h ndis_exports.h ntoskernel_exports.h \
	ntoskernel_io_exports.h rtl_exports.h usb_exports.h

STUB_SRCS = crt.c hal.c ndis.c ntoskernel.c ntoskernel_io.c \
	pnp.c rtl.c wrapndis.c usb.c


EXTRA_CFLAGS += -DENABLE_USB -I$(obj)
EXTRA_AFLAGS += -I$(obj)

# generate exports symbol table from C files
quiet_cmd_mkexport = MKEXPORT $@
cmd_mkexport = $(SHELL) $(srctree)/$(src)/mkexport.sh $< $@

%_exports.h: %.c $(srctree)/$(src)/mkexport.sh FORCE
	$(call if_changed,mkexport)

$(addprefix $(obj)/,$(EXPORTS:_exports.h=.o)): %.o: %_exports.h
extra-y += $(EXPORTS)

ifeq ($(CONFIG_X86_64),y)
quiet_cmd_mkstubs = MKSTUBS $@
cmd_mkstubs = $(SHELL) $(srctree)/$(src)/mkstubs.sh $(addprefix $(srctree)/$(src)/,$(STUB_SRCS)) >$@

$(obj)/win2lin_stubs.h: $(addprefix $(srctree)/$(src)/,$(STUB_SRCS)) FORCE
	$(call if_changed,mkstubs)

$(obj)/win2lin_stubs.o: $(obj)/win2lin_stubs.h
extra-y += win2lin_stubs.h
ndiswrapper-objs += win2lin_stubs.o
else
ndiswrapper-objs += divdi3.o
endif

obj-$(CONFIG_NDISWRAPPER) := ndiswrapper.o
