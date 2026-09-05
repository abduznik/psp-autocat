TARGET   := favtool
OBJS     := favtool.o autocat.o sfo.o classify.o genre.o isocd.o cso.o

VPATH   := ../src
INCDIR  := ../src
CFLAGS  := -O2 -G0 -Wall
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS := $(CFLAGS)

LIBDIR :=
LIBS   := -lpspdebug -lpspdisplay -lpspge -lpspctrl -lz

BUILD_PRX = 0

EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := AutoCat Favorites

PSPSDK := $(shell psp-config --pspsdk-path)
PSPBIN := $(PSPSDK)/../bin
include $(PSPSDK)/lib/build.mak
