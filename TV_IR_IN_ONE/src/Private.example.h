#pragma once

// =============================================================================
// Private.example.h — template for an OPTIONAL src/Private.h
// =============================================================================
// Without Private.h (the normal case for anyone building this project):
//   * first boot: the board opens its setup WiFi "IR-FORWARD-IN1-XXXX"; join
//     it with a phone and enter your home WiFi on the page that opens
//   * the setup page asks you to choose an owner password: needed to change
//     the WiFi later and to update firmware (/update page, or over the network
//     with IR_OTA_PASS set on your PC)
//
// With Private.h (copy this file to src/Private.h and fill it in) every board
// you flash already knows your networks and upload password — handy if you
// build several. Private.h is listed in .gitignore: never commit or share it.
// =============================================================================

//   { ssid, password, ip, gateway, subnet, dns }   ip "" = automatic (DHCP)
// #define WIFI_DEFAULT_NETS { \
//     { "MyWiFi", "my-wifi-password", "", "", "", "" }, \
// }

// #define OWNER_PASSWORD_DEFAULT  "choose-a-password"
