#include "wifi.hpp"

#import <CoreWLAN/CoreWLAN.h>

bool isWifiConnected()
{
    @autoreleasepool {
        for (CWInterface* wifi in [[CWWiFiClient sharedWiFiClient] interfaces])
            if ([wifi powerOn] && [wifi rssiValue] != 0)
                return true;
    }
    return false;
}
