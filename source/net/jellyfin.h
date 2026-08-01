#ifndef NET_JELLYFIN_H
#define NET_JELLYFIN_H

#include <3ds.h>
#include <stddef.h>

typedef struct {
	char serverName[64];
	char version[32];
	char id[64];
} JellyfinServerInfo;

Result jellyfinServerInfo(const char* serverUrl, JellyfinServerInfo* info);
Result jellyfinAuthByPassword(const char* serverUrl, const char* username, const char* password,
	char* apiKey, size_t apiKeyLen, char* userId, size_t userIdLen);
Result jellyfinGetViews(const char* serverUrl, const char* apiKey, char** json, size_t* len);
Result jellyfinGetItems(const char* serverUrl, const char* apiKey, const char* parentId,
	const char* artistId, const char* includeTypes, char** json, size_t* len);
void jellyfinGetThumbnailUrl(const char* serverUrl, const char* apiKey, const char* itemId,
	char* out, size_t outLen);
Result jellyfinGetStreamUrl(const char* serverUrl, const char* apiKey, const char* itemId,
	char* out, size_t outLen);

#endif
