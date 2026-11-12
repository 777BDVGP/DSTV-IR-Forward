#pragma once

// =============================================================================
// 04_WebPages.h — Web Server, WebSocket, Page Elements & HTML Templates
// =============================================================================

// ─── WebSocket Variable ID Base Addresses ────────────────────────────────────
#define ID_HOME       50    // Home page      — variables  (50+)
#define ID_SETTINGS  100    // Settings page  — variables  (100+)
#define ID_SUSPEND   150    // (unused)
#define ID_VALVES    200    // (unused)
#define ID_VALCONF   250    // (unused)
#define ID_IRRISETUP 300    // (unused)
#define ID_TANK      350    // (unused)

// ─── Web Server & WebSocket ───────────────────────────────────────────────────
inline AsyncWebServer server(80);
inline AsyncWebSocket ws("/ws");

// ─── HTML Colour Style Strings ────────────────────────────────────────────────
inline const char COLOR_DEFAULT[] = "color:#94a3b8";
inline const char COLOR_RED[]     = "color:#ef4444";
inline const char COLOR_BLUE[]    = "color:#38bdf8";
inline const char COLOR_GREEN[]   = "color:#22c55e";
inline const char COLOR_VIOLET[]  = "color:#ff00ff";

// ─── Status Header Message Constants (FA_MESS_*) ─────────────────────────────
#define FA_MESS_01    1
#define FA_MESS_02    2
#define FA_MESS_03    3
#define FA_MESS_04    4
#define FA_MESS_06    6
#define FA_MESS_07    7
#define FA_MESS_08    8
#define FA_MESS_09    9
#define FA_MESS_10   10
#define FA_MESS_11   11
#define FA_MESS_RED  12
#define FA_MESS_GRN  13

inline int Delmess = FA_MESS_06;

#define DEL_FNF    9
#define DEL_GEN   10
#define DEL_PUMP  11

// ─── WebSocket Connection Tracking ────────────────────────────────────────────
inline uint32_t Lastnum       = 0;
inline bool     LastnumUpdated = true;

