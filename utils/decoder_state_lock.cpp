#include <windows.h>

namespace {
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION stateLock;
BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*)
{
	InitializeCriticalSection(&stateLock);
	return TRUE;
}
}

void SignalDecoderStateEnter()
{
	InitOnceExecuteOnce(&once, Initialize, NULL, NULL);
	EnterCriticalSection(&stateLock);
}
void SignalDecoderStateLeave() { LeaveCriticalSection(&stateLock); }
