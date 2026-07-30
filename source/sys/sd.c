#include "sys/sd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* ROOT = "/3ds/j3ds";

const char* sdGetRoot(void)
{
	return ROOT;
}

static void sdEnsureRoot(void)
{
	struct stat st;
	if (stat(ROOT, &st) != 0) {
		mkdir(ROOT, 0777);
	}
}

void sdPath(char* out, size_t outLen, const char* relative)
{
	if (!out || outLen == 0)
		return;

	sdEnsureRoot();

	if (!relative || relative[0] == '\0')
		snprintf(out, outLen, "%s", ROOT);
	else
		snprintf(out, outLen, "%s/%s", ROOT, relative);
}
