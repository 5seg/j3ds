#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <3ds.h>
#include <stddef.h>

Result httpGlobalInit(void);
void httpGlobalExit(void);

Result httpGet(const char* url, char** out, size_t* outLen);
Result httpGetWithHeader(const char* url, const char* headerName, const char* headerValue,
	char** out, size_t* outLen);

Result httpPost(const char* url, const char* body, char** out, size_t* outLen);
Result httpPostWithHeader(const char* url, const char* body, const char* headerName,
	const char* headerValue, char** out, size_t* outLen);

typedef struct {
	const char* name;
	const char* value;
} HttpHeader;

Result httpGetWithHeaders(const char* url, const HttpHeader* headers, int headerCount,
	char** out, size_t* outLen);
Result httpPostWithHeaders(const char* url, const char* body, const HttpHeader* headers,
	int headerCount, char** out, size_t* outLen);

/* HTTP_ERR_STATUS is returned when the server replies with a non-200 status.
   Call httpLastStatus() to read the actual status code. */
#define HTTP_ERR_STATUS ((Result)-2)
int httpLastStatus(void);

typedef void (*HttpDownloadProgress)(size_t downloaded, size_t total);

Result httpDownloadFileWithProgress(const char* url, const char* path,
	HttpDownloadProgress progress);
Result httpDownloadFile(const char* url, const char* path);

/* Stream a download to a sink callback. sink returns true to continue,
   false to abort. Used for progressive audio playback. */
typedef bool (*HttpChunkSink)(const void* data, size_t size, void* user);
Result httpDownloadToSink(const char* url, HttpChunkSink sink, void* user);

#endif