// Header - Required on all pages (holds title, time, action & style)
inline char webHeadPage[] PROGMEM = R"=====(
<html>
<head>
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<link rel="icon" href="data:,">
	<style>
		/* Same look as the TV IR RX pages: dark navy, slate cards, pill nav */
		*{box-sizing:border-box;margin:0;padding:0}
		:root{color-scheme:dark;--bg:#0f172a;--card:#1e293b;--line:#334155;--text:#e2e8f0;--mute:#94a3b8;--acc:#0284c7;--acc2:#38bdf8}
		body{font-family:sans-serif;background:var(--bg);color:var(--text);margin:0;padding:16px;font-size:.9em}
		h2{font-size:1.5em;color:var(--acc);margin:0 0 4px}
		h4{font-size:.8em;font-weight:normal;color:var(--mute);margin:0 0 10px}
		p{text-align:left;margin:0}
		b{color:var(--text)}
		a{color:var(--acc2)}
		ul{list-style-type:none;margin:4px 0 12px;padding:0;display:flex;flex-wrap:wrap;gap:6px}
		lh a,li a,lk a{display:block;color:var(--text);background:var(--card);padding:6px 12px;border-radius:6px;text-decoration:none}
		lh a{background:var(--acc)}
		li a:hover,lk a:hover{background:var(--line)}
		li a.red{background:#b91c1c;color:#fff}
		li a.red:hover{background:#991b1b}
		.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
		.nav a{color:var(--text);text-decoration:none;background:var(--card);padding:6px 12px;border-radius:6px}
		.nav a:hover:not(.act):not(.warn){background:var(--line)}
		.nav a.act{background:var(--acc);color:#fff}
		.nav a.warn{background:#b45309;color:#fff}
		.nav a.warn:hover{background:#92400e}
		table{width:100%;border-collapse:collapse;font-size:.95em;margin:0 0 12px;background:var(--card);border-radius:8px;overflow:hidden}
		td,th{text-align:left;padding:6px 10px;border-bottom:1px solid var(--line);word-break:break-word;overflow-wrap:break-word}
		tr:last-child td,tr:last-child th{border-bottom:none}
		th{color:var(--mute);font-weight:normal}
		.hd td,.hd th{color:var(--acc2);font-size:.85em;font-weight:bold;text-transform:uppercase;letter-spacing:.5px;border-bottom:1px solid var(--line);text-align:left}
		.hd th:last-child,.hd td:last-child{text-align:right}
		table.compact td,table.compact th{padding:3px 6px}table.compact input[type=text],table.compact input[type=number],table.compact input[type=time],table.compact input[type=date],table.compact select{padding:1px 2px;font-size:11px}
		tr:not(.hd) td:first-child{width:100%}
		table.susp th:not(:first-child){min-width:65px;text-align:center}
		table.susp tr:not(.hd) td:not(:first-child){min-width:65px;text-align:center}
		tr:not(.hd) td:last-child{text-align:right;white-space:nowrap}
		tr:not(.hd) td:last-child label{display:inline-block;vertical-align:middle}
		tr:not(.hd) th:first-child{width:100%}
		tr:not(.hd) th:last-child:not(:first-child){text-align:right;white-space:nowrap}
		tr:not(.hd) th:last-child:not(:first-child) p{text-align:right;margin-left:0}
		table.grid tr:not(.hd) td:first-child{width:auto;text-align:left}
		table.grid tr:not(.hd) td:not(:first-child){padding-left:10px;padding-right:10px;text-align:center}
		table.grid tr:not(.hd) td:last-child{text-align:center;white-space:normal;padding-right:10px}
		table.grid tr:not(.hd) td{text-align:center}
		table.grid tr th{text-align:center;white-space:nowrap;padding-left:10px;padding-right:10px}
		table.grid tr th:first-child{text-align:left;white-space:normal;padding-left:6px;padding-right:6px}
		table.grid tr th:last-child:not(:first-child){text-align:center;white-space:nowrap;padding-right:10px}
		table.grid .hd th{text-align:center}
		table.grid .hd th:first-child{text-align:left}
		table.grid .hd th:last-child{text-align:center}
		input[type=time],input[type=text],input[type=number],input[type=date],input[type=password],select{background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:4px;padding:5px 6px;font-size:.95em;width:140px;max-width:45vw}
		input[type=checkbox]{accent-color:var(--acc);cursor:pointer;width:16px;height:16px}
		input:focus,select:focus{outline:1px solid var(--acc)}
		input[type=button],input[type=submit],button{background:var(--acc);border:none;color:#fff;padding:8px 14px;font-size:.9em;cursor:pointer;border-radius:4px}
		input[type=button]:hover,input[type=submit]:hover,button:hover{background:#0369a1}
		input[type=file]{color:var(--text)}
		pre{color:var(--text)}
		.btn{display:inline-block;padding:8px 14px;border-radius:4px;font-size:.9em;font-weight:bold;border:none;cursor:pointer;margin:4px;text-decoration:none}
		.btn-bl{background:var(--acc);color:#fff}.btn-bl:hover{background:#0369a1}
		.btn-rd{background:#b91c1c;color:#fff}.btn-rd:hover{background:#991b1b}
		.btn-or{background:#b45309;color:#fff}.btn-or:hover{background:#92400e}
		.btn-gr{background:#15803d;color:#fff}.btn-gr:hover{background:#166534}
		.btns{text-align:center;padding:8px 0}
		label{color:var(--text)}
	</style>
</head>

<body onload="javascript:init()">

)=====";

// WebSocket Script
inline char webSocketScript[] PROGMEM = R"=====(
<script>

	function init() {
		StartWebSocket();
	}

	function setOtaStatus(msg, color) {
		var el = document.getElementById("ota_status");
		if (el) { el.value = msg; el.style.color = color; }
	}

	// Auto-reconnect: after a reboot / OTA / WiFi drop the socket closes; retry
	// every 2 s. An UPDATE pressed while offline is kept and sent on reconnect.
	var Socket;
	var wsWasOpen   = false;
	var wsRetry     = null;
	var wsPending   = null;   // UPDATE payload waiting for the connection

	function setConnMsg(msg, color) {
		var el = document.getElementById("H03");
		if (el) { el.innerHTML = msg; el.style.color = color; }
	}

	function wsScheduleReconnect() {
		if (wsRetry) return;
		wsRetry = setTimeout(function() { wsRetry = null; StartWebSocket(); }, 2000);
	}

	function wsSend(text) {
		if (Socket && Socket.readyState === WebSocket.OPEN) { Socket.send(text); return true; }
		return false;
	}

	function StartWebSocket() {
		try { Socket = new WebSocket('ws://' + window.location.host + '/ws'); }
		catch (e) { wsScheduleReconnect(); return; }

		Socket.onopen = function() {
			if (wsWasOpen) setConnMsg("Reconnected", "#22c55e");
			wsWasOpen = true;
			if (wsPending) { var p = wsPending; wsPending = null; Socket.send(p); }
		};

		Socket.onerror = function(event) {
			console.log("Socket.onerror");   // onclose follows and reconnects
		};

		Socket.onclose = function(event) {
			console.log("Socket.onclose");
			setConnMsg(wsPending ? "Not connected - UPDATE will be sent on reconnect..." : "Connection lost - reconnecting...", "#ef4444");
			wsScheduleReconnect();
		};

		// Update web page elements from ESP32
		Socket.onmessage = function(event) {
			const jsonObject = JSON.parse(event.data);
			if (jsonObject.type === "ota_start")    { setOtaStatus("Upload starting...",                     "orange"); return; }
			if (jsonObject.type === "ota_progress") { setOtaStatus("Uploading... " + jsonObject.percent + "%", "orange"); return; }
			if (jsonObject.type === "ota_end")      { setOtaStatus("Busy Rebooting", "red"); otaBusy = true; return; }
			if (jsonObject.type === "ota_error")    { setOtaStatus("Update failed!",                         "red");    return; }
			var ElementAtt;
			for (const [key, value] of Object.entries(jsonObject)) {
				if (key === "Attribute") {
					const attrName = jsonObject["AttributeName"] || "style";
					if (ElementAtt !== null) { ElementAtt.setAttribute(attrName, value); }
				} else if (key === "AttributeName") {
					// consumed by the "Attribute" handler above
				} else {
					const Element = document.getElementById(key);
					if (Element === null) { ElementAtt = null; continue; }
					if (Element.tagName === 'SELECT') {
						Element.value = value;
						ElementAtt = Element;
					} else {
						if (Element !== null) {
							if (Element.type === "checkbox") {
								Element.checked = (value === "true");
								ElementAtt = null;
							} else {
								Element.value = value;
								Element.innerHTML = value;
								ElementAtt = Element;
							}
						} else {
							ElementAtt = null;
						}
					}
				}
			}
		}
	}

	// Read all inputs and send to ESP32 via WebSocket
	function UpdatePage() {
		const jsonObject = {};
		for (const input of document.querySelectorAll("input")) {
			jsonObject[input.id] = (input.type === "checkbox") ? (input.checked ? "true" : "false") : input.value;
		}
		for (const select of document.querySelectorAll("select")) {
			jsonObject[select.id] = select.value;
		}
		const text = JSON.stringify(jsonObject);
		if (!wsSend(text)) {
			wsPending = text;   // sent by onopen as soon as the board is back
			setConnMsg("Not connected - UPDATE will be sent on reconnect...", "#ef4444");
			wsScheduleReconnect();
		}
	}

	// Buttons are not queued (a delayed REBOOT would surprise) — just report
	function wsSendNow(text) {
		if (!wsSend(text)) { setConnMsg("Not connected - try again in a moment", "#ef4444"); wsScheduleReconnect(); }
	}

	function ButtonUser1() {
		wsSendNow(JSON.stringify({"MAN1": "User 1"}));
	}

	function ButtonUser2() {
		wsSendNow(JSON.stringify({"MAN2": "User 2"}));
	}

	function ButtonReboot() {
		wsSendNow(JSON.stringify({"REBOOT": "YES"}));
	}

	// Build label+input table below header
	function createLabelInputTable(parameters, tableClass) {
		const tableElement = document.createElement("table");
		if (tableClass) tableElement.className = tableClass;
		const headerRow = document.createElement("tr");

		for (const parameter of parameters) {
			if (!parameter.name) continue;
			const headerCell = document.createElement("th");
			headerCell.textContent = parameter.name;
			headerRow.appendChild(headerCell);
		}

		if (headerRow.textContent.trim() !== '') {
			headerRow.querySelectorAll('th').forEach(cell => { cell.style.textAlign = 'left'; });
			tableElement.appendChild(headerRow);
		}

		for (const parameter of parameters) {
			if (parameter.name !== undefined) continue;
			if (parameter.sectionHeader !== undefined) {
				const row = document.createElement("tr");
				const cell = document.createElement("td");
				cell.colSpan = 2;
				cell.textContent = parameter.sectionHeader;
				cell.style.cssText = "font-weight:bold;padding:4px 6px;text-align:left;";
				row.appendChild(cell);
				tableElement.appendChild(row);
				continue;
			}
			const row = document.createElement("tr");

			for (const label of parameter.labels) {
				const cell = document.createElement("td");
				const labelElement = document.createElement("label");
				labelElement.textContent = label.text;
				labelElement.id = label.id;
				for (const a of (label.attributes || [])) { labelElement.setAttribute(a.n, a.v); }
				cell.appendChild(labelElement);
				row.appendChild(cell);
			}

			for (const input of parameter.inputs) {
				const cell = document.createElement("td");

				if (input.type === "select") {
					const inputElement = document.createElement("select");
					inputElement.id = input.id;
					for (const a of (input.attributes || [])) { inputElement.setAttribute(a.n, a.v); }
					for (const optionText of input.options) {
						const option = document.createElement("option");
						option.value = optionText;
						option.textContent = optionText;
						inputElement.appendChild(option);
					}
					inputElement.value = input.value;
					cell.appendChild(inputElement);
				} else {
					const inputElement = document.createElement("input");
					inputElement.type = input.type;
					inputElement.id = input.id;
					inputElement.value = input.value;
					if (input.type === "checkbox") {
						inputElement.checked = (input.value === "true");
					}
					for (const a of (input.attributes || [])) { inputElement.setAttribute(a.n, a.v); }
					cell.appendChild(inputElement);
				}

				row.appendChild(cell);
			}

			tableElement.appendChild(row);
		}

		document.body.appendChild(tableElement);
	}

</script>
)=====";

#define webScriptTag "<script src='/wss.js?v=" __DATE__ "'></script>\n"

inline char webHTMLEnd[] PROGMEM = R"=====(
</body>
</html>
)=====";

inline char webUpateButton[] PROGMEM = R"=====(
	<div><br></div>
	<div>
		<b><a><input type="button" id="Update" onclick="UpdatePage()" value="UPDATE" style="font-size:14px"></a></b>
	</div>
)=====";

// ─── SelectOptions ────────────────────────────────────────────────────────────
class SelectOptions {
public:
	String Option = "";
};

// ─── PageElement Variable Type Tags ──────────────────────────────────────────
#define VTYPE_STRING 1
#define VTYPE_DOUBLE 2
#define VTYPE_FLOAT  3
#define VTYPE_INT    4
#define VTYPE_BOOL   5

// =============================================================================
// PageElement
// =============================================================================
class PageElement {
public:
	void* operator new(size_t sz)  { void* p = ps_malloc(sz); return p ? p : malloc(sz); }
	void  operator delete(void* p) { free(p); }

	PageElement(String* InName, String Desc, String FStyle, String* VStyle, int ID);
	PageElement(double* InName, String Desc, String FStyle, String* VStyle, int ID);
	PageElement(float*  InName, String Desc, String FStyle, String* VStyle, int ID);
	PageElement(int*    InName, String Desc, String FStyle, String* VStyle, int ID);
	PageElement(bool*   InName, String Desc, String FStyle, String* VStyle, int ID);
	~PageElement();

	void   SetName(String InName);
	String GetName();
	String GetNameLast();
	void   UpdateLast();
	bool   HasChanged();

	const String& GetVarStyle();
	const String& GetDescription();
	void          SetDescription(String desc);
	const String& GetFStyle();

	String GetTypeStr();
	String GetID(const char* Type);

	void          SetStringToSelect(int Options);
	void          AddOption(String Option);
	const String& GetOption(int Idx);
	int           GetOptionCount();

	void SetTextAlign(String align);
	void SetRange(float mn, float mx, float step = NAN);
	void SetPlaceholder(String ph);
	void SetReadOnly(bool ro = true);

	const String& GetAttrTextAlign();
	bool          HasAttrMin();
	bool          HasAttrMax();
	bool          HasAttrStep();
	float         GetAttrMin();
	float         GetAttrMax();
	float         GetAttrStep();
	const String& GetAttrPlaceholder();
	bool          GetAttrReadOnly();

private:
	String* NameS;
	double* NameD;
	float*  NameF;
	int*    NameI;
	bool*   NameB;
	int     Type;

	String NameSLast;
	double NameDLast;
	float  NameFLast;
	int    NameILast;
	String NameBLast;

	String  VarDesc  = "";
	String  FixStyle = "";
	String* VarStyle;
	int     VarID = 0;
	bool    NameStringSelect = false;
	int     OptionCount = 0;
	int     OptionIndex = 0;
	SelectOptions* OptionList;

	String AttrTextAlign   = "";
	float  AttrMin         = NAN;
	float  AttrMax         = NAN;
	float  AttrStep        = NAN;
	String AttrPlaceholder = "";
	bool   AttrReadOnly    = false;
};

inline PageElement::PageElement(String* InName, String Desc, String FStyle, String* VStyle, int ID) {
	NameS = InName; NameSLast = *NameS;
	Type = VTYPE_STRING; VarDesc = Desc; FixStyle = FStyle; VarStyle = VStyle; VarID = ID;
	OptionList = nullptr;
}

inline PageElement::PageElement(double* InName, String Desc, String FStyle, String* VStyle, int ID) {
	NameD = InName; NameDLast = *NameD;
	Type = VTYPE_DOUBLE; VarDesc = Desc; FixStyle = FStyle; VarStyle = VStyle; VarID = ID;
	OptionList = nullptr;
}

inline PageElement::PageElement(float* InName, String Desc, String FStyle, String* VStyle, int ID) {
	NameF = InName; NameFLast = *NameF;
	Type = VTYPE_FLOAT; VarDesc = Desc; FixStyle = FStyle; VarStyle = VStyle; VarID = ID;
	OptionList = nullptr;
}

inline PageElement::PageElement(int* InName, String Desc, String FStyle, String* VStyle, int ID) {
	NameI = InName; NameILast = *NameI;
	Type = VTYPE_INT; VarDesc = Desc; FixStyle = FStyle; VarStyle = VStyle; VarID = ID;
	OptionList = nullptr;
}

inline PageElement::PageElement(bool* InName, String Desc, String FStyle, String* VStyle, int ID) {
	NameB = InName; NameBLast = (*NameB ? "true" : "false");
	Type = VTYPE_BOOL; VarDesc = Desc; FixStyle = FStyle; VarStyle = VStyle; VarID = ID;
	OptionList = nullptr;
}

inline PageElement::~PageElement() {
	delete[] OptionList;
}

inline void PageElement::SetName(String InName) {
	switch (Type) {
		case VTYPE_STRING: *NameS = InName;              break;
		case VTYPE_DOUBLE: *NameD = InName.toDouble();   break;
		case VTYPE_FLOAT:  *NameF = InName.toFloat();    break;
		case VTYPE_INT:    *NameI = InName.toInt();      break;
		case VTYPE_BOOL:   *NameB = (InName == "true");  break;
	}
}

inline String PageElement::GetName() {
	switch (Type) {
		case VTYPE_STRING: return *NameS;
		case VTYPE_DOUBLE: return String(*NameD);
		case VTYPE_FLOAT:  return String(*NameF);
		case VTYPE_INT:    return String(*NameI);
		case VTYPE_BOOL:   return (*NameB ? "true" : "false");
	}
	return "";
}

inline const String& PageElement::GetVarStyle() {
	return *VarStyle;
}

inline String PageElement::GetNameLast() {
	switch (Type) {
		case VTYPE_STRING: return NameSLast;
		case VTYPE_DOUBLE: return String(NameDLast);
		case VTYPE_FLOAT:  return String(NameFLast);
		case VTYPE_INT:    return String(NameILast);
		case VTYPE_BOOL:   return NameBLast;
	}
	return "";
}

inline void PageElement::UpdateLast() {
	switch (Type) {
		case VTYPE_STRING: NameSLast = *NameS;                         break;
		case VTYPE_DOUBLE: NameDLast = *NameD;                         break;
		case VTYPE_FLOAT:  NameFLast = *NameF;                         break;
		case VTYPE_INT:    NameILast = *NameI;                         break;
		case VTYPE_BOOL:   NameBLast = (*NameB ? "true" : "false");    break;
	}
}

inline const String& PageElement::GetDescription()         { return VarDesc;  }
inline void          PageElement::SetDescription(String d) { VarDesc = d;      }
inline const String& PageElement::GetFStyle()              { return FixStyle;  }

inline bool PageElement::HasChanged() {
	switch (Type) {
		case VTYPE_STRING: return *NameS != NameSLast;
		case VTYPE_DOUBLE: return *NameD != NameDLast;
		case VTYPE_FLOAT:  return *NameF != NameFLast;
		case VTYPE_INT:    return *NameI != NameILast;
		case VTYPE_BOOL:   return NameBLast != (*NameB ? "true" : "false");
	}
	return false;
}

inline String PageElement::GetID(const char* Type) {
	if (Type && *Type) { String r = Type; r += VarID; return r; }
	return String(VarID);
}

inline void PageElement::SetStringToSelect(int Options) {
	NameStringSelect = true;
	OptionCount = Options;
	OptionList = new SelectOptions[OptionCount];
}

inline void PageElement::AddOption(String Option) {
	OptionList[OptionIndex].Option = Option;
	if (OptionIndex < (OptionCount - 1)) OptionIndex++;
}

inline const String& PageElement::GetOption(int Idx)  { return OptionList[Idx].Option; }
inline int            PageElement::GetOptionCount()   { return OptionCount; }

inline String PageElement::GetTypeStr() {
	switch (Type) {
		case VTYPE_STRING: return (NameStringSelect ? "select" : "text");
		case VTYPE_DOUBLE:
		case VTYPE_FLOAT:
		case VTYPE_INT:    return "number";
		case VTYPE_BOOL:   return "checkbox";
	}
	return "";
}

inline void   PageElement::SetTextAlign(String align)               { AttrTextAlign = align; }
inline void   PageElement::SetRange(float mn, float mx, float step) { AttrMin = mn; AttrMax = mx; AttrStep = step; }
inline void   PageElement::SetPlaceholder(String ph)                { AttrPlaceholder = ph; }
inline void   PageElement::SetReadOnly(bool ro)                     { AttrReadOnly = ro; }

inline const String& PageElement::GetAttrTextAlign()   { return AttrTextAlign;   }
inline bool          PageElement::HasAttrMin()         { return !isnan(AttrMin); }
inline bool          PageElement::HasAttrMax()         { return !isnan(AttrMax); }
inline bool          PageElement::HasAttrStep()        { return !isnan(AttrStep); }
inline float         PageElement::GetAttrMin()         { return AttrMin;         }
inline float         PageElement::GetAttrMax()         { return AttrMax;         }
inline float         PageElement::GetAttrStep()        { return AttrStep;        }
inline const String& PageElement::GetAttrPlaceholder() { return AttrPlaceholder; }
inline bool          PageElement::GetAttrReadOnly()    { return AttrReadOnly;    }

// ─── PageVar ──────────────────────────────────────────────────────────────────
struct PageVar {
	PageElement* Var     = nullptr;
	bool         deleted = true;
};
