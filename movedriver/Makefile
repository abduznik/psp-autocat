TARGET   := movedriver
OBJS     := movedriver.o

CFLAGS   := -O2 -G0 -Wall
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  := $(CFLAGS)

LIBDIR :=
LIBS   := -lpspsystemctrl_kernel

BUILD_PRX    = 1
PRX_EXPORTS  := exports.exp

USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

# Generates movedriver.S, the import stub favtool links against to call
# into this PRX once it's loaded — same mechanism CMFileManager PSP's
# fs_driver uses (psp-build-exports -s on the same exports.exp that
# defines what this module exports).
stub:
	psp-build-exports -s $(PRX_EXPORTS)
	cp movedriver.S ../favtool/movedriver-stub.S
