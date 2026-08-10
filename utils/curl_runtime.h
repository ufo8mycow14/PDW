#ifndef PDW_CURL_RUNTIME_H
#define PDW_CURL_RUNTIME_H

// libcurl global state is shared by FTP, Apprise, and Publishing. Each manager
// acquires one reference before creating workers and releases it only after its
// final worker has joined.
bool CurlRuntimeAcquire(void);
void CurlRuntimeRelease(void);

#endif
