#include "net/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "app.h"

#define HTTP_MAX_URL        1024
#define HTTP_CHUNK_SIZE     4096
#define HTTP_MAX_REDIRECTS  8

#define HTTP_ERR_GENERIC    ((Result)-1)
#define HTTP_ERR_STATUS       ((Result)-2)
#define HTTP_ERR_TOO_MANY_REDIRECTS ((Result)-3)

static Result httpReadResponse(httpcContext* context, char** out, size_t* outLen)
{
	char* buf = (char*)malloc(HTTP_CHUNK_SIZE);
	if (!buf)
		return HTTP_ERR_GENERIC;

	size_t size = 0;
	Result ret = 0;

	while (true) {
		u32 read = 0;
		ret = httpcDownloadData(context, (u8*)(buf + size), HTTP_CHUNK_SIZE, &read);
		size += read;

		if (ret == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING) {
			if (size + HTTP_CHUNK_SIZE < size) {
				free(buf);
				return HTTP_ERR_GENERIC;
			}
			char* tmp = (char*)realloc(buf, size + HTTP_CHUNK_SIZE);
			if (!tmp) {
				free(buf);
				return HTTP_ERR_GENERIC;
			}
			buf = tmp;
		} else if (R_SUCCEEDED(ret)) {
			char* tmp = (char*)realloc(buf, size + 1);
			if (!tmp) {
				free(buf);
				return HTTP_ERR_GENERIC;
			}
			buf = tmp;
			buf[size] = '\0';
			*out = buf;
			*outLen = size;
			return 0;
		} else {
			free(buf);
			return ret;
		}
	}
}

static Result httpRequest(httpcContext* context, HTTPC_RequestMethod method, const char* url,
	const char* body, const char* headerName, const char* headerValue, u32* statusOut)
{
	Result ret = httpcOpenContext(context, method, url, 1);
	if (R_FAILED(ret))
		return ret;

	if (g_app.config.disableSslVerify) {
		ret = httpcSetSSLOpt(context, SSLCOPT_DisableVerify);
		if (R_FAILED(ret))
			goto cleanup;
	}

	ret = httpcAddRequestHeaderField(context, "User-Agent", "Jellyfin3DS/1.0");
	if (R_FAILED(ret))
		goto cleanup;

	if (headerName && headerValue) {
		ret = httpcAddRequestHeaderField(context, headerName, headerValue);
		if (R_FAILED(ret))
			goto cleanup;
	}

	char* postRaw = NULL;

	if (method == HTTPC_METHOD_POST) {
		ret = httpcAddRequestHeaderField(context, "Content-Type", "application/json");
		if (R_FAILED(ret))
			goto cleanup;

		if (body) {
			size_t bodyLen = strlen(body);
			size_t alignedSize = (bodyLen + 3) & ~3;
			postRaw = (char*)malloc(alignedSize);
			if (!postRaw) {
				ret = HTTP_ERR_GENERIC;
				goto cleanup;
			}
			memset(postRaw, 0, alignedSize);
			memcpy(postRaw, body, bodyLen);
			ret = httpcAddPostDataRaw(context, (u32*)postRaw, bodyLen);
			if (R_FAILED(ret)) {
				free(postRaw);
				goto cleanup;
			}
		}
	}

	ret = httpcBeginRequest(context);
	if (postRaw) {
		free(postRaw);
		postRaw = NULL;
	}
	if (R_FAILED(ret))
		goto cleanup;

	ret = httpcGetResponseStatusCode(context, statusOut);
	if (R_FAILED(ret))
		goto cleanup;

	return 0;

cleanup:
	httpcCloseContext(context);
	return ret;
}

static Result httpDoRequest(const char* url, HTTPC_RequestMethod method, const char* body,
	const char* headerName, const char* headerValue, char** out, size_t* outLen)
{
	if (!url || !out || !outLen)
		return HTTP_ERR_GENERIC;
	if (strlen(url) >= HTTP_MAX_URL)
		return HTTP_ERR_GENERIC;

	*out = NULL;
	*outLen = 0;

	char* redirectUrl = (char*)malloc(HTTP_MAX_URL);
	if (!redirectUrl)
		return HTTP_ERR_GENERIC;
	redirectUrl[0] = '\0';

	const char* currentUrl = url;
	int redirects = 0;
	Result ret = 0;
	httpcContext context;

	while (true) {
		u32 status = 0;
		ret = httpRequest(&context, method, currentUrl, body, headerName, headerValue, &status);
		if (R_FAILED(ret)) {
			free(redirectUrl);
			return ret;
		}

		if ((status >= 301 && status <= 303) || (status >= 307 && status <= 308)) {
			if (redirects >= HTTP_MAX_REDIRECTS) {
				httpcCloseContext(&context);
				free(redirectUrl);
				return HTTP_ERR_TOO_MANY_REDIRECTS;
			}

			ret = httpcGetResponseHeader(&context, "Location", redirectUrl, HTTP_MAX_URL);
			httpcCloseContext(&context);
			if (R_FAILED(ret)) {
				free(redirectUrl);
				return ret;
			}
			redirectUrl[HTTP_MAX_URL - 1] = '\0';
			currentUrl = redirectUrl;
			redirects++;
			continue;
		}

		if (status != 200) {
			httpcCloseContext(&context);
			free(redirectUrl);
			return HTTP_ERR_STATUS;
		}

		ret = httpReadResponse(&context, out, outLen);
		httpcCloseContext(&context);
		free(redirectUrl);
		return ret;
	}
}

