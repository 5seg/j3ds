#include "sys/power.h"

#include <3ds.h>

void powerPreventSleep(void)
{
	aptSetSleepAllowed(false);
}

void powerAllowSleep(void)
{
	aptSetSleepAllowed(true);
}
