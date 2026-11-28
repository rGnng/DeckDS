#include "wifi.hpp"

#if defined(__linux__)
#include <filesystem>
#include <fstream>
#endif

bool isWifiConnected()
{
#if defined(__linux__)
    namespace fs = std::filesystem;
    std::error_code directoryError;
    for (fs::directory_iterator it("/sys/class/net", directoryError), end;
         !directoryError && it != end; it.increment(directoryError)) {
        const auto& path = it->path();
        std::error_code interfaceError;
        if (!fs::exists(path / "phy80211", interfaceError) && !fs::is_directory(path / "wireless", interfaceError))
            continue;
        int carrier = 0;
        std::ifstream link(path / "carrier");
        if (link >> carrier && carrier == 1)
            return true;
    }
#endif
    return false;
}

#if defined(DECKDS_NETWORK_MANAGER)
#include <gio/gio.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

namespace {
using Bus = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
constexpr auto manager = "org.freedesktop.NetworkManager";
constexpr auto managerPath = "/org/freedesktop/NetworkManager";
constexpr auto deviceInterface = "org.freedesktop.NetworkManager.Device";
constexpr auto wireless = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto accessPoint = "org.freedesktop.NetworkManager.AccessPoint";
constexpr auto activeConnection = "org.freedesktop.NetworkManager.Connection.Active";
constexpr auto savedConnection = "org.freedesktop.NetworkManager.Settings.Connection";
constexpr auto properties = "org.freedesktop.DBus.Properties";
constexpr guint32 wifiDeviceType = 2, deviceDisconnected = 30, deviceActivated = 100;
constexpr guint32 activeActivated = 2, activeDeactivating = 3;
constexpr guint32 privacy = 0x001, psk = 0x100, sae = 0x400;
using namespace std::chrono_literals;

Variant call(GDBusConnection* bus, const char* path, const char* interface, const char* method,
             const char* resultType, GVariant* arguments = nullptr, int timeout = 1500)
{
    return {g_dbus_connection_call_sync(bus, manager, path, interface, method, arguments,
               G_VARIANT_TYPE(resultType), G_DBUS_CALL_FLAGS_NONE, timeout, nullptr, nullptr), g_variant_unref};
}

Variant property(GDBusConnection* bus, const char* path, const char* interface,
                 const char* name, const char* type)
{
    const auto reply = call(bus, path, properties, "Get", "(v)", g_variant_new("(ss)", interface, name));
    GVariant* value = nullptr;
    if (reply)
        g_variant_get(reply.get(), "(v)", &value);
    Variant result(value, g_variant_unref);
    if (result && !g_variant_is_of_type(result.get(), G_VARIANT_TYPE(type)))
        result.reset();
    return result;
}

guint32 unsignedProperty(GDBusConnection* bus, const char* path, const char* interface, const char* name)
{
    const auto value = property(bus, path, interface, name, "u");
    return value ? g_variant_get_uint32(value.get()) : 0;
}

std::string wifiDevice(GDBusConnection* bus)
{
    const auto reply = call(bus, managerPath, manager, "GetDevices", "(ao)");
    if (!reply)
        return {};
    const Variant devices(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    std::string available;
    GVariantIter entries;
    const char* device = nullptr;
    g_variant_iter_init(&entries, devices.get());
    while (g_variant_iter_next(&entries, "&o", &device)) {
        if (unsignedProperty(bus, device, deviceInterface, "DeviceType") != wifiDeviceType)
            continue;
        const auto state = unsignedProperty(bus, device, deviceInterface, "State");
        if (state == deviceActivated)
            return device;
        if (available.empty() && state >= deviceDisconnected)
            available = device;
    }
    return available;
}

gint64 lastScan(GDBusConnection* bus, const char* device)
{
    const auto value = property(bus, device, wireless, "LastScan", "x");
    return value ? g_variant_get_int64(value.get()) : -1;
}

WifiNetwork::Security securityOf(GVariant* fields)
{
    guint32 flags = 0, wpa = 0, rsn = 0;
    g_variant_lookup(fields, "Flags", "u", &flags);
    g_variant_lookup(fields, "WpaFlags", "u", &wpa);
    g_variant_lookup(fields, "RsnFlags", "u", &rsn);
    using Security = WifiNetwork::Security;
    if ((wpa | rsn) & psk)
        return Security::Personal;
    if (rsn & sae)
        return Security::Sae;
    return !(flags & privacy) && !(wpa | rsn) ? Security::Open : Security::Other;
}

Variant accessPointFields(GDBusConnection* bus, const char* path)
{
    const auto reply = call(bus, path, properties, "GetAll", "(a{sv})", g_variant_new("(s)", accessPoint));
    return {reply ? g_variant_get_child_value(reply.get(), 0) : nullptr, g_variant_unref};
}

std::string_view ssidBytes(GVariant* ssid)
{
    if (!ssid)
        return {};
    gsize length = 0;
    const auto* bytes = static_cast<const char*>(g_variant_get_fixed_array(ssid, &length, 1));
    return bytes ? std::string_view(bytes, length) : std::string_view{};
}

WifiResult wifiError(std::string message)
{
    return {.message = std::move(message), .error = true};
}
}

bool wifiSetupSupported() { return true; }

std::optional<WifiConnection> activeWifi()
{
    const Bus bus(g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr), g_object_unref);
    const auto device = bus ? wifiDevice(bus.get()) : std::string{};
    if (device.empty())
        return std::nullopt;
    const auto active = property(bus.get(), device.c_str(), wireless, "ActiveAccessPoint", "o");
    const char* path = active ? g_variant_get_string(active.get(), nullptr) : "/";
    if (std::string_view(path) == "/")
        return std::nullopt;
    WifiConnection connection{.name = {}, .id = path, .strength = std::nullopt};
    const auto fields = accessPointFields(bus.get(), path);
    if (!fields)
        return connection;
    const Variant ssid(g_variant_lookup_value(fields.get(), "Ssid", G_VARIANT_TYPE("ay")), g_variant_unref);
    if (const auto bytes = ssidBytes(ssid.get()); !bytes.empty()) {
        const std::unique_ptr<gchar, decltype(&g_free)> name(g_utf8_make_valid(bytes.data(), bytes.size()), g_free);
        connection.name = name.get();
    }
    guchar strength = 0;
    if (g_variant_lookup(fields.get(), "Strength", "y", &strength))
        connection.strength = std::min<int>(strength, 100);
    return connection;
}

