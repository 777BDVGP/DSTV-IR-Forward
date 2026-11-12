#pragma once

// =============================================================================
// 02_Time.h — Software Timer Engine & Date/Time Globals
// =============================================================================
// Defines the TimerStruct type, all TIM_* index constants, and the Timer[]
// array that drives all non-blocking timeouts across the firmware.
// Also declares global date/time strings updated each second by CompileDateTime().
//
// Timer API (implemented in 02_Time.cpp):
//   activateTimer(idx)    — Start or restart a timer
//   deactivateTimer(idx)  — Cancel a running timer
//   TimerExpired(idx)     — Returns true when the timer is not running (expired)
//   RemainingSec(idx)     — Seconds remaining on a running timer
//   RemainingMin(idx)     — Minutes remaining on a running timer
// =============================================================================

// ─── NTP Server ───────────────────────────────────────────────────────────────
inline const char *ntpServer = "za.pool.ntp.org";   // South Africa NTP pool (UTC+2)

// =============================================================================
// Software Timer Engine
// =============================================================================

// ─── TimerStruct ─────────────────────────────────────────────────────────────
// Each entry in Timer[] holds the configuration and live state of one timer.
struct TimerStruct
{
	bool           Status;       // true = timer is running; false = expired or not started
	unsigned int   Units;        // Duration in the specified TimerUnits
	unsigned int   TimerUnits;   // Time base: MS (1), SEC (1000), or MIN (60000)
	unsigned long  ExpireTime;   // millis() value at which the timer expires
};

// ─── Time-Base Multipliers ────────────────────────────────────────────────────
#define MS    1        // Milliseconds multiplier
#define SEC   1000     // Seconds multiplier  (Units × 1000 ms)
#define MIN   60000    // Minutes multiplier  (Units × 60000 ms)

// ─── Timer Index Definitions ──────────────────────────────────────────────────
// Each TIM_* constant is an index into Timer[].  Keep in ascending order and
// update TIM_TOTAL when adding new entries.
#define TIM_LED_FLASHING_SPEED   0   // Heartbeat LED toggle interval         (600 ms)
#define TIM_UPDATE_SYSTEM_TIME   1   // Date/time string refresh & WebSocket push (1 s)
#define TIM_ASP_RESET            2   // (unused — WiFi is hard-coded)
#define TIM_SCREEN_TIMEOUT       3   // Browser page auto-blank timeout        (60 min)
#define TIM_RESTART_DELAY        4   // Scheduled reboot delay                 (15 s)
#define TIM_STABILIZE_DELAY      5   // Post-action stabilisation wait         (5 s)
#define TIM_PUMPING_FLASHING     6   // Pump-active LED flash rate             (500 ms)
#define TIM_RESET_OPTION_MESS    7   // WebSocket status message display time  (2 s)
#define TIM_REMOTE_LED_FLASHING  8   // Remote indicator LED flash rate        (300 ms)
#define TIM_CHECK_HEAP_AN_STACK  9   // Heap monitor — reboot if < MIN_FREE_HEAP  (15 s)
#define TIM_FILTER_SENSOR        10  // Filter pressure ADC averaging tick     (300 ms)
#define TIM_PRESURE_SENSOR       11  // Pressure sensor ADC averaging tick     (300 ms)
#define TIM_USER1_MANUAL_TIME    12  // User 1 manual irrigation auto-stop     (480 min)
#define TIM_USER2_MANUAL_TIME    13  // User 2 manual irrigation auto-stop     (480 min)
#define TIM_TANK_ON_DELAY        14  // Filter pump start delay after tank full (10 min)
#define TIM_TANK_SENSOR          15  // Tank level ADC averaging tick          (1 s)
#define TIM_BOREHOLE_TOUT        16  // Borehole pump start confirmation window (3 s)
#define TIM_BOREHOLE_RETRY       17  // Borehole retry wait after failure      (3 min)
#define TIM_WIFI_RECONNECT       18  // Reboot if WiFi absent for this long    (60 s)
#define TIM_TRIAC_ON             19  // Keep TRIAC/relay/IR-LED on after DSTV IR  (5 s default)
#define TIM_PROTOCOL_PROTECT     20  // Grace window to tolerate misdecoded DSTV repeat frames (5 s)
#define TIM_SOUNDER_DELAY        21  // Delay after LED_INDICATOR on before BUZZER beep (2 s default)
#define TIM_BUZZER_PULSE         22  // Fixed BUZZER beep duration             (150 ms)
#define TIM_BUTTON_DELAY         23  // Keep TRIAC/relay/LED_INDICATOR on after PRESS_BUTTON (3 s default)
#define TIM_HEAP_FRAGMENTED      24  // Reboot if heap stays fragmented (maxBlk < HEAP_MAXBLK_MIN) this long (2 min)

