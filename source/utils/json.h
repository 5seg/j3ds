#ifndef UTILS_JSON_H
#define UTILS_JSON_H

#include <jansson.h>
#include <stdbool.h>

const char* jsonGetString(json_t* root, const char* key);
int jsonGetInt(json_t* root, const char* key);
bool jsonGetBool(json_t* root, const char* key);
double jsonGetDouble(json_t* root, const char* key);

#endif
