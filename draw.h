#pragma once

#include <xcb/xcb.h>
#include <cairo/cairo-xcb.h>
#include <gio/gio.h>
#include "libgwater/xcb/libgwater-xcb.h"

extern xcb_connection_t *c;

typedef struct {
	uint8_t r, g, b, a;
} rgba_t;

enum click_type {
	PRIMARY = 1,
	SECONDARY,
	CONTEXT,
	UNUSED,
	SCROLL
};
gboolean callback(xcb_generic_event_t *event, gpointer user_data);
void draw_tray();
void init_window();
