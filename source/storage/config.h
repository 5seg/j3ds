#ifndef STORAGE_CONFIG_H
#define STORAGE_CONFIG_H

#include <stdbool.h>

#define CONFIG_MAX_URL  256
#define CONFIG_MAX_USER 64
#define CONFIG_MAX_PASSWORD 128
#define CONFIG_MAX_KEY  128

typedef struct {
	char serverUrl[CONFIG_MAX_URL];
	char username[CONFIG_MAX_USER];
	char password[CONFIG_MAX_PASSWORD];
	char apiKey[CONFIG_MAX_KEY];
	bool disableSslVerify;
} Config;

bool configLoad(Config* cfg);
bool configSave(const Config* cfg);
const char* configPath(void);

#endif
