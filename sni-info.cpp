#include <gio/gio.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <map>
#include <string>
using namespace std::string_literals;

static const auto kde_prefix{"org.kde."s};
static const auto freedesktop_prefix{"org.freedesktop."s};

static const auto item_interface{"StatusNotifierItem"s};

static const auto path_item{"/StatusNotifierItem"s};

static const auto host_base{"org.freedesktop.StatusNotifierHost-"s};
static const auto watcher{"org.kde.StatusNotifierWatcher"s};
static const auto watcher_path{"/StatusNotifierWatcher"s};
static const auto register_method{"RegisterStatusNotifierHost"s};
static const auto registerd_items_prop{"RegisteredStatusNotifierItems"s};

static const auto sig_item_register{"StatusNotifierItemRegistered"s};
static const auto sig_item_unregister{"StatusNotifierItemUnregistered"s};
static std::string host;

class dbus_owner {
 public:
  void own(const std::string& name, const GBusNameAcquiredCallback& acq,
           const GBusNameLostCallback& lost) {
    id = g_bus_own_name(G_BUS_TYPE_SESSION, name.c_str(),
                        G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, acq, lost,
                        nullptr, nullptr);
  }

  void unown() {
    if (id != 0) {
      g_bus_unown_name(id);
      id = 0;
    }
  }

  ~dbus_owner() { unown(); }

 private:
  guint id{0};
};

struct SNItem {
  std::string cat;
  std::string id;
  std::string title;
  std::string status;
  std::string icon_name;
};

static GMainLoop* loop;
static GDBusProxy* proxy = nullptr;

static std::map<std::string, SNItem> items;

static void deregister_item(const std::string& service) {
  items.erase(service);
}

static SNItem& get_item(const std::string& service) {
  return items[service];
}

static void print_items() {
  for (const auto& p : items) {
    const auto& i = p.second;
    std::cout << i.id << ": cat: " << i.cat << ", title: " << i.title
              << ", status: " << i.status << ", iconName: " << i.icon_name
              << std::endl;
  }
}


static std::string get_property_string(GDBusProxy* p, const std::string& prop) {
  auto variant = g_dbus_proxy_get_cached_property(p, prop.c_str());

  if (variant == nullptr) {
    return "";
  }

  std::string str{g_variant_get_string(variant, nullptr)};
  g_variant_unref(variant);
  return str;
}

static void on_item_sig_changed(GDBusProxy* p, gchar* sender_name,
                                gchar* signal_name, GVariant* param,
                                gpointer user_data) {
  std::string sig{signal_name};
  printf("Item Changed Signal received: sender_name: %s, signal_name: %s\n",
         sender_name, signal_name);

  SNItem& item = get_item(sender_name);

  if (sig == "NewTitle") {
    item.title = get_property_string(p, "Title");
  } else if (sig == "NewIcon") {
    item.icon_name = get_property_string(p, "IconName");
  } else if (sig == "NewAttentionIcon") {
    // TODO
  } else if (sig == "NewOverlayIcon") {
    // TODO
  } else if (sig == "NewToolTip") {
    // TODO
  } else if (sig == "NewStatus") {
    item.status = get_property_string(p, "Status");
  } else {
    printf("Unknown item signal received: sender_name: %s, signal_name: %s\n",
           sender_name, signal_name);
  }

  print_items();
}

static void register_item(GDBusProxy* p, const std::string& service,
                          const SNItem&& item) {
  items[service] = std::move(item);

  g_signal_connect(proxy, "g-signal", G_CALLBACK(on_item_sig_changed), nullptr);
}

static SNItem load_item(GDBusProxy* p) {
  SNItem item;
  item.cat = get_property_string(p, "Category");
  item.id = get_property_string(p, "Id");
  item.title = get_property_string(p, "Title");
  item.status = get_property_string(p, "Status");
  item.icon_name = get_property_string(p, "IconName");

  return item;
}

/**
 * Searches on the given bus name for the StatusNotifierItem interface
 *
 * Returns (object path, iface name)
 */
static std::pair<std::string, std::string> find_interface(
    GDBusConnection* c, const std::string& bus_name) {
  std::function<std::string(const std::string&, const std::string&)> finder =
      [&c, &bus_name, &finder](const std::string& iface_name,
                               const std::string& path) -> std::string {
    GVariant* result = g_dbus_connection_call_sync(
        c, bus_name.c_str(), path.empty() ? "/" : path.c_str(),
        "org.freedesktop.DBus.Introspectable", "Introspect", nullptr,
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);

    const gchar* xml_data;
    g_variant_get(result, "(&s)", &xml_data);

    struct node_wrapper {
      ~node_wrapper() {
        if (node) {
          g_dbus_node_info_unref(node);
          node = nullptr;
        }
      }

      GDBusNodeInfo* node = nullptr;
    };

    node_wrapper wrapper;
    GError* error = nullptr;
    auto node = g_dbus_node_info_new_for_xml(xml_data, &error);
    g_variant_unref(result);
    wrapper.node = node;

    if (!node) {
      throw std::runtime_error(error->message);
    }

    for (int i = 0; node->interfaces[i]; i++) {
      GDBusInterfaceInfo* iface = node->interfaces[i];

      if (iface_name == iface->name) {
        std::cout << "Found Interface in " << path << std::endl;
        return path;
      }
    }

    for (int i = 0; node->nodes[i]; i++) {
      GDBusNodeInfo* child = node->nodes[i];

      auto child_path = finder(iface_name, path + '/' + child->path);
      if (!child_path.empty()) {
        return child_path;
      }
    }

    return "";
  };

  auto freedesktop_iface = freedesktop_prefix + item_interface;
  auto kde_iface = kde_prefix + item_interface;

  std::string path = finder(freedesktop_iface, "");

  if (!path.empty()) {
    return {path, freedesktop_iface};
  }

  path = finder(kde_iface, "");

  if (!path.empty()) {
    return {path, kde_iface};
  }

  throw std::runtime_error("StatusNotifierItem interface not found");
}

