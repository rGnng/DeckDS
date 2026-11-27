#include "bluetooth.hpp"

#include <algorithm>
#include <utility>

namespace {
void mergeDevice(std::vector<BluetoothPairing::Device>& devices, BluetoothPairing::Device device)
{
    const auto existing = std::ranges::find(devices, device.id, &BluetoothPairing::Device::id);
    if (existing == devices.end())
        devices.push_back(std::move(device));
    else
        *existing = std::move(device);
}
}

namespace {
constexpr std::string_view demoHeadphones = "demo-headphones";
constexpr std::string_view demoSpeaker = "demo-speaker";
constexpr auto headphonesName = "Demo Headphones";
constexpr auto speakerName = "Demo Speaker";
}

#if defined(DECKDS_BLUEZ)
#include <gio/gio.h>

#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

namespace {
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;
using Connection = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;
using Cancellable = std::unique_ptr<GCancellable, decltype(&g_object_unref)>;
constexpr auto adapterInterface = "org.bluez.Adapter1";
constexpr auto deviceInterface = "org.bluez.Device1";
constexpr auto objectManager = "org.freedesktop.DBus.ObjectManager";

Variant call(GDBusConnection* bus, const char* path, const char* interface, const char* method,
             GCancellable* cancel, int timeout = 2500, GVariant* args = nullptr)
{
    return {g_dbus_connection_call_sync(bus, "org.bluez", path, interface, method, args,
                                        nullptr, G_DBUS_CALL_FLAGS_NONE, timeout, cancel, nullptr),
            g_variant_unref};
}

bool pairAudioDevice(GDBusConnection* bus, const char* device, GCancellable* cancel)
{
    constexpr auto agentPath = "/org/deckds/PairingAgent";
    constexpr auto agentManager = "org.bluez.AgentManager1";
    constexpr auto agentXml = R"(
        <node><interface name="org.bluez.Agent1">
            <method name="Release"/>
            <method name="Cancel"/>
            <method name="RequestAuthorization"><arg type="o" direction="in"/></method>
        </interface></node>)";
    const GDBusInterfaceVTable handler{
        [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
           GVariant* args, GDBusMethodInvocation* invocation, gpointer target) {
            if (std::string_view(method) == "RequestAuthorization") {
                const char* path = nullptr;
                g_variant_get(args, "(&o)", &path);
                if (std::string_view(path) != static_cast<const char*>(target)) {
                    g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Device not selected");
                    return;
                }
            }
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }, nullptr, nullptr, {}
    };
    const std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context(g_main_context_new(), g_main_context_unref);
    const std::unique_ptr<GDBusNodeInfo, decltype(&g_dbus_node_info_unref)> node(
        g_dbus_node_info_new_for_xml(agentXml, nullptr), g_dbus_node_info_unref);
    g_main_context_push_thread_default(context.get());
    const guint agent = g_dbus_connection_register_object(bus, agentPath, node->interfaces[0], &handler,
                                                           const_cast<char*>(device), nullptr, nullptr);
    const bool registered = agent && static_cast<bool>(call(bus, "/org/bluez", agentManager, "RegisterAgent",
                                             cancel, 2500, g_variant_new("(os)", agentPath, "NoInputNoOutput")));
    bool paired = false;
    if (registered) {
        std::optional<bool> result;
        g_dbus_connection_call(bus, "org.bluez", device, deviceInterface, "Pair", nullptr, nullptr,
                               G_DBUS_CALL_FLAGS_NONE, 30000, cancel,
                               [](GObject* connection, GAsyncResult* response, gpointer state) {
            GError* error = nullptr;
            const Variant reply(g_dbus_connection_call_finish(G_DBUS_CONNECTION(connection), response, &error), g_variant_unref);
            const std::unique_ptr<gchar, decltype(&g_free)> name(error ? g_dbus_error_get_remote_error(error) : nullptr, g_free);
            *static_cast<std::optional<bool>*>(state) = reply || (name && std::string_view(name.get()) == "org.bluez.Error.AlreadyExists");
            g_clear_error(&error);
        }, &result);
        while (!result)
            g_main_context_iteration(context.get(), true);
        paired = *result;
        if (!paired)
            call(bus, device, deviceInterface, "CancelPairing", nullptr, 300);
        call(bus, "/org/bluez", agentManager, "UnregisterAgent", nullptr, 300, g_variant_new("(o)", agentPath));
    }
    if (agent)
        g_dbus_connection_unregister_object(bus, agent);
    g_main_context_pop_thread_default(context.get());
    return paired;
}

