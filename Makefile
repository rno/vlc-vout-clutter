
libdir = $(shell pkg-config --variable=libdir vlc-plugin )
vlclibdir = $(libdir)/vlc/video_output

all: libclutter_plugin.so

libclutter_plugin.so: libclutter_plugin.o
	gcc -shared -std=gnu99 $< `pkg-config  --libs vlc-plugin clutter-0.9`  -Wl,-soname -Wl,$@ -o $@

libclutter_plugin.o: clutter.c
	gcc -c -std=gnu99  $< `pkg-config  --cflags vlc-plugin clutter-0.9` -D__PLUGIN__  -DMODULE_STRING=\"clutter\" -o $@

clean:
	rm -f libclutter_plugin.o libclutter_plugin.so

install: all
	mkdir -p $(DESTDIR)$(vlclibdir)/
	install -m 0755 libclutter_plugin.so $(DESTDIR)$(vlclibdir)/

install-strip: all
	mkdir -p $(DESTDIR)$(vlclibdir)/
	install -s -m 0755 libclutter_plugin.so $(DESTDIR)$(vlclibdir)/

uninstall:
	rm -f -- $(DESTDIR)$(vlclibdir)/libclutter_plugin.so

.PHONY: all clean install uninstall
