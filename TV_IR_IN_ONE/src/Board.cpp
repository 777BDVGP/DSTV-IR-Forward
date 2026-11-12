// =============================================================================
// Board.cpp — IR_FORWARD_V1 PCB: heartbeat, IR LEDs, TMOV monitor, buzzer
// =============================================================================
// Same as TV_IR_Sending/src and TV_IR_Receiving/src.
// Runs in its own FreeRTOS task (10 ms), so the heartbeat and the TMOV alarm
// keep working while the main firmware is still connecting to WiFi.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include "Board.h"

static uint32_t          heartbeatHalfMs = BRD_HEARTBEAT_TX_MS / 2;

static volatile uint32_t redUntilMs   = 0;
static volatile uint32_t greenUntilMs = 0;

static volatile uint32_t movLastPulseMs = 0;   // set by the GPIO 36 ISR
static volatile BoardMovState movState  = MOV_UNKNOWN;
static volatile bool     movMuted       = false;
static volatile bool     movFitted      = true;  // false = surge parts left out (NVS)
static uint32_t          movPulsingSinceMs = 0; // 0 = no continuous pulses
static uint32_t          bootMs            = 0;

static volatile bool     inputActive   = false;
static uint32_t          inputChangeMs = 0;

// -----------------------------------------------------------------------------
// NVS — buzzer mute survives reboots (scheduled reboots happen every night)
// -----------------------------------------------------------------------------
static void SaveMuted(bool muted) {
	Preferences prefs;
	if (prefs.begin("board", false)) {
		prefs.putBool("movMute", muted);
		prefs.end();
	}
}

static bool LoadMuted() {
	Preferences prefs;
	bool muted = false;
	if (prefs.begin("board", true)) {
		muted = prefs.getBool("movMute", false);
		prefs.end();
	}
	return muted;
}

static void SaveFitted(bool fitted) {
	Preferences prefs;
	if (prefs.begin("board", false)) {
		prefs.putBool("movFit", fitted);
		prefs.end();
	}
}

static bool LoadFitted() {
	Preferences prefs;
	bool fitted = true;
	if (prefs.begin("board", true)) {
		fitted = prefs.getBool("movFit", true);
		prefs.end();
	}
	return fitted;
}

static void IRAM_ATTR MovPulseISR() {
	movLastPulseMs = millis();
}

// -----------------------------------------------------------------------------
// UpdateMov — 50 Hz pulses present → OK, absent → BLOWN
// -----------------------------------------------------------------------------
static void UpdateMov(uint32_t now) {
	if (!movFitted) {               // surge parts left out: nothing to watch
		movState          = MOV_NONE;
		movPulsingSinceMs = 0;
		return;
	}
	// Signed age: the ISR can stamp a pulse just after 'now' was read, making
	// it 1 ms "in the future". Unsigned, that wrapped to ~4e9 → instant false
	// BLOWN. Negative age = pulse just seen. BLOWN needs 5 s with no pulse.
	int32_t pulseAgeMs = (int32_t)(now - movLastPulseMs);
	bool pulsing = pulseAgeMs < (int32_t)BRD_MOV_BLOWN_MS;

	if (!pulsing)                    movPulsingSinceMs = 0;
	else if (movPulsingSinceMs == 0) movPulsingSinceMs = now;

	BoardMovState next = movState;
	if (!pulsing && now - bootMs >= BRD_MOV_BLOWN_MS) {
		next = MOV_BLOWN;
	} else if (pulsing && now - movPulsingSinceMs >= BRD_MOV_OK_MS) {
		next = MOV_OK;
	}

	// TMOV intact again (replaced) → the mute has done its job
	if (next == MOV_OK && movMuted) {
		movMuted = false;
		SaveMuted(false);
	}

	movState = next;
}

