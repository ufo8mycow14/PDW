#ifndef PDW_RESTORE_TEMPORARY_FILE_H
#define PDW_RESTORE_TEMPORARY_FILE_H
#include <windows.h>

// The restore parser owns this plaintext file only for the duration of parsing.
struct RestoreTemporaryFile
{
	char path[MAX_PATH] = {};
	explicit RestoreTemporaryFile(const char* folder) { GetTempFileNameA(folder, "PDF", 0, path); }
	~RestoreTemporaryFile() { if (path[0]) DeleteFileA(path); }
	RestoreTemporaryFile(const RestoreTemporaryFile&) = delete;
	RestoreTemporaryFile& operator=(const RestoreTemporaryFile&) = delete;
};
#endif
