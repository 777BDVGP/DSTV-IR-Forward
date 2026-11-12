#pragma once

// =============================================================================
// 05_ThisSystem.h — Hardware I/O Map, Debounce Engine & Navigation Constants
// =============================================================================

// =============================================================================
// Pin Numbers — IR_FORWARD_V1 PCB (full map in Board.h)
// =============================================================================
// Buzzer, RED/GREEN LEDs, blue heartbeat LED, INPUT and the TMOV monitor are
// driven by Board.cpp. TRIAC/relay/indicator LED of TV_Remote are not on the PCB.
	#include "Board.h"
	#include "RouterCheck.h"

	#define OPIN_IR_LED         BRD_PIN_IR_OUT   // IR OUT — not used by TX (no local repeat)
	#define IPIN_IR_RECEIVE     BRD_PIN_IR_IN    // IR receiver TSOP31238 — active LOW output

// =============================================================================
// Input Array Index Aliases  (IIDX_*)
// =============================================================================
	#define IIDX_IR_RECEIVE     0

// =============================================================================
// Input Debounce Engine
// =============================================================================
	inline unsigned long DebouceTimeRead;
	#define DEBOUNCE_STEADY_MS   50

	struct DebounceStruct
	{
		const int    PinIO;
		bool         MemoryH;
		bool         MemoryL;
		bool         Input;
		unsigned long Time;
		bool         Status;
		String       Desc;
	};

	#define InputPorts   1

	inline DebounceStruct InPort[InputPorts] =
	{
		{ IPIN_IR_RECEIVE,   false, false, false, 0, false, "IR RECEIVER (TSOP31238)" },
	};

// =============================================================================
// Navigation Page Index Constants  (PS_*)
// =============================================================================
	#define PS_HOME       1
	#define PS_SETTINGS   8

// =============================================================================
// WT_* Table-Builder Type Constants
// =============================================================================
	#define WT_LABEL   0
	#define WT_INPUT   1
	#define WT_FIRST   2
	#define WT_NEXT    3
	#define WT_MORE    5
	#define WT_LAST    4