// -----------------------------------------------------------------------------
// UpdateBuzzer — repeating beep while BLOWN and not muted
// -----------------------------------------------------------------------------
static void UpdateBuzzer(uint32_t now) {
	bool on = BoardBuzzerOn() && (now % BRD_BUZZ_PERIOD_MS) < BRD_BUZZ_ON_MS;
	digitalWrite(BRD_PIN_BUZZER, on ? HIGH : LOW);
}

static void UpdateInput(uint32_t now) {
	bool reading = digitalRead(BRD_PIN_INPUT) == LOW;   // shorted to GND = active
	if (reading == inputActive) {
		inputChangeMs = now;
	} else if (now - inputChangeMs >= BRD_INPUT_DEBOUNCE_MS) {
		inputActive = reading;
	}
}

static void BoardTask(void *) {
	for (;;) {
		uint32_t now = millis();

		digitalWrite(BRD_PIN_BLUE_LED,  ((now / heartbeatHalfMs) & 1) ? HIGH : LOW);
		digitalWrite(BRD_PIN_LED_RED,   (int32_t)(redUntilMs   - now) > 0 ? HIGH : LOW);
		digitalWrite(BRD_PIN_LED_GREEN, (int32_t)(greenUntilMs - now) > 0 ? HIGH : LOW);

		UpdateMov(now);
		UpdateBuzzer(now);
		UpdateInput(now);

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

// -----------------------------------------------------------------------------
// BoardSetup
// -----------------------------------------------------------------------------
void BoardSetup(uint32_t heartbeatPeriodMs) {
	heartbeatHalfMs = heartbeatPeriodMs / 2;

	const uint8_t outputs[] = { BRD_PIN_IR_OUT, BRD_PIN_BUZZER, BRD_PIN_LED_RED,
	                            BRD_PIN_LED_GREEN, BRD_PIN_BLUE_LED };
	for (uint8_t pin : outputs) {
		pinMode(pin, OUTPUT);
		digitalWrite(pin, LOW);
	}
	pinMode(BRD_PIN_INPUT,     INPUT_PULLUP);
	pinMode(BRD_PIN_MOV_SENSE, INPUT_PULLUP);   // 4N35 open collector

	movMuted       = LoadMuted();
	movFitted      = LoadFitted();
	bootMs         = millis();
	movLastPulseMs = bootMs - BRD_MOV_BLOWN_MS; // no pulse seen yet
	attachInterrupt(digitalPinToInterrupt(BRD_PIN_MOV_SENSE), MovPulseISR, FALLING);

	xTaskCreatePinnedToCore(BoardTask, "BoardTask", 4096, NULL, 1, NULL, 0);
}

void BoardFlashRed()   { redUntilMs   = millis() + BRD_LED_FLASH_MS; }
void BoardFlashGreen() { greenUntilMs = millis() + BRD_LED_FLASH_MS; }

BoardMovState BoardMovGetState() { return movState; }
bool          BoardMovMuted()    { return movMuted; }
bool          BoardBuzzerOn()    { return movState == MOV_BLOWN && !movMuted; }
bool          BoardInputActive() { return inputActive; }

bool BoardMovFitted() { return movFitted; }

void BoardMovSetFitted(bool fitted) {
	if (fitted == movFitted) return;
	if (fitted) {                  // check again from scratch: 5 s grace, no false alarm
		movLastPulseMs = millis() - BRD_MOV_BLOWN_MS;
		bootMs         = millis();
		movState       = MOV_UNKNOWN;
	}
	movFitted = fitted;
	SaveFitted(fitted);
}

void BoardMovSetMuted(bool muted) {
	if (muted == movMuted) return;
	movMuted = muted;
	SaveMuted(muted);
}

const char *BoardMovText() {
	switch (movState) {
		case MOV_OK:    return "OK";
		case MOV_BLOWN: return movMuted ? "BLOWN - buzzer muted" : "BLOWN";
		case MOV_NONE:  return "not fitted";
		default:        return "checking...";
	}
}
