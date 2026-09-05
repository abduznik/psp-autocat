TARGET   := favtool
OBJS     := favtool.o

INCDIR  :=
CFLAGS  := -O2 -G0 -Wall
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS := $(CFLAGS)

LIBDIR :=
LIBS   := -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspge

BUILD_PRX = 0

EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := AutoCat Favorites

PSPSDK := $(shell psp-config --pspsdk-path)
PSPBIN := $(PSPSDK)/../bin
include $(PSPSDK)/lib/build.mak