WifiResult scanWifi()
{
    const Bus bus(g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr), g_object_unref);
    const auto device = bus ? wifiDevice(bus.get()) : std::string{};
    if (device.empty())
        return wifiError("No active Wi-Fi adapter managed by NetworkManager.");

    const auto before = lastScan(bus.get(), device.c_str());
    const auto scan = call(bus.get(), device.c_str(), wireless, "RequestScan", "()",
                          g_variant_new("(a{sv})", nullptr));
    bool fresh = false;
    if (scan) {
        for (int attempt = 0; attempt < 20 && !fresh; ++attempt) {
            std::this_thread::sleep_for(250ms);
            const auto last = lastScan(bus.get(), device.c_str());
            fresh = last >= 0 && last != before;
        }
    }
    const auto reply = call(bus.get(), device.c_str(), wireless, "GetAllAccessPoints", "(ao)");
    if (!reply)
        return wifiError("NetworkManager could not list Wi-Fi networks.");
    const auto active = property(bus.get(), device.c_str(), wireless, "ActiveAccessPoint", "o");
    const std::string_view connected = active ? g_variant_get_string(active.get(), nullptr) : "";
    const Variant points(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    WifiResult result;
    GVariantIter entries;
    const char* path = nullptr;
    g_variant_iter_init(&entries, points.get());
    while (g_variant_iter_next(&entries, "&o", &path)) {
        const auto fields = accessPointFields(bus.get(), path);
        if (!fields)
            continue;
        const Variant ssid(g_variant_lookup_value(fields.get(), "Ssid", G_VARIANT_TYPE("ay")), g_variant_unref);
        const auto bytes = ssidBytes(ssid.get());
        if (bytes.empty())
            continue;
        const std::unique_ptr<gchar, decltype(&g_free)> name(g_utf8_make_valid(bytes.data(), bytes.size()), g_free);
        guchar strength = 0;
        g_variant_lookup(fields.get(), "Strength", "y", &strength);
        WifiNetwork network{path, name.get(), securityOf(fields.get()), strength, connected == path, std::string(bytes)};
        const auto same = std::ranges::find_if(result.networks, [&](const WifiNetwork& other) {
            return other.ssidBytes == network.ssidBytes && other.security == network.security;
        });
        if (same == result.networks.end())
            result.networks.push_back(std::move(network));
        else if (!same->connected && (network.connected || network.strength > same->strength))
            *same = std::move(network);
    }
    std::ranges::sort(result.networks, [](const WifiNetwork& left, const WifiNetwork& right) {
        if (left.connected != right.connected)
            return left.connected;
        if (left.strength != right.strength)
            return left.strength > right.strength;
        return left.name < right.name;
    });
    if (result.networks.empty())
        return wifiError(fresh ? "No visible Wi-Fi networks." : "NetworkManager could not scan for Wi-Fi networks.");
    if (!fresh)
        result.message = "Showing networks from the last scan.";
    return result;
}

