#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class BluetoothPairing {
public:
    enum class State { Idle, Searching, Ready, Pairing, Connected, Unpairing, Removed, Failed, UnpairFailed, Unavailable };
    struct Device {
        std::string id, name;
        bool paired = false;
    };

    BluetoothPairing();
    ~BluetoothPairing();
    void useDemoDevices();
    void refreshPaired();
    void scan();
    void pair(int index);
    void unpair(int index);
    void cancel();
    void update(double dt);
    void restoreDemoDevice(std::string_view id, bool active);
    std::string_view demoDevice() const;
    std::vector<std::string_view> demoPairedDevices() const;

    State state() const { return status; }
    bool busy() const { return status == State::Pairing || status == State::Unpairing; }
    const std::vector<Device>& devices() const { return found; }
    State stateFor(const Device& device) const { return device.id == pending.id ? status : State::Idle; }
    bool isConnected(const Device& device) const { return device.id == connected.id; }
    const std::string& connectedDevice() const { return connected.name; }

private:
    struct Operation;
    std::unique_ptr<Operation> operation;
    State status = State::Idle;
    std::vector<Device> found;
    Device pending, connected;
    bool demo = false;
    double demoTime = 0;
};
