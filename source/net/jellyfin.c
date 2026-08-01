#include "net/jellyfin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#include "net/http.h"
#include "utils/json.h"
#include "app.h"

#define JF_URL_MAX 1024
#define JF_FULL_URL_MAX 2048
#define JF_AUTH_HEADER_MAX 512
#define JF_HTTP_MAX_URL 1024

static void normalizeServerUrl(const char* in, char* out, size_t outLen)
{
	if (!in || outLen == 0)
		return;

	size_t len = strlen(in);
	if (len > 0 && in[len - 1] == '/')
		len--;
	if (len >= outLen)
		len = outLen - 1;

	memcpy(out, in, len);
	out[len] = '\0';
}

Result jellyfinServerInfo(const char* serverUrl, JellyfinServerInfo* info)
{
	if (!info)
		return -1;

	memset(info, 0, sizeof(*info));

	char base[JF_URL_MAX];
	normalizeServerUrl(serverUrl, base, sizeof(base));
	if (base[0] == '\0')
		return -1;

	char url[JF_FULL_URL_MAX];
	int n = snprintf(url, sizeof(url), "%s/System/Info/Public", base);
	if (n < 0 || (size_t)n >= sizeof(url) || strlen(url) >= JF_HTTP_MAX_URL)
		return -1;

	char* resp = NULL;
	size_t len = 0;
	Result ret = httpGet(url, &resp, &len);
	if (R_FAILED(ret))
		return ret;

	json_error_t err;
	json_t* root = json_loads(resp, 0, &err);
	free(resp);

	if (!root)
		return -1;

	const char* s;
	s = jsonGetString(root, "ServerName");
	if (s) {
		strncpy(info->serverName, s, sizeof(info->serverName) - 1);
		info->serverName[sizeof(info->serverName) - 1] = '\0';
	}

	s = jsonGetString(root, "Version");
	if (s) {
		strncpy(info->version, s, sizeof(info->version) - 1);
		info->version[sizeof(info->version) - 1] = '\0';
	}

	s = jsonGetString(root, "Id");
	if (s) {
		strncpy(info->id, s, sizeof(info->id) - 1);
		info->id[sizeof(info->id) - 1] = '\0';
	}

	json_decref(root);
	return 0;
}

Result jellyfinAuthByPassword(const char* serverUrl, const char* username, const char* password,
	char* apiKey, size_t apiKeyLen, char* userId, size_t userIdLen)
{
	if (!apiKey || apiKeyLen == 0)
		return -1;
	apiKey[0] = '\0';
	if (userId && userIdLen > 0)
		userId[0] = '\0';

	if (!username || !password)
		return -1;

	char base[JF_URL_MAX];
	normalizeServerUrl(serverUrl, base, sizeof(base));
	if (base[0] == '\0')
		return -1;

	char url[JF_FULL_URL_MAX];
	int n = snprintf(url, sizeof(url), "%s/Users/AuthenticateByName", base);
	if (n < 0 || (size_t)n >= sizeof(url) || strlen(url) >= JF_HTTP_MAX_URL)
		return -1;

	json_t* bodyObj = json_object();
	if (!bodyObj)
		return -1;
	json_object_set_new(bodyObj, "Username", json_string(username));
	json_object_set_new(bodyObj, "Pw", json_string(password));

	char* body = json_dumps(bodyObj, 0);
	json_decref(bodyObj);
	if (!body)
		return -1;

	char* resp = NULL;
	size_t len = 0;

	/* Jellyfin rejects AuthenticateByName without a MediaBrowser
	   authorization header that identifies the client/device. Send it both
	   as Authorization (current) and X-Emby-Authorization (legacy) because
	   some reverse proxies strip the Authorization header. */
	char auth[JF_AUTH_HEADER_MAX];
	snprintf(auth, sizeof(auth),
		"MediaBrowser Client=\"Jellyfin3DS\", Device=\"Nintendo 3DS\", "
		"DeviceId=\"j3ds-3ds\", Version=\"0.1.10\"");

	HttpHeader headers[] = {
		{ "Content-Type", "application/json" },
		{ "Authorization", auth },
		{ "X-Emby-Authorization", auth }
	};

	Result ret = httpPostWithHeaders(url, body, headers, 3, &resp, &len);
	free(body);

	if (R_FAILED(ret))
		return ret;

	json_error_t err;
	json_t* root = json_loads(resp, 0, &err);
	free(resp);

	if (!root)
		return -1;

	const char* token = jsonGetString(root, "AccessToken");
	if (token && apiKeyLen > 0) {
		strncpy(apiKey, token, apiKeyLen - 1);
		apiKey[apiKeyLen - 1] = '\0';
	} else {
		ret = -1;
	}

	json_t* userObj = json_object_get(root, "User");
	if (userId && userIdLen > 0) {
		const char* uid = jsonGetString(userObj, "Id");
		if (uid) {
			strncpy(userId, uid, userIdLen - 1);
			userId[userIdLen - 1] = '\0';
		}
	}

	json_decref(root);
	return ret;
}