WifiResult connectWifi(const WifiNetwork& network, const std::string& password)
{
    const Bus bus(g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr), g_object_unref);
    const auto device = bus ? wifiDevice(bus.get()) : std::string{};
    if (device.empty())
        return wifiError("No active Wi-Fi adapter managed by NetworkManager.");
    const auto fields = accessPointFields(bus.get(), network.id.c_str());
    if (!fields)
        return wifiError("This network is no longer visible. Scan again.");
    const Variant ssid(g_variant_lookup_value(fields.get(), "Ssid", G_VARIANT_TYPE("ay")), g_variant_unref);
    const auto security = securityOf(fields.get());
    if (!ssid || ssidBytes(ssid.get()) != network.ssidBytes || security != network.security)
        return wifiError("The network changed. Scan again.");

    using Security = WifiNetwork::Security;
    if (security == Security::Other)
        return wifiError("This network uses an unsupported security type.");
    if (security != Security::Open && (password.empty() || !g_utf8_validate(password.data(), password.size(), nullptr)))
        return wifiError("Enter a Wi-Fi password.");
    if (security == Security::Personal) {
        const bool hexKey = password.size() == 64 && std::ranges::all_of(password, [](char character) {
            return std::string_view("0123456789abcdefABCDEF").find(character) != std::string_view::npos;
        });
        if (!hexKey && (password.size() < 8 || password.size() > 63))
            return wifiError("Enter an 8–63 byte Wi-Fi password or a 64-digit hex key.");
    }
    GVariantBuilder settings, wifi;
    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_init(&wifi, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&wifi, "{sv}", "ssid", ssid.get());
    g_variant_builder_add(&settings, "{sa{sv}}", "802-11-wireless", &wifi);
    if (security != Security::Open) {
        GVariantBuilder credentials;
        g_variant_builder_init(&credentials, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&credentials, "{sv}", "key-mgmt",
                              g_variant_new_string(security == Security::Sae ? "sae" : "wpa-psk"));
        g_variant_builder_add(&credentials, "{sv}", "psk", g_variant_new_string(password.c_str()));
        g_variant_builder_add(&settings, "{sa{sv}}", "802-11-wireless-security", &credentials);
    }
    const auto reply = call(bus.get(), managerPath, manager, "AddAndActivateConnection", "(oo)",
                            g_variant_new("(a{sa{sv}}oo)", &settings, device.c_str(), network.id.c_str()), 5000);
    if (!reply)
        return wifiError("NetworkManager could not start the connection. Check the password and network permissions.");
    const char* profile = nullptr;
    const char* connection = nullptr;
    g_variant_get(reply.get(), "(&o&o)", &profile, &connection);
    const auto deadline = std::chrono::steady_clock::now() + 25s;
    do {
        const auto state = unsignedProperty(bus.get(), connection, activeConnection, "State");
        if (state == activeActivated)
            return {.message = "Connected to " + network.name + "."};
        if (!state || state >= activeDeactivating)
            break;
        std::this_thread::sleep_for(250ms);
    } while (std::chrono::steady_clock::now() < deadline);
    call(bus.get(), profile, savedConnection, "Delete", "()");
    return wifiError("Could not connect to " + network.name + ".");
}
#else
bool wifiSetupSupported() { return false; }
std::optional<WifiConnection> activeWifi() { return std::nullopt; }
WifiResult scanWifi() { return {.message = "Wi-Fi setup is unavailable.", .error = true}; }
WifiResult connectWifi(const WifiNetwork&, const std::string&) { return scanWifi(); }
#endif