static GDBusProxy* get_proxy(const std::string& bus_name,
                             const std::string& path,
                             const std::string& iface) {
  auto p = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr, bus_name.c_str(),
      path.c_str(), iface.c_str(), nullptr, nullptr);

  if (p) {
    auto variant = g_dbus_proxy_get_cached_property(p, "Id");

    if (!variant) {
      g_object_unref(p);
      return nullptr;
    }
  }

  return p;
}

/**
 * Initializes a new instance of StatusNotifierItem.
 *
 * The spec states that
 *
 * > Each instance of StatusNotifierItem must provide an object called
 * > StatusNotifierItem
 *
 * However not all implementations follow this.
 *
 * For example libappindicator provides the org.kde.StatusNotifierItem
 * interface at `/org/ayatana/NotificationItem/app-id` where app-id is
 * specific to the application.
 *
 * Also, according to the spec, the StatusNotifierItem interface is called
 * org.freedesktop.StatusNotifierItem, but most implementations will use
 * org.kde.StatusNotifierItem
 *
 * This will try its best to support all these variants
 */
static void init_item(GDBusConnection* c, const std::string& bus_name) {
  GDBusProxy* p =
      get_proxy(bus_name, path_item, freedesktop_prefix + item_interface);

  if (!p) {
    p = get_proxy(bus_name, path_item, kde_prefix + item_interface);
  }

  /*
   * If we can't find the StatusNotifierItem interface at well-known paths, we
   * search the entire node tree for the interface.
   */
  if (!p) {
    std::string path;
    std::string iface;

    std::cout << "Could not find StatusNotifierItem interface at well-known "
                 "path. Searching all objects"
              << std::endl;
    std::tie(path, iface) = find_interface(c, bus_name);
    p = get_proxy(bus_name, path, iface);
  }

  if (!p) {
    throw std::runtime_error("Could not create proxy for StatusNotifierItem");
  }

  register_item(p, bus_name, std::move(load_item(p)));
  g_object_unref(p);
}

static void on_watch_sig_changed(GDBusProxy* p, gchar* sender_name,
                                 gchar* signal_name, GVariant* param,
                                 gpointer user_data) {
  std::string sig{signal_name};
  const gchar* item;

  printf("Signal received: sender_name: %s, signal_name: %s\n", sender_name,
         signal_name);

  if (sig == sig_item_register) {
    g_variant_get(param, "(&s)", &item);
    std::cout << "New Item Registered: " << item << std::endl;
    init_item(g_dbus_proxy_get_connection(p), std::string{item});
  } else if (sig == sig_item_unregister) {
    g_variant_get(param, "(&s)", &item);
    std::cout << "Item Unregistered: " << item << std::endl;
    deregister_item(std::string{item});
  }

  print_items();
}

static void watcher_appeared_handler(GDBusConnection* c, const gchar* name,
                                     const gchar* sender, gpointer user_data) {
  std::cout << name << " appeared" << std::endl;

  proxy = g_dbus_proxy_new_sync(c, G_DBUS_PROXY_FLAGS_NONE, nullptr,
                                watcher.c_str(), watcher_path.c_str(),
                                watcher.c_str(), nullptr, nullptr);

  g_dbus_proxy_call_sync(proxy, register_method.c_str(),
                         g_variant_new("(s)", host.c_str()),
                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);

  GVariant* items =
      g_dbus_proxy_get_cached_property(proxy, registerd_items_prop.c_str());
  GVariantIter* it = g_variant_iter_new(items);
  GVariant* content;
  while ((content = g_variant_iter_next_value(it))) {
    const gchar* it_name = g_variant_get_string(content, NULL);
    std::cout << "Registered Item: " << it_name << std::endl;
    init_item(c, std::string{it_name});
  }
  g_variant_iter_free(it);
  g_variant_unref(items);

  g_signal_connect(proxy, "g-signal", G_CALLBACK(on_watch_sig_changed),
                   nullptr);

  print_items();
}
static void watcher_vanished_handler(GDBusConnection* c, const gchar* name,
                                     gpointer user_data) {
  std::cout << name << " disappeared" << std::endl;
  g_object_unref(proxy);
  g_main_loop_quit(loop);
}

static void on_name_acquired(GDBusConnection* c, const gchar* name,
                             gpointer user_data) {
  std::cout << "Acquired " << name << std::endl;

  guint watcher_id = g_bus_watch_name(
      G_BUS_TYPE_SESSION, watcher.c_str(), G_BUS_NAME_WATCHER_FLAGS_NONE,
      watcher_appeared_handler, watcher_vanished_handler, nullptr, nullptr);
}
static void on_name_lost(GDBusConnection* c, const gchar* name,
                         gpointer user_data) {
  std::cout << "Could not acquire " << name << std::endl;
}

int main(int argc, char* argv[]) {
  host = host_base + std::to_string(::getpid());
  std::cout << "Host: " << host << std::endl;

  loop = g_main_loop_new(nullptr, false);

  dbus_owner owner;
  owner.own(host, &on_name_acquired, &on_name_lost);

  g_main_loop_run(loop);

  owner.unown();
  g_main_loop_unref(loop);
  return 0;
}
