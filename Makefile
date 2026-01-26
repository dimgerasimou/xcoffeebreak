CC       ?= cc
CFLAGS   ?= -Wall -Wextra -Wno-deprecated-declarations -Os
LDLIBS   ?= -lX11 -lXss

BINDIR   := bin
OBJDIR   := obj

BIN      := xcoffeebreak
SRCS     := xcoffeebreak.c mpris.c utils.c args.c
OBJS     := $(SRCS:%.c=$(OBJDIR)/%.o)
TARGET   := $(BINDIR)/$(BIN)

PKG      := dbus-1
CPPFLAGS += $(shell pkg-config --cflags $(PKG) 2>/dev/null)
LDLIBS   += $(shell pkg-config --libs   $(PKG) 2>/dev/null)

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	@echo "LD $(OBJS) -> $@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

$(OBJDIR)/%.o: %.c mpris.h utils.h args.h | $(OBJDIR)
	@echo "CC $< -> $@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	@mkdir -p $@

clean:
	@echo "RM $(BINDIR) $(OBJDIR)"
	@rm -rf $(BINDIR) $(OBJDIR)

.PHONY: all clean
