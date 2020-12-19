#include <gio/gio.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <string>

static const std::string host_base{"org.freedesktop.StatusNotifierHost-"};
static const std::string watcher{"org.kde.StatusNotifierWatcher"};
static const std::string watcher_path{"/StatusNotifierWatcher"};
static const std::string register_method{"RegisterStatusNotifierHost"};
static const std::string registerd_items_prop{"RegisteredStatusNotifierItems"};

static const std::string sig_item_register{"StatusNotifierItemRegistered"};
static const std::string sig_item_unregister{"StatusNotifierItemUnregistered"};
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

static void register_item(const std::string& service, const SNItem&& item) {
  items[service] = std::move(item);
}

static std::string get_property_string(GDBusProxy* p, const std::string& prop) {
  auto variant = g_dbus_proxy_get_cached_property(p, prop.c_str());
  std::string str{g_variant_get_string(variant, nullptr)};
  g_variant_unref(variant);
  return str;
}

static SNItem load_item(const std::string& service) {
  std::string name = service.substr(0, service.find('/'));

  GDBusProxy* p = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
      name.c_str(), "/StatusNotifierItem", "org.kde.StatusNotifierItem",
      nullptr, nullptr);

  SNItem item;
  item.cat = get_property_string(p, "Category");
  item.id = get_property_string(p, "Id");
  item.title = get_property_string(p, "Title");
  item.status = get_property_string(p, "Status");
  item.icon_name = get_property_string(p, "IconName");

  return item;
}

static void print_items() {
  for (const auto& p : items) {
    const auto& i = p.second;
    std::cout << i.id << ": cat: " << i.cat << ", title: " << i.title
              << ", status: " << i.status << ", iconName: " << i.icon_name
              << std::endl;
  }
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
    std::string service{item};
    // TODO add signal handler for this item
    register_item(service, std::move(load_item(service)));
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
    std::string service{it_name};
    register_item(service, std::move(load_item(service)));
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