#define TIM_TOTAL   25   // Total number of timer slots — must match the array size below

// ─── Timer Array Initialisation ──────────────────────────────────────────────
// All timers start inactive (Status = false).  Call activateTimer() to start one.
// Columns: Status   Units   TimerUnits   ExpireTime
inline TimerStruct Timer[TIM_TOTAL] =
{
	{ false,  600,   MS,   0 },   // TIM_LED_FLASHING_SPEED   — 600 ms LED heartbeat
	{ false,    1,  SEC,   0 },   // TIM_UPDATE_SYSTEM_TIME   — 1 s clock refresh
	{ false,    5,  SEC,   0 },   // TIM_ASP_RESET            — unused
	{ false,   60,  MIN,   0 },   // TIM_SCREEN_TIMEOUT       — 60 min browser timeout
	{ false,   15,  SEC,   0 },   // TIM_RESTART_DELAY        — 15 s reboot delay
	{ false,    5,  SEC,   0 },   // TIM_STABILIZE_DELAY      — 5 s stabilisation
	{ false,  500,   MS,   0 },   // TIM_PUMPING_FLASHING     — 500 ms pump LED flash
	{ false,    2,  SEC,   0 },   // TIM_RESET_OPTION_MESS    — 2 s message display
	{ false,  300,   MS,   0 },   // TIM_REMOTE_LED_FLASHING  — 300 ms remote LED flash
	{ false,   15,  SEC,   0 },   // TIM_CHECK_HEAP_AN_STACK  — 15 s heap monitor
	{ false,  300,   MS,   0 },   // TIM_FILTER_SENSOR        — 300 ms filter ADC tick
	{ false,  300,   MS,   0 },   // TIM_PRESURE_SENSOR       — 300 ms pressure ADC tick
	{ false,  480,  MIN,   0 },   // TIM_USER1_MANUAL_TIME    — 480 min User 1 timeout
	{ false,  480,  MIN,   0 },   // TIM_USER2_MANUAL_TIME    — 480 min User 2 timeout
	{ false,   10,  MIN,   0 },   // TIM_TANK_ON_DELAY        — 10 min filter pump delay
	{ false,    1,  SEC,   0 },   // TIM_TANK_SENSOR          — 1 s tank level ADC tick
	{ false,    3,  SEC,   0 },   // TIM_BOREHOLE_TOUT        — 3 s borehole start window
	{ false,    3,  MIN,   0 },   // TIM_BOREHOLE_RETRY       — 3 min borehole retry wait 
	{ false,   60,  SEC,   0 },   // TIM_WIFI_RECONNECT       — 60 s WiFi-loss reboot
	{ false,    5,  SEC,   0 },   // TIM_TRIAC_ON             — 5 s TRIAC/relay/IR-LED keep-on
	{ false, 5000,   MS,   0 },   // TIM_PROTOCOL_PROTECT     — 5 s DSTV-repeat misdecode grace window
	{ false,    2,  SEC,   0 },   // TIM_SOUNDER_DELAY        — 2 s delay before BUZZER beep
	{ false,  150,   MS,   0 },   // TIM_BUZZER_PULSE         — 150 ms fixed beep duration
	{ false,    3,  SEC,   0 },   // TIM_BUTTON_DELAY         — 3 s TRIAC/relay/LED_INDICATOR keep-on after PRESS_BUTTON
	{ false,    2,  MIN,   0 },   // TIM_HEAP_FRAGMENTED      — 2 min sustained fragmentation reboot
};

// =============================================================================
// Date / Time Globals
// =============================================================================

// Month and weekday name lookup tables used by CompileDateTime()
inline const char *YR_MONTH[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
inline const char *WEEK_DAY[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

inline String NoNetwork = F("OFFLINE  NO  TIME   ");  // Shown before NTP sync succeeds
inline String DateTime  = NoNetwork;                   // Current date/time display string (updated every second)

inline int MonthDay = -1;  // Last seen day-of-month; triggers log trim when it changes
inline int WeekDay  = 1;   // Current weekday: 1 = Sunday … 7 = Saturday

