#pragma once

// =============================================================================
// 10_Reports.h — SaveGenReport (logs to Serial + appends to /GeneralLog.txt)
// =============================================================================
// Usage: SaveGenReport("message string");
//        SaveGenReport F("PROGMEM string");  — constructor-style with F() macro
// =============================================================================

struct SaveGenReport {
	SaveGenReport(String msg) {
		DEBUG_PRINTLN("[LOG] " + msg);
		File lf = SPIFFS.open(StrGeneralLog, "a");
		if (!lf) return;
		lf.println(DateTime + " - " + msg);
		lf.close();
	}
};