bool connectAudioDevice(GDBusConnection* bus, const char* device, GCancellable* cancel)
{
    if (call(bus, device, deviceInterface, "Connect", cancel, 15000))
        return true;
    const auto reply = call(bus, device, "org.freedesktop.DBus.Properties", "Get", cancel, 2500,
                            g_variant_new("(ss)", deviceInterface, "Connected"));
    if (!reply || !g_variant_is_of_type(reply.get(), G_VARIANT_TYPE("(v)")))
        return false;
    const Variant value(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    const Variant unboxed(g_variant_get_variant(value.get()), g_variant_unref);
    return g_variant_is_of_type(unboxed.get(), G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(unboxed.get());
}

Variant managedObjects(GDBusConnection* bus, GCancellable* cancel)
{
    const auto reply = call(bus, "/", objectManager, "GetManagedObjects", cancel);
    return {reply ? g_variant_get_child_value(reply.get(), 0) : nullptr, g_variant_unref};
}

std::string poweredAdapter(GVariant* objects)
{
    if (!objects)
        return {};
    GVariantIter entries;
    g_variant_iter_init(&entries, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&entries, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        const Variant values(interfaces, g_variant_unref);
        const Variant adapter(g_variant_lookup_value(values.get(), adapterInterface, G_VARIANT_TYPE_VARDICT),
                              g_variant_unref);
        gboolean powered = false;
        if (adapter && g_variant_lookup(adapter.get(), "Powered", "b", &powered) && powered)
            return path;
    }
    return {};
}

std::vector<BluetoothPairing::Device> audioDevices(GVariant* objects, const std::string& adapter, bool pairedOnly = false)
{
    std::vector<BluetoothPairing::Device> devices;
    if (!objects)
        return devices;
    GVariantIter entries;
    g_variant_iter_init(&entries, objects);
    const char* path = nullptr;
    GVariant* interfaces = nullptr;
    while (g_variant_iter_next(&entries, "{&o@a{sa{sv}}}", &path, &interfaces)) {
        const Variant values(interfaces, g_variant_unref);
        if (!std::string_view(path).starts_with(adapter + "/"))
            continue;
        const Variant device(g_variant_lookup_value(values.get(), deviceInterface, G_VARIANT_TYPE_VARDICT),
                             g_variant_unref);
        if (!device)
            continue;
        gboolean paired = false;
        g_variant_lookup(device.get(), "Paired", "b", &paired);
        gint16 rssi = 0;
        if (!paired && (pairedOnly || !g_variant_lookup(device.get(), "RSSI", "n", &rssi)))
            continue;

        const char* icon = "";
        guint32 deviceClass = 0;
        const char* name = "";
        g_variant_lookup(device.get(), "Icon", "&s", &icon);
        g_variant_lookup(device.get(), "Class", "u", &deviceClass);
        g_variant_lookup(device.get(), "Alias", "&s", &name);
        if (*name && (std::string_view(icon).starts_with("audio-") || (deviceClass & 0x1f00) == 0x0400))
            devices.push_back({path, name, static_cast<bool>(paired)});
    }
    std::ranges::sort(devices, {}, &BluetoothPairing::Device::name);
    return devices;
}
}
#endif

struct BluetoothPairing::Operation {
#if defined(DECKDS_BLUEZ)
    enum class Kind { ListPaired, Scan, Connect, Remove };
    struct Result {
        State state;
        bool paired = false;
    };
    std::future<Result> result;
    std::mutex discoveryMutex;
    std::vector<Device> discoveries;
    std::jthread worker;

    std::vector<Device> takeDiscoveries()
    {
        const std::lock_guard lock(discoveryMutex);
        return std::exchange(discoveries, {});
    }

    explicit Operation(Kind kind = Kind::Scan, Device target = {})
    {
        std::promise<Result> reply;
        result = reply.get_future();
        worker = std::jthread([this, kind, target = std::move(target), reply = std::move(reply)](std::stop_token stop) mutable {
            const Cancellable cancel(g_cancellable_new(), g_object_unref);
            const std::stop_callback onStop(stop, [&] { g_cancellable_cancel(cancel.get()); });
            std::unique_ptr<gchar, decltype(&g_free)> address(
                g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, cancel.get(), nullptr), g_free);
            const Connection bus(address ? g_dbus_connection_new_for_address_sync(
                address.get(), static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, cancel.get(), nullptr) : nullptr,
                g_object_unref);
            if (!bus) {
                reply.set_value({kind == Kind::ListPaired ? State::Idle :
                                 kind == Kind::Connect ? State::Failed :
                                 kind == Kind::Remove ? State::UnpairFailed : State::Unavailable});
                return;
            }
            if (kind == Kind::Remove) {
                const auto adapter = target.id.substr(0, target.id.rfind('/'));
                const bool removed = static_cast<bool>(call(bus.get(), adapter.c_str(), adapterInterface, "RemoveDevice",
                                                           cancel.get(), 2500, g_variant_new("(o)", target.id.c_str())));
                reply.set_value({removed ? State::Removed : State::UnpairFailed});
                return;
            }
            if (kind == Kind::Connect) {
                const char* path = target.id.c_str();
                const bool paired = target.paired || pairAudioDevice(bus.get(), path, cancel.get());
                const bool connected = paired && connectAudioDevice(bus.get(), path, cancel.get());
                reply.set_value({connected ? State::Connected : State::Failed, paired});
                return;
            }

            const auto initial = managedObjects(bus.get(), cancel.get());
            const auto adapter = poweredAdapter(initial.get());
            const auto publish = [&](std::vector<Device> devices) {
                const std::lock_guard lock(discoveryMutex);
                for (auto& device : devices)
                    mergeDevice(discoveries, std::move(device));
            };
            if (!adapter.empty())
                publish(audioDevices(initial.get(), adapter, true));
            if (kind == Kind::ListPaired) {
                reply.set_value({State::Idle});
                return;
            }
            if (adapter.empty() || !call(bus.get(), adapter.c_str(), adapterInterface, "StartDiscovery", cancel.get())) {
                reply.set_value({State::Unavailable});
                return;
            }
            using namespace std::chrono_literals;
            bool refreshed = false;
            for (int i = 0; i < 60 && !stop.stop_requested(); ++i) {
                if (i % 3 == 0) {
                    const auto objects = managedObjects(bus.get(), cancel.get());
                    if (objects) {
                        publish(audioDevices(objects.get(), adapter));
                        refreshed = true;
                    }
                }
                std::this_thread::sleep_for(100ms);
            }
            call(bus.get(), adapter.c_str(), adapterInterface, "StopDiscovery", nullptr, 300);
            reply.set_value({refreshed ? State::Ready : State::Unavailable});
        });
    }
#endif
};

