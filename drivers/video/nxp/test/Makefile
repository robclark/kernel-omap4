PACKAGE_NAME=DEMO_TDA

RULES:=compile
# ROOT=/home/vadmin/dev/hdmi/omapzoom/nxp-modules
ROOT=..
INC=-I${ROOT} -I${ROOT}/inc -I${ROOT}/comps -I${ROOT}/comps/inc -I${ROOT}/comps/tmdlHdmiTx/inc -I${ROOT}/comps/tmdlHdmiCEC/inc

EXTRA_CFLAGS += -DFUNC_PTR=" " -DCONST_DAT="const " -DRAM_DAT=" "
CFLAGS=  ${INC}
# LDFLAGS= -lpthread
ACC=agcc
# ACC=arm-none-linux-gnueabi-gcc

BINARIES=demo_tda

all: $(RULES)

clean:  
	@echo "\t-----> $(PACKAGE_NAME):$@"
	@rm -f $(BINARIES) *.o

compile:
	$(ACC) $(CFLAGS) $(LDFLAGS) demo_tda.c -o demo_tda

upload: 
	adb shell rm demo_tda
	adb push demo_tda demo_tda
	adb shell ./demo_tda
