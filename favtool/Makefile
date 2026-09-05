TARGET   := favtool
OBJS     := favtool.o autocat.o sfo.o classify.o genre.o isocd.o cso.o movedriver-stub.o

VPATH   := ../src
INCDIR  := ../src
CFLAGS  := -O2 -G0 -Wall
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS := $(CFLAGS)

LIBDIR :=
# -lpspkubridge: kuKernelLoadModule, for loading movedriver.prx (a
# kernel-mode helper for privileged sceIoRename) from this user-mode
# homebrew on demand. Ships as a standard part of the PSPSDK.
LIBS   := -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspkubridge -lz

BUILD_PRX = 0

EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := AutoCat Favorites

PSPSDK := $(shell psp-config --pspsdk-path)
PSPBIN := $(PSPSDK)/../bin
include $(PSPSDK)/lib/build.mak
