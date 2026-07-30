#include "ui/input.h"

#include <string.h>
#include <3ds.h>

static bool inputShowKeyboardWithType(char* out, size_t outLen, const char* hint, const char* initial, SwkbdType type, bool password)
{
	if (!out || outLen == 0)
		return false;

	SwkbdState swkbd;
	swkbdInit(&swkbd, type, 2, -1);
	swkbdSetInitialText(&swkbd, initial ? initial : "");
	swkbdSetHintText(&swkbd, hint ? hint : "");
	swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
	swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);

	if (password)
		swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE);

	SwkbdButton button = swkbdInputText(&swkbd, out, outLen);
	return button == SWKBD_BUTTON_RIGHT;
}

bool inputShowKeyboard(char* out, size_t outLen, const char* hint, const char* initial)
{
	return inputShowKeyboardWithType(out, outLen, hint, initial, SWKBD_TYPE_NORMAL, false);
}

bool inputShowKeyboardPassword(char* out, size_t outLen, const char* hint)
{
	return inputShowKeyboardWithType(out, outLen, hint, "", SWKBD_TYPE_NORMAL, true);
}

u32 inputGetKeysDown(void)
{
	return hidKeysDown();
}

u32 inputGetKeysHeld(void)
{
	return hidKeysHeld();
}