BluetoothPairing::BluetoothPairing()
{
#if defined(__APPLE__)
    demo = true;
#endif
}
BluetoothPairing::~BluetoothPairing() = default;

void BluetoothPairing::useDemoDevices()
{
    demo = true;
    constexpr std::string_view names[]{
        "Studio Headphones", "Living Room Speaker", "Pocket Earbuds", "Desk Speaker",
        "Travel Headphones", "Kitchen Speaker", "Bedroom Speaker", "Workshop Radio",
        "Portable Speaker", "Wireless Over-Ear Headphones with Active Noise Cancellation",
    };
    found.clear();
    for (const auto name : names)
        found.push_back({"demo-audio-" + std::to_string(found.size() + 1), std::string(name), true});
}

void BluetoothPairing::refreshPaired()
{
    if (busy())
        return;
    cancel();
    std::erase_if(found, [](const Device& device) { return !device.paired; });
#if defined(DECKDS_BLUEZ)
    if (!demo)
        operation = std::make_unique<Operation>(Operation::Kind::ListPaired);
#endif
}

void BluetoothPairing::scan()
{
    if (busy())
        return;
    cancel();
    if (demo) {
        status = State::Searching;
        demoTime = .8;
        return;
    }
#if defined(DECKDS_BLUEZ)
    status = State::Searching;
    operation = std::make_unique<Operation>();
#else
    status = State::Unavailable;
#endif
}

