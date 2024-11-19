# makefile per epcore
# $Id: $

PRODUCT := epxcore

# standard directories according to GNU Makefile conventions
MKDIR ?= mkdir -p
RM ?= rm
MV ?= mv
SYMLINK ?= ln -sfn
INSTALL ?= install
RANLIB ?= ranlib
prefix ?= /opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi/usr
libdir ?= $(prefix)/lib
includedir ?= $(prefix)/include
CROSS := /opt/poky/2.1.1/sysroots/x86_64-pokysdk-linux/usr/bin/arm-poky-linux-gnueabi/

FIND ?= find
TAR ?= tar
SED ?= sed

BUILDDIR := _bins
# prepare dependencies file
DEPENDS := _depends$

CC := $(CROSS)arm-poky-linux-gnueabi-gcc -march=armv7-a -marm -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9 --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi -Wl,-build-id=none
CXX := $(CROSS)arm-poky-linux-gnueabi-g++ -march=armv7-a -marm -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9 --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi -Wl,-build-id=none
LD := $(CROSS)arm-poky-linux-gnueabi-ld --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi
CFLAGS := --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi -fno-short-enums -specs=Xilinx.spec -Wl,-T -Wl,../lscript.ld
LDFLAGS := --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi -fno-short-enums
CPPFLAGS := --sysroot=/opt/poky/2.1.1/sysroots/cortexa9hf-neon-poky-linux-gnueabi

# warning selection 
WARNINGS := \
	-Wall \
	-Wno-unused-result \
	-Wno-unused-but-set-variable \
	-Wpointer-arith \
	-Wno-cast-qual \
	-Wcast-align \
	-Wwrite-strings \
	-Wsign-compare \
	-Wfloat-equal \
	-Wendif-labels \
	-Wshadow

WARNINGS += -Wextra

LDLIBS:=-L$(libdir)
LDLIBS+=-lpthread -lrt -lm
LDLIBS+=../liblog/liblog.a
LDLIBS+=../libep/libep.a
#LDLIBS+=/home/berta/Vivado/EPControl/EPControl.sdk/standalone_bsp_0/ps7_cortexa9_0/lib/libxil.a

# makefile name detection

MAINMAKEFILE = $(firstword $(MAKEFILE_LIST))

HEADERS := $(shell $(FIND) . -maxdepth 1 -name '*.h')
SOURCES := $(shell $(FIND) . -maxdepth 1 -name '*.c' -printf '%p \n' | sort)
OBJECTS := $(addprefix $(BUILDDIR)/,$(notdir $(SOURCES:.c=.o)))

MYFLAGS := $(WARNINGS) -D_REENTRANT -D_FILE_OFFSET_BITS=64 -pipe -O3 
MYCPPFLAGS := -I$(includedir) $(CPPFLAGS) 
MYCXXFLAGS := $(WARNINGSCXX) -D_REENTRANT -D_FILE_OFFSET_BITS=64 -pipe -O3 $(CXXFLAGS)
MYCFLAGS := $(MYFLAGS) $(CFLAGS) -std=gnu9x
MYLDFLAGS := $(MYFLAGS) $(LDFLAGS)

# product full name composition

APPLICATION := $(PRODUCT)
APPLICATIONV := z$(PRODUCT)

# default compile rules
$(BUILDDIR)/%.o : %.c
	$(CC) -c  $(MYCPPFLAGS) $(MYCFLAGS) $< -o $@

.PHONY: all
all: init
	$(MAKE) -f $(MAINMAKEFILE) depend
	$(MAKE) -f $(MAINMAKEFILE) app


.PHONY: init
init:

	touch $(PRODUCT).c
	$(MKDIR) $(BUILDDIR)

.PHONY: depend
depend: init $(SOURCES) $(HEADERS)
	$(CC) -DMKSYS_DEPENDS -MM $(MYCPPFLAGS) $(MYCXXFLAGS) $(SOURCES) | $(SED) -r '/^[^ ]+\.o/s/^/$(BUILDDIR)\//' > $(DEPENDS)


-include $(DEPENDS)


# app
.PHONY: app
app: $(APPLICATIONV)
$(APPLICATIONV) : $(OBJECTS)
	$(CC) $(MYLDFLAGS) -o $@ $^ $(LDLIBS)
	cd .; $(SYMLINK) $(APPLICATIONV) $(APPLICATION)


.PHONY: clean
clean:
	$(RM) -r ./$(BUILDDIR)/
	$(RM) $(APPLICATIONV) $(APPLICATION)

.PHONY: FORCE
FORCE:






