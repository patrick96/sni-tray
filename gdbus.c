#include "gdbus.h"
/* Side advantages:
 * Menus can be used elsewhere
 * Icons can be specified by name, not path
 */
static void on_watch_sig_changed(GDBusProxy *p, gchar *sender_name, gchar *signal_name, GVariant *param, gpointer user_data);
static inline gboolean apply_cached_prop_string(GDBusProxy *p, const gchar *name, gchar **output);
static void on_item_sig_changed(GDBusProxy *p, gchar *sender_name, gchar *signal_name, GVariant *param, gpointer user_data);
static void init_item_data(const gchar *name, const gchar *path, ItemData *data);
static void watcher_appeared_handler(GDBusConnection *c, const gchar *name, const gchar *sender, gpointer user_data);
static void watcher_vanished_handler(GDBusConnection *c, const gchar *name, gpointer user_data);
static void on_name_acquired(GDBusConnection *c, const gchar *name, gpointer user_data);
static void on_name_lost(GDBusConnection *c, const gchar *name, gpointer user_data);

static gchar host[50] = "org.freedesktop.StatusNotifierHost-";
static const gchar watcher[] = "org.kde.StatusNotifierWatcher";
static const gchar watcher_path[] = "/StatusNotifierWatcher";
//put in ya_bar_t:
static GList *list = NULL;
static gchar *theme = NULL;
//this will be height or something like that
static int size = 24;

static void print_data(ItemData *data) {
	printf("dbus_name: %s\n", data->dbus_name);
	printf("category: %s\n", data->category);
	printf("id: %s\n", data->id);
	printf("title: %s\n", data->title);
	printf("status: %s\n", data->status);
	printf("window_id: %u\n", data->win_id);
	printf("icon_name: %s\n", data->icon_name);
	printf("icon_path: %s\n", data->icon_path);
	printf("overlay_name: %s\n", data->overlay_name);
	printf("att_name: %s\n", data->att_name);
	printf("movie_name: %s\n", data->movie_name);
}
static void on_watch_sig_changed(GDBusProxy *p, gchar *sender_name, gchar *signal_name,
		GVariant *param, gpointer user_data) {
	const gchar *item;
	gchar *just_name;
	if(g_strcmp0(signal_name, "StatusNotifierItemRegistered") == 0) {
		g_variant_get(param, "(&s)", &item);
		just_name = g_strndup(item, g_strstr_len(item, -1, "/") - item);
		printf("Item %s has been registered\n", item);
		//extract param and add it to the list
		ItemData *data = g_new0(ItemData, 1);
		//extract object path from item name
		init_item_data(just_name, g_strstr_len(item, -1, "/"), data);
		list = g_list_prepend(list, data);

	}
	else if(g_strcmp0(signal_name, "StatusNotifierItemUnregistered") == 0) {
		g_variant_get(param, "(&s)", &item);
		just_name = g_strndup(item, g_strstr_len(item, -1, "/") - item);
		printf("Item %s has been unregistered\n", item);
		//remove param from the list
		GList *l = list;
		while(l != NULL) {
			GList *next = l->next;
			ItemData *d = l->data;
			if(g_strcmp0(d->dbus_name, just_name) == 0) {
				list = g_list_remove(user_data, l->data);
			}
			l = next;
		}
	}
}
static gchar * get_property_string(GDBusProxy *p, gchar *prop) {
	gchar *retstr = NULL;
	GVariant *val = g_dbus_proxy_call_sync(p, "org.freedesktop.DBus.Properties.Get",
			g_variant_new("(ss)", "org.kde.StatusNotifierItem", prop), G_DBUS_CALL_FLAGS_NONE,
			-1, NULL, NULL);
	if(val != NULL) {
		GVariant *variant = NULL;
		g_variant_get(val, "(v)", &variant);
		if(variant != NULL) {
			retstr = g_variant_dup_string(variant, NULL);
			printf("%s: %s\n", prop, retstr);
			g_variant_unref(variant);
		}
		g_variant_unref(val);
	}
	return retstr;
}
//TODO check no string mem leaks (excessive duplication)
static inline gboolean apply_cached_prop_string(GDBusProxy *p, const gchar *name, gchar **output) {
	//const gchar *str;
	GVariant *var = g_dbus_proxy_get_cached_property(p, name);
	if(var != NULL) {
		//g_free(output);
		//str = g_variant_get_string(var, NULL);
		//output = (gchar *) g_variant_get_string(var, NULL);
		*output = g_variant_dup_string(var, NULL);
		//printf("%s: '%s'\n", name, str);
		printf("%s: '%s'\n", name, *output);
		//output = g_strdup(str);
		g_variant_unref(var);
		return TRUE;
	}
	output = NULL;
	return FALSE;
}
static inline void ensure_icon_path(GDBusProxy *p, gchar *icon, gchar **output) {
	printf("%s %s\n", icon, *output);
	if((icon != NULL) && (*output == NULL)) {
		*output = find_icon(icon, size, theme);
		printf("%s\n", *output);
	}
}
static inline void apply_cached_prop_pixmap(GDBusProxy *p, const gchar *name, gpointer output) {
	GVariant *var = g_dbus_proxy_get_cached_property(p, name);
	Pixmap *pix = g_new0(Pixmap, 1);
	//gint32 w, h;
	if(var != NULL) {
		GVariantIter *iter;
		g_variant_get(var, "a(iiay)", &iter);
		while(g_variant_iter_loop(iter, "(iiay)", &(pix->width), &(pix->height), pix->pixmap))
			printf("%s: %d x %d\n", name, pix->width, pix->height);
		g_variant_iter_free(iter);
		g_variant_unref(var);
		return;
	}
	output = NULL;
}


static void on_item_sig_changed(GDBusProxy *p, gchar *sender_name, gchar *signal_name,
		GVariant *param, gpointer user_data) {
	GVariant *item = NULL;
	ItemData *data = user_data;
	//const gchar *prop;
	printf("Item %s emitted signal %s\n", sender_name, signal_name);
	if(g_strcmp0(signal_name, "NewTitle") == 0) {
		//apply_cached_prop_string(p, "Title", &(data->title));
		data->title = get_property_string(p, "Title");
		printf("New title: %s\n", data->title);
	}
	else if(g_strcmp0(signal_name, "NewIcon") == 0) {
		//if(apply_cached_prop_string(p, "IconName", &(data->icon_name)))
		if((data->icon_name = get_property_string(p, "IconName")) != NULL)
			ensure_icon_path(p, data->icon_name, &(data->icon_path));
		//apply_cached_prop_string(p, "IconThemePath", data->icon_path);
		apply_cached_prop_pixmap(p, "IconPixmap", &(data->icon_pixmap));
		printf("New icon name: %s\n", data->icon_name);
	}
	else if(g_strcmp0(signal_name, "NewAttentionIcon") == 0) {
		//maybe check for pixmap and/or movie too
		data->att_name = get_property_string(p, "AttentionIconName");
		//apply_cached_prop_string(p, "AttentionIconName", &(data->att_name));
		printf("New attention icon name: %s\n", data->att_name);
	}
	else if(g_strcmp0(signal_name, "NewOverlayIcon") == 0) {
		//maybe check for pixmap too
		data->overlay_name = get_property_string(p, "OverlayIconName");
		//apply_cached_prop_string(p, "OverlayIconName", &(data->overlay_name));
		printf("New overlay icon name: %s\n", data->overlay_name);
	}
	else if(g_strcmp0(signal_name, "NewToolTip") == 0) {
		/*
		g_variant_get(item, "(&s)", &prop);
		printf("New tooltip: %s\n", prop);
		*/
	}
	else if(g_strcmp0(signal_name, "NewStatus") == 0) {
		data->status = get_property_string(p, "Status");
		//apply_cached_prop_string(p, "OverlayIconName", &(data->status));
		printf("New status: %s\n", data->status);
	}
	g_free(item);

}

static void init_item_data(const gchar *name, const gchar *path, ItemData *data) {
	printf("name: %s, path: %s\n", name, path);
	GDBusProxy *proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
			G_DBUS_PROXY_FLAGS_NONE, NULL, name, path,
			//G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES, NULL, name, path,
			"org.kde.StatusNotifierItem", NULL, NULL);
	g_signal_connect(proxy, "g-signal", G_CALLBACK(on_item_sig_changed), data);

	data->dbus_name = g_strdup(name);

	//apply_cached_prop_string(proxy, "Category", &(data->category));
	data->category = get_property_string(proxy, "Category");
	//apply_cached_prop_string(proxy, "Id", &(data->id));
	data->id = get_property_string(proxy, "Id");
	//apply_cached_prop_string(proxy, "Title", &(data->title));
	data->title = get_property_string(proxy, "Title");
	//apply_cached_prop_string(proxy, "Status", &(data->status));
	data->status = get_property_string(proxy, "Status");
	//windowid
	//if(apply_cached_prop_string(proxy, "IconName", &(data->icon_name))) {
	//	ensure_icon_path(proxy, data->icon_name, &(data->icon_path));
	//}
	printf("%s\n", get_property_string(proxy, "IconName"));
	if((data->icon_name = get_property_string(proxy, "IconName")) != NULL) {
		printf("%s\n", data->icon_name);
		ensure_icon_path(proxy, data->icon_name, &(data->icon_path));
	}
	//apply_cached_prop_string(proxy, "IconThemePath", &(data->theme_path));
	data->theme_path = get_property_string(proxy, "IconThemePath");
	apply_cached_prop_pixmap(proxy, "IconPixmap", &(data->icon_pixmap));
	//data->icon_pixmap = g_strdup(get_property_string(proxy, "IconPixmap"));
	//apply_cached_prop_string(proxy, "OverlayIconName", &(data->overlay_name));
	data->overlay_name = get_property_string(proxy, "OverlayIconName");
	//apply_cached_prop_string(proxy, "AttentionIconName", &(data->att_name));
	data->att_name = get_property_string(proxy, "AttentionIconName");
	//apply_cached_prop_string(proxy, "AttentionMovieName", &(data->movie_name));
	data->movie_name = get_property_string(proxy, "AttentionMovieName");
	//tooltip
	print_data(data);

}

static void watcher_appeared_handler(GDBusConnection *c, const gchar *name, const gchar *sender, gpointer user_data) {
	GDBusProxy *proxy;
	g_dbus_connection_call_sync(c, watcher, watcher_path, watcher, "RegisterStatusNotifierHost",
			g_variant_new("(s)", host), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);

	proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL, watcher,
			watcher_path, watcher, NULL, NULL);

	g_signal_connect(proxy, "g-signal", G_CALLBACK(on_watch_sig_changed), user_data);

	//initialize the list
	GVariant *items = g_dbus_proxy_get_cached_property(proxy, "RegisteredStatusNotifierItems");
	GVariantIter *it = g_variant_iter_new(items);
	GVariant *content;
	ItemData *data;
	int i = 0;
	while((content = g_variant_iter_next_value(it))) {
		const gchar *it_name = g_variant_get_string(content, NULL);
		gchar *just_name = g_strndup(it_name, g_strstr_len(it_name, -1, "/") - it_name);
		printf("%d: %s\n", i, it_name);
		data = g_new0(ItemData, 1);
		//extract object path from item name
		init_item_data(just_name, g_strstr_len(it_name, -1, "/"), data);
		list = g_list_prepend(list, data);
		i++;
	}
	g_variant_iter_free(it);
	g_variant_unref(items);

}

static void watcher_vanished_handler(GDBusConnection *c, const gchar *name, gpointer user_data) {
	printf("Watcher is nowhere to be found... rip\n");
	exit(1);
}
static void on_name_acquired(GDBusConnection *c, const gchar *name, gpointer user_data) {
	printf("I am acquired\n");
	guint watcher_id = g_bus_watch_name(G_BUS_TYPE_SESSION, watcher, G_BUS_NAME_OWNER_FLAGS_NONE,
			watcher_appeared_handler, watcher_vanished_handler, user_data, NULL);
}
static void on_name_lost(GDBusConnection *c, const gchar *name, gpointer user_data) {
	printf("Couldn't get name\n");
	exit(1);
}

int main() {
	theme = get_icon_theme();
	printf("%s\n", theme);
	GMainLoop *loop;
	guint id;
	sprintf(host + strlen(host), "%ld", (long) getpid());
	printf("name: %s\n", host);

	loop = g_main_loop_new(NULL, FALSE);
	id = g_bus_own_name(G_BUS_TYPE_SESSION, (const gchar *) host,
			G_BUS_NAME_OWNER_FLAGS_NONE, NULL, on_name_acquired, on_name_lost, NULL, NULL);

	g_main_loop_run(loop);

	g_bus_unown_name(id);
	g_main_loop_unref(loop);
	return 0;
}
