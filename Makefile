export RELVER := 1.0

TARGET   := autocat
BUILD_DIR := build
SRC_DIR  := src

OBJS := $(SRC_DIR)/main.o \
        $(SRC_DIR)/autocat.o \
        $(SRC_DIR)/sfo.o \
        $(SRC_DIR)/classify.o

INCDIR := $(SRC_DIR)
CFLAGS := -O2 -G0 -Wall \
          -Wno-implicit-function-declaration \
          -Wno-incompatible-pointer-types \
          -Wno-int-conversion \
          -Wno-int-to-pointer-cast
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS  := $(CFLAGS)

LDFLAGS := -nostartfiles

LIBDIR :=
LIBS   :=

BUILD_PRX   = 1
PRX_EXPORTS := exports.exp

USE_USER_LIBS = 1
USE_USER_LIBC = 1

PSPSDK := $(shell psp-config --pspsdk-path 2>/dev/null)
# Non-fatal: allows host-side 'make test'/'make pack' without pspdev
-include $(PSPSDK)/lib/build.mak

release: clean all
	@echo "==> Built $(TARGET).prx (v$(RELVER))"

# ── Host unit tests (plain gcc, no PSP SDK) ──────────────
TEST_CC ?= cc
TEST_SRC := test/test_classify.c test/test_sfo.c src/sfo.c src/classify.c

test: $(TEST_SRC)
	$(TEST_CC) -Isrc -o test/test_classify test/test_classify.c src/sfo.c src/classify.c
	$(TEST_CC) -Isrc -o test/test_sfo test/test_sfo.c src/sfo.c
	./test/test_classify
	./test/test_sfo

# ── Host-side release packaging (zip on CI runner) ───────
# NOTE: pack must NOT depend on all — CI builds the PRX in Docker first,
# then runs pack on the host which has no pspdev SDK.
pack:
	mkdir -p $(BUILD_DIR)/temp/seplugins
	cp $(TARGET).prx $(BUILD_DIR)/temp/seplugins/
	echo "ms0:/seplugins/$(TARGET).prx 1" > $(BUILD_DIR)/temp/seplugins/vsh.txt
	cp README.md $(BUILD_DIR)/temp/
	cd $(BUILD_DIR)/temp && zip -r ../$(TARGET)-$(RELVER).zip *
	@echo "==> Packaged $(BUILD_DIR)/$(TARGET)-$(RELVER).zip"