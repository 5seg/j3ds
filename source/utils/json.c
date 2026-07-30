#include "utils/json.h"

const char* jsonGetString(json_t* root, const char* key)
{
	if (!root || !key)
		return NULL;
	json_t* value = json_object_get(root, key);
	if (json_is_string(value))
		return json_string_value(value);
	return NULL;
}

int jsonGetInt(json_t* root, const char* key)
{
	if (!root || !key)
		return 0;
	json_t* value = json_object_get(root, key);
	if (json_is_integer(value))
		return (int)json_integer_value(value);
	return 0;
}

bool jsonGetBool(json_t* root, const char* key)
{
	if (!root || !key)
		return false;
	json_t* value = json_object_get(root, key);
	if (json_is_boolean(value))
		return json_is_true(value);
	return false;
}

double jsonGetDouble(json_t* root, const char* key)
{
	if (!root || !key)
		return 0.0;
	json_t* value = json_object_get(root, key);
	if (json_is_real(value))
		return json_real_value(value);
	if (json_is_integer(value))
		return (double)json_integer_value(value);
	return 0.0;
}
