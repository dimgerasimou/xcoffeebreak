# xcoffeebreak - dynamic window manager
# See LICENSE file for copyright and license details.

VERSION  ?= 0.1.0
CC       ?= cc
CFLAGS   ?= -Wall -Wextra -Wno-deprecated-declarations -Os
CPPFLAGS += -MMD -MP -DVERSION=\"${VERSION}\"
LDLIBS   ?= -lX11 -lXss

PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
BINDIR    := bin
OBJDIR    := obj

BIN      := xcoffeebreak
SRCS     := xcoffeebreak.c mpris.c utils.c args.c state.c x.c
OBJS     := $(SRCS:%.c=$(OBJDIR)/%.o)
DEPS     := $(OBJS:.o=.d)
TARGET   := $(BINDIR)/$(BIN)

PKG        := dbus-1
PKG_CONFIG ?= pkg-config
CPPFLAGS   += $(shell $(PKG_CONFIG) --cflags $(PKG) 2>/dev/null)
LDLIBS     += $(shell $(PKG_CONFIG) --libs   $(PKG) 2>/dev/null)

COLOR  ?= 1
PRINTF ?= printf

ifeq ($(COLOR),0)
COLOR_RESET  :=
COLOR_GREEN  :=
COLOR_YELLOW :=
COLOR_BLUE   :=
COLOR_CYAN   :=
else
COLOR_RESET  := \033[0m
COLOR_GREEN  := \033[1;32m
COLOR_YELLOW := \033[1;33m
COLOR_BLUE   := \033[1;34m
COLOR_CYAN   := \033[1;36m
endif

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	@$(PRINTF) "$(COLOR_GREEN)Linking:$(COLOR_RESET) %s\n" "$@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@$(PRINTF) "$(COLOR_BLUE)Compiling:$(COLOR_RESET) %s\n" "$@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	@mkdir -p $@

clean:
	@$(PRINTF) "$(COLOR_YELLOW)Cleaning:$(COLOR_RESET) %s %s\n" "$(BINDIR)" "$(OBJDIR)"
	@rm -rf $(BINDIR) $(OBJDIR)

install: $(TARGET)
	@$(PRINTF) "$(COLOR_CYAN)Installing $(BIN) at:$(COLOR_RESET) %s\n" "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -d $(DESTDIR)$(MANPREFIX)/man1
	@install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@sed "s/VERSION/$(VERSION)/g" < $(BIN).1 > $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1
	@chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

uninstall:
	@$(PRINTF) "$(COLOR_CYAN)Uninstalling $(BIN) from:$(COLOR_RESET) %s\n" "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	@rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

-include $(DEPS)

.PHONY: all clean install uninstall
