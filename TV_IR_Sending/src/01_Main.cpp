// =============================================================================
// 01_Main.cpp — Hardware Initialisation & Watchdog
// =============================================================================

#include <Arduino.h>
#include "all_includes.h"

void InitHardware(){
	BoardSetup(BRD_HEARTBEAT_TX_MS);   // PCB pins, fast blue heartbeat, TMOV monitor (Board.cpp)

	SetupSystemIOs();
}

void WatchdogSetup() {
	secondTick.attach(1, ISRwatchdog);
}

void ISRwatchdog() {
	watchdogCount++;
	if (watchdogCount > 15)
	{
		SaveGenReport F("REBOOT - Watchdog");
		delay(500);
		ESP.restart();
	}
}

void LEDToggle(){

	watchdogCount = 0;

	if (WiFi.status() != WL_CONNECTED) {
		if (!Timer[TIM_WIFI_RECONNECT].Status) {
			activateTimer(TIM_WIFI_RECONNECT);
		} else if (TimerExpired(TIM_WIFI_RECONNECT)) {
			SaveGenReport F("REBOOT - WiFi Lost");
			delay(500);
			ESP.restart();
		}
	} else {
		deactivateTimer(TIM_WIFI_RECONNECT);
	}
}
