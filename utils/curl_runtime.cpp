#include "curl_runtime.h"

#include <windows.h>
#include <curl/curl.h>

namespace
{
	SRWLOCK g_curlRuntimeLock = SRWLOCK_INIT;
	unsigned int g_curlRuntimeReferences = 0;
}

bool CurlRuntimeAcquire(void)
{
	AcquireSRWLockExclusive(&g_curlRuntimeLock);
	if (g_curlRuntimeReferences == 0 && curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
	{
		ReleaseSRWLockExclusive(&g_curlRuntimeLock);
		return false;
	}
	++g_curlRuntimeReferences;
	ReleaseSRWLockExclusive(&g_curlRuntimeLock);
	return true;
}

void CurlRuntimeRelease(void)
{
	AcquireSRWLockExclusive(&g_curlRuntimeLock);
	if (g_curlRuntimeReferences > 0)
	{
		--g_curlRuntimeReferences;
		if (g_curlRuntimeReferences == 0)
			curl_global_cleanup();
	}
	ReleaseSRWLockExclusive(&g_curlRuntimeLock);
}