Result httpGlobalInit(void)
{
	return httpcInit(4 * 1024 * 1024);
}

void httpGlobalExit(void)
{
	httpcExit();
}

Result httpGet(const char* url, char** out, size_t* outLen)
{
	return httpDoRequest(url, HTTPC_METHOD_GET, NULL, NULL, NULL, out, outLen);
}

Result httpGetWithHeader(const char* url, const char* headerName, const char* headerValue,
	char** out, size_t* outLen)
{
	return httpDoRequest(url, HTTPC_METHOD_GET, NULL, headerName, headerValue, out, outLen);
}

Result httpPost(const char* url, const char* body, char** out, size_t* outLen)
{
	return httpDoRequest(url, HTTPC_METHOD_POST, body, NULL, NULL, out, outLen);
}

Result httpPostWithHeader(const char* url, const char* body, const char* headerName,
	const char* headerValue, char** out, size_t* outLen)
{
	return httpDoRequest(url, HTTPC_METHOD_POST, body, headerName, headerValue, out, outLen);
}

Result httpDownloadFile(const char* url, const char* path)
{
	return httpDownloadFileWithProgress(url, path, NULL);
}

Result httpDownloadFileWithProgress(const char* url, const char* path,
	HttpDownloadProgress progress)
{
	if (!url || !path || strlen(url) >= HTTP_MAX_URL)
		return HTTP_ERR_GENERIC;

	FILE* f = fopen(path, "wb");
	if (!f)
		return HTTP_ERR_GENERIC;

	char* redirectUrl = (char*)malloc(HTTP_MAX_URL);
	if (!redirectUrl) {
		fclose(f);
		return HTTP_ERR_GENERIC;
	}
	redirectUrl[0] = '\0';

	const char* currentUrl = url;
	int redirects = 0;
	Result ret = 0;
	httpcContext context;

	while (true) {
		u32 status = 0;
		ret = httpRequest(&context, HTTPC_METHOD_GET, currentUrl, NULL, NULL, NULL, &status);
		if (R_FAILED(ret)) {
			free(redirectUrl);
			fclose(f);
			return ret;
		}

		if ((status >= 301 && status <= 303) || (status >= 307 && status <= 308)) {
			if (redirects >= HTTP_MAX_REDIRECTS) {
				httpcCloseContext(&context);
				free(redirectUrl);
				fclose(f);
				return HTTP_ERR_TOO_MANY_REDIRECTS;
			}

			ret = httpcGetResponseHeader(&context, "Location", redirectUrl, HTTP_MAX_URL);
			httpcCloseContext(&context);
			if (R_FAILED(ret)) {
				free(redirectUrl);
				fclose(f);
				return ret;
			}
			redirectUrl[HTTP_MAX_URL - 1] = '\0';
			currentUrl = redirectUrl;
			redirects++;
			continue;
		}

		if (status != 200) {
			httpcCloseContext(&context);
			free(redirectUrl);
			fclose(f);
			return HTTP_ERR_STATUS;
		}

		u8 buf[HTTP_CHUNK_SIZE];
		u32 read = 0;
		Result downloadRet = 0;
		bool writeErr = false;
		size_t totalDownloaded = 0;
		u32 downloadedSize = 0;
		u32 totalSize = 0;

		httpcGetDownloadSizeState(&context, &downloadedSize, &totalSize);

		do {
			downloadRet = httpcDownloadData(&context, buf, sizeof(buf), &read);
			if (read > 0) {
				if (fwrite(buf, 1, read, f) != read) {
					writeErr = true;
					break;
				}
				totalDownloaded += read;
				if (progress)
					progress(totalDownloaded, (size_t)totalSize);
			}
		} while (downloadRet == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

		httpcCloseContext(&context);
		free(redirectUrl);
		fclose(f);

		if (writeErr)
			return HTTP_ERR_GENERIC;
		if (R_FAILED(downloadRet))
			return downloadRet;
		return 0;
	}
}
