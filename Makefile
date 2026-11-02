# WideStrike - widescreen + stability patch for Joint Strike Fighter (1997)
#
#   Build (MSYS2 MINGW32 shell, or plain make with the 32 bit gcc on PATH):
#       make            -> build/glide2x.dll
#       make clean
#
#   The output must be a 32 bit DLL named glide2x.dll: jsf.exe links that
#   library statically, so the loader maps us in before any game code runs.

CC      ?= gcc
TARGET   = build/glide2x.dll
SRCDIR   = src
OBJDIR   = build/obj

CFLAGS  := -m32 -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter \
           -fno-stack-protector -fno-strict-aliasing \
           -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0501
LDFLAGS := -m32 -shared -static-libgcc -Wl,--export-all-symbols \
           -Wl,--enable-stdcall-fixup -Wl,--subsystem,windows

SRCS_C  := $(wildcard $(SRCDIR)/*.c)
SRCS_S  := $(wildcard $(SRCDIR)/*.S)
OBJS    := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS_C)) \
           $(patsubst $(SRCDIR)/%.S,$(OBJDIR)/%.o,$(SRCS_S))

all: $(TARGET)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/wstrike.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.S | $(OBJDIR)
	$(CC) -m32 -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(dir $(TARGET))
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo
	@echo "built  $(TARGET)"
	@echo "copy it next to jsf.exe together with wstrike.ini"

clean:
	@rm -rf $(OBJDIR) $(TARGET) build/stubs build/*.S

# ---------------------------------------------------------------------------
# optional compatibility stubs for installs that are missing the original
# third party DLLs jsf.exe links against (winplay, winstr, IFORCE2, DPLAYX).
# The Windows loader resolves every static import *before* it runs any
# DllMain, so the game cannot start - and WideStrike cannot patch anything -
# until all of them are present.
#
# Movie playback, force feedback and the DirectPlay lobby are dead with these.
# Only install them where the originals are missing.
# ---------------------------------------------------------------------------
STUBDIR = build/stubs
STUBFLAGS = -m32 -shared -nostartfiles -static-libgcc -fno-stack-protector \
            -Wl,--export-all-symbols -Wl,--subsystem,windows

stubs: $(STUBDIR)
	$(CC) $(STUBFLAGS) -O2 stubs/winplay.c -o $(STUBDIR)/winplay.dll
	$(CC) $(STUBFLAGS) -O2 stubs/winstr.c  -o $(STUBDIR)/winstr.dll
	$(CC) $(STUBFLAGS) stubs/iforce2.S      -o $(STUBDIR)/IFORCE2.dll
	$(CC) $(STUBFLAGS) stubs/dplayx.S stubs/dplayx.def -o $(STUBDIR)/DPLAYX.dll
	@echo "stubs written to $(STUBDIR)"

$(STUBDIR):
	@mkdir -p $(STUBDIR)

.PHONY: all clean stubs
