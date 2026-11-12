#pragma once

// =============================================================================
// 03_Files.h — SPIFFS File System Globals & Web File-Manager Definitions
// =============================================================================

// ─── Temporary String Buffers for HTML Link Generation ───────────────────────
	inline String OP01 = "";
	inline String OP03 = "";

// ─── File Action Status Message ───────────────────────────────────────────────
	inline int    SetMess    = 0;
	inline String SpesialStr = "";

// ─── Global SPIFFS File Handles ───────────────────────────────────────────────
	inline File   f;
	inline File   ftemp;
	inline String line;

// ─── Sorted Directory Listing ─────────────────────────────────────────────────
	#define File_array_LEN   50
	#define Filename_LEN     32

	struct FileNameStruct
	{
		char FileName[Filename_LEN];
		char SortName[Filename_LEN];
		int  FileSize;
	};
	inline int            StructIdx;
	inline FileNameStruct SortedFiles[File_array_LEN];

// ─── Report Display Limit ─────────────────────────────────────────────────────
	#define REPORT_LINES   25

// ─── File Page Navigation Index Constants ────────────────────────────────────
	#define PS_FILES_HOME      1
	#define PS_FILES           2
	#define PS_FILES_DOWNLOAD  3
	#define PS_FILES_UPLOAD    4
	#define PS_FILES_DELETE    5

// ─── SPIFFS Filename Constants ────────────────────────────────────────────────
	inline String StrFormatComplete = F("/FormatComplete.txt");
	inline String StrGeneralLog     = F("/GeneralLog.txt");
	inline String StrIRLog          = F("/IRLog.txt");
