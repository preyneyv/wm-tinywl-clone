
PKG_CONFIG?=pkg-config
WAYLAND_PROTOCOLS!=$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols
WAYLAND_SCANNER!=$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner

PKGS=wayland-server xkbcommon
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
LIBS!=$(PKG_CONFIG) --libs $(PKGS)

WLROOTS_B=/root/projects/wlroots-b/usr/local
CFLAGS+=-I$(WLROOTS_B)/include/wlroots-0.19
LIBS+=-L$(WLROOTS_B)/lib -lwlroots-0.19

all: twl-clone

run: twl-clone
	LD_LIBRARY_PATH=$(WLROOTS_B)/lib ./twl-clone

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

twl-clone.o: twl-clone.c xdg-shell-protocol.h
	$(CC) -c $< -g -Werror $(CFLAGS) -I. -DWLR_USE_UNSTABLE -o $@

twl-clone: twl-clone.o
	$(CC) $^ $> -g -Werror $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f twl-clone twl-clone.o xdg-shell-protocol-h

.PHONY: all clean run