void BluetoothPairing::pair(int index)
{
    if (busy() || index < 0 || index >= static_cast<int>(found.size()))
        return;
    cancel();
    pending = found[index];
    status = State::Pairing;
    if (demo) {
        demoTime = .7;
        return;
    }
#if defined(DECKDS_BLUEZ)
    operation = std::make_unique<Operation>(Operation::Kind::Connect, pending);
#endif
}

void BluetoothPairing::unpair(int index)
{
    if (busy() || index < 0 || index >= static_cast<int>(found.size()) || !found[index].paired)
        return;
    cancel();
    pending = found[index];
    status = State::Unpairing;
    if (demo) {
        demoTime = .25;
        return;
    }
#if defined(DECKDS_BLUEZ)
    operation = std::make_unique<Operation>(Operation::Kind::Remove, pending);
#endif
}

void BluetoothPairing::cancel()
{
    if (status == State::Unpairing)
        return;
    operation.reset();
    status = State::Idle;
    demoTime = 0;
}

void BluetoothPairing::update(double dt)
{
    if (demo) {
        if (demoTime <= 0)
            return;
        demoTime = std::max(0.0, demoTime - dt);
        if (status == State::Searching) {
            if (demoTime <= .6 && std::ranges::find(found, demoHeadphones, &Device::id) == found.end())
                found.push_back({std::string(demoHeadphones), headphonesName});
            if (demoTime <= .3 && std::ranges::find(found, demoSpeaker, &Device::id) == found.end())
                found.push_back({std::string(demoSpeaker), speakerName});
            if (demoTime == 0)
                status = State::Ready;
        } else if (status == State::Pairing && demoTime == 0) {
            status = State::Connected;
        } else if (status == State::Unpairing && demoTime == 0) {
            status = State::Removed;
        }
    } else {
#if defined(DECKDS_BLUEZ)
        if (!operation)
            return;
        const bool ready = operation->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        for (auto& device : operation->takeDiscoveries())
            mergeDevice(found, std::move(device));
        if (!ready)
            return;
        const auto result = operation->result.get();
        operation.reset();
        status = result.state;
        if (result.paired) {
            pending.paired = true;
            mergeDevice(found, pending);
        }
#endif
    }
    if (status == State::Connected) {
        pending.paired = true;
        mergeDevice(found, pending);
        connected = pending;
    } else if (status == State::Removed) {
        std::erase_if(found, [&](const Device& device) { return device.id == pending.id; });
        if (connected.id == pending.id)
            connected = {};
        status = State::Idle;
    }
}

void BluetoothPairing::restoreDemoDevice(std::string_view id, bool active)
{
#if defined(__APPLE__)
    const auto* name = id == demoHeadphones ? headphonesName : id == demoSpeaker ? speakerName : nullptr;
    if (name) {
        Device device{std::string(id), name, true};
        mergeDevice(found, device);
        if (active)
            connected = std::move(device);
    }
#else
    (void)id;
    (void)active;
#endif
}

std::string_view BluetoothPairing::demoDevice() const
{
#if defined(__APPLE__)
    if (connected.id == demoHeadphones)
        return demoHeadphones;
    if (connected.id == demoSpeaker)
        return demoSpeaker;
#endif
    return {};
}

std::vector<std::string_view> BluetoothPairing::demoPairedDevices() const
{
    std::vector<std::string_view> ids;
#if defined(__APPLE__)
    for (const auto& device : found)
        if (device.paired)
            ids.emplace_back(device.id);
#endif
    return ids;
}
