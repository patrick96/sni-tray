CXX = clang++
sni-info: sni-info.cpp
	$(CXX) -g -o $@ $^ -Wall `pkg-config --cflags --libs glibmm-2.4 giomm-2.4`

sni-tray: libgwater/xcb/libgwater-xcb.c draw.c gdbus.c icons.c
	$(CC) -g -o $@ $^ -Wall -lxcb -lxcb-randr -lxcb-util -lxcb-ewmh -lxcb-icccm  `pkg-config --cflags --libs gio-2.0 cairo gdk-pixbuf-2.0`
test-window: draw.c
	$(CC) -g -o $@ $^ -Wall -lxcb -lxcb-randr -lxcb-util -lxcb-ewmh -lxcb-icccm `pkg-config --cflags --libs cairo gdk-pixbuf-2.0`
test-water: libgwater/xcb/libgwater-xcb.c draw.c
	$(CC) -g -o $@ $^ -Wall -lxcb -lxcb-randr -lxcb-util -lxcb-ewmh -lxcb-icccm `pkg-config --cflags --libs cairo gdk-pixbuf-2.0`
test-full: libgwater/xcb/libgwater-xcb.c draw.c gdbus.c icons.c
	$(CC) -g -o $@ $^ -Wall -lxcb -lxcb-randr -lxcb-util -lxcb-ewmh -lxcb-icccm `pkg-config --cflags --libs cairo gdk-pixbuf-2.0 gio-2.0`
clean:
	rm sni-tray test-window test-water