Result jellyfinGetItems(const char* serverUrl, const char* apiKey, const char* parentId,
	const char* includeTypes, const char* sortBy, const char* sortOrder,
	char** json, size_t* len)
{
	if (!json || !len)
		return -1;

	char base[JF_URL_MAX];
	normalizeServerUrl(serverUrl, base, sizeof(base));
	if (base[0] == '\0')
		return -1;

	char url[JF_FULL_URL_MAX];
	const char* uid = g_app.userId[0] ? g_app.userId : "me";
	int n = snprintf(url, sizeof(url), "%s/Items?UserId=%s&Recursive=true", base, uid);
	if (n < 0 || (size_t)n >= sizeof(url))
		return -1;

	size_t pos = strlen(url);

	if (parentId && parentId[0]) {
		n = snprintf(url + pos, sizeof(url) - pos, "&ParentId=%s", parentId);
		if (n < 0 || (size_t)n >= sizeof(url) - pos)
			return -1;
		pos += (size_t)n;
	}

	if (includeTypes && includeTypes[0]) {
		n = snprintf(url + pos, sizeof(url) - pos, "&IncludeItemTypes=%s", includeTypes);
		if (n < 0 || (size_t)n >= sizeof(url) - pos)
			return -1;
		pos += (size_t)n;
	}

	if (sortBy && sortBy[0]) {
		n = snprintf(url + pos, sizeof(url) - pos, "&SortBy=%s", sortBy);
		if (n < 0 || (size_t)n >= sizeof(url) - pos)
			return -1;
		pos += (size_t)n;
	}

	if (sortOrder && sortOrder[0]) {
		n = snprintf(url + pos, sizeof(url) - pos, "&SortOrder=%s", sortOrder);
		if (n < 0 || (size_t)n >= sizeof(url) - pos)
			return -1;
		pos += (size_t)n;
	}

	n = snprintf(url + pos, sizeof(url) - pos, "&Limit=256");
	if (n < 0 || (size_t)n >= sizeof(url) - pos)
		return -1;
	pos += (size_t)n;

	if (pos >= JF_HTTP_MAX_URL)
		return -1;

	char auth[JF_AUTH_HEADER_MAX];
	snprintf(auth, sizeof(auth), "MediaBrowser Token=\"%s\"", apiKey ? apiKey : "");

	HttpHeader headers[] = {
		{ "Authorization", auth },
		{ "X-Emby-Authorization", auth }
	};

	return httpGetWithHeaders(url, headers, 2, json, len);
}

void jellyfinGetThumbnailUrl(const char* serverUrl, const char* apiKey, const char* itemId,
	char* out, size_t outLen)
{
	if (!out || outLen == 0)
		return;

	char base[JF_URL_MAX];
	normalizeServerUrl(serverUrl, base, sizeof(base));

	snprintf(out, outLen,
		"%s/Items/%s/Images/Primary?maxWidth=240&maxHeight=240&quality=90&ApiKey=%s",
		base, itemId ? itemId : "", apiKey ? apiKey : "");
}

Result jellyfinGetStreamUrl(const char* serverUrl, const char* apiKey, const char* itemId,
	char* out, size_t outLen)
{
	if (!out || outLen == 0)
		return -1;

	char base[JF_URL_MAX];
	normalizeServerUrl(serverUrl, base, sizeof(base));
	if (base[0] == '\0')
		return -1;

	int n = snprintf(out, outLen,
		"%s/Audio/%s/stream?Container=mp3&Static=true&ApiKey=%s",
		base, itemId ? itemId : "", apiKey ? apiKey : "");
	if (n < 0 || (size_t)n >= outLen)
		return -1;

	return 0;
}
