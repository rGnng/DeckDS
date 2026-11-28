#include "wifi.hpp"

#import <CoreWLAN/CoreWLAN.h>

#include <algorithm>
#include <utility>

bool isWifiConnected()
{
    @autoreleasepool {
        for (CWInterface* wifi in [[CWWiFiClient sharedWiFiClient] interfaces])
            if ([wifi powerOn] && [wifi rssiValue] != 0)
                return true;
    }
    return false;
}

namespace {
int signalStrength(NSInteger rssi)
{
    return std::clamp<int>(2 * (rssi + 100), 0, 100);
}

WifiNetwork::Security securityOf(CWNetwork* network)
{
    using Security = WifiNetwork::Security;
    if ([network supportsSecurity:kCWSecurityWPA3Personal] && ![network supportsSecurity:kCWSecurityWPA3Transition])
        return Security::Sae;
    if ([network supportsSecurity:kCWSecurityPersonal] ||
        [network supportsSecurity:kCWSecurityWPAPersonal] ||
        [network supportsSecurity:kCWSecurityWPA2Personal] ||
        [network supportsSecurity:kCWSecurityWPA3Transition])
        return Security::Personal;
    if ([network supportsSecurity:kCWSecurityNone])
        return Security::Open;
    return Security::Other;
}

std::string networkId(NSData* ssid, WifiNetwork::Security security)
{
    return std::string([[ssid base64EncodedStringWithOptions:0] UTF8String]) + ":" +
        std::to_string(static_cast<int>(security));
}

WifiResult wifiError(std::string message)
{
    return {.message = std::move(message), .error = true};
}
}

bool wifiSetupSupported() { return true; }

std::optional<WifiConnection> activeWifi()
{
    @autoreleasepool {
        for (CWInterface* wifi in [[CWWiFiClient sharedWiFiClient] interfaces]) {
            const auto rssi = [wifi rssiValue];
            if (![wifi powerOn] || rssi == 0)
                continue;
            const char* name = [[wifi ssid] UTF8String];
            return WifiConnection{.name = name ? name : "", .id = {}, .strength = signalStrength(rssi)};
        }
    }
    return std::nullopt;
}

WifiResult scanWifi()
{
    @autoreleasepool {
        CWInterface* wifi = [[CWWiFiClient sharedWiFiClient] interface];
        if (![wifi powerOn])
            return wifiError("No active Wi-Fi adapter.");
        NSSet<CWNetwork*>* points = [wifi scanForNetworksWithSSID:nil error:nullptr];
        if (!points)
            return wifiError("macOS could not scan for Wi-Fi networks.");
        NSString* connected = [wifi bssid];
        WifiResult result;
        for (CWNetwork* point in points) {
            NSData* ssid = [point ssidData];
            const char* name = [[point ssid] UTF8String];
            if (![ssid length] || !name || !*name)
                continue;
            const auto security = securityOf(point);
            const int strength = signalStrength([point rssiValue]);
            WifiNetwork network{networkId(ssid, security), name, security, strength,
                                [connected isEqualToString:[point bssid]] != NO};
            const auto same = std::ranges::find(result.networks, network.id, &WifiNetwork::id);
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
            result.message = "No visible Wi-Fi networks.";
        return result;
    }
}

WifiResult connectWifi(const WifiNetwork& network, const std::string& password)
{
    @autoreleasepool {
        CWInterface* wifi = [[CWWiFiClient sharedWiFiClient] interface];
        if (![wifi powerOn])
            return wifiError("No active Wi-Fi adapter.");
        if (network.security == WifiNetwork::Security::Other)
            return wifiError("This network uses an unsupported security type.");
        NSString* secret = nil;
        if (network.security != WifiNetwork::Security::Open) {
            secret = [NSString stringWithUTF8String:password.c_str()];
            if (![secret length])
                return wifiError("Enter a Wi-Fi password.");
        }
        const auto encoded = network.id.substr(0, network.id.find(':'));
        NSData* ssid = [[[NSData alloc] initWithBase64EncodedString:
                         [NSString stringWithUTF8String:encoded.c_str()] options:0] autorelease];
        if (!ssid)
            return wifiError("This network is no longer visible. Scan again.");
        for (CWNetwork* point in [wifi scanForNetworksWithSSID:ssid error:nullptr]) {
            if (![[point ssidData] isEqualToData:ssid] || securityOf(point) != network.security)
                continue;
            if ([wifi associateToNetwork:point password:secret error:nullptr])
                return {.message = "Connected to " + network.name + "."};
            break;
        }
        return wifiError("Could not connect to " + network.name + ".");
    }
}
