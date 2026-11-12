#pragma once

// =============================================================================
// Private.example.h — template for an OPTIONAL src/Private.h
// =============================================================================
// Without Private.h (the normal case for anyone building this project): on
// first start the board opens its setup WiFi "IR-FORWARD-RX-XXXX" /
// "IR-FORWARD-TX-XXXX" (open); join it with a phone, enter your home WiFi and
// choose an owner password. You need that password to change the WiFi later
// and for network firmware uploads (set it on your PC as IR_OTA_PASS).
//
// With Private.h (copy this file to src/Private.h) every board you flash gets
// this owner password at once. Private.h is in .gitignore: never share it.
// =============================================================================

// #define OWNER_PASSWORD_DEFAULT  "choose-a-password"

// Optional names for your access points (shown on the status page / wifiscan),
// 2.4 GHz BSSID → name. Without it the BSSID is shown.
// #define AP_NAMES { \
//     { {0x8C,0x86,0xDD,0x00,0x00,0x01}, "LivingRoom" }, \
// }
