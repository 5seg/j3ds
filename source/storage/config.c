#include "storage/config.h"

#include <stdio.h>
#include <string.h>
#include <jansson.h>

#include "sys/sd.h"

const char* configPath(void)
{
	static char path[512];
	sdPath(path, sizeof(path), "config.json");
	return path;
}

bool configLoad(Config* cfg)
{
	if (!cfg)
		return false;

	memset(cfg, 0, sizeof(*cfg));
	cfg->disableSslVerify = true;

	const char* path = configPath();
	json_error_t error;
	json_t* root = json_load_file(path, 0, &error);
	if (!root) {
		printf("configLoad: failed to load %s: %s\n", path, error.text);
		return false;
	}

	if (!json_is_object(root)) {
		json_decref(root);
		return false;
	}

	json_t* val;
	val = json_object_get(root, "serverUrl");
	if (json_is_string(val)) {
		strncpy(cfg->serverUrl, json_string_value(val), CONFIG_MAX_URL - 1);
	}

	val = json_object_get(root, "username");
	if (json_is_string(val)) {
		strncpy(cfg->username, json_string_value(val), CONFIG_MAX_USER - 1);
	}

	val = json_object_get(root, "apiKey");
	if (json_is_string(val)) {
		strncpy(cfg->apiKey, json_string_value(val), CONFIG_MAX_KEY - 1);
	}

	val = json_object_get(root, "disableSslVerify");
	if (json_is_boolean(val)) {
		cfg->disableSslVerify = json_boolean_value(val);
	}

	json_decref(root);
	return true;
}

bool configSave(const Config* cfg)
{
	if (!cfg)
		return false;

	const char* path = configPath();
	json_t* root = json_object();

	json_object_set_new(root, "serverUrl", json_string(cfg->serverUrl));
	json_object_set_new(root, "username", json_string(cfg->username));
	json_object_set_new(root, "apiKey", json_string(cfg->apiKey));
	json_object_set_new(root, "disableSslVerify", json_boolean(cfg->disableSslVerify));

	int rc = json_dump_file(root, path, JSON_INDENT(2));
	json_decref(root);

	if (rc != 0) {
		printf("configSave: failed to write %s\n", path);
		return false;
	}

	return true;
}
