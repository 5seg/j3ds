#include "ui/screens.h"

#include <stdio.h>
#include <string.h>

#include "ui/browser.h"
#include "ui/ui_player.h"
#include "ui/input.h"
#include "storage/config.h"
#include "net/http.h"
#include "net/jellyfin.h"

static AppState s_stack[8];
static int s_top = 0;

typedef enum {
	SET_SERVER_URL,
	SET_USERNAME,
	SET_API_KEY,
	SET_DISABLE_SSL,
	SET_COUNT
} SettingItem;

static int s_settingSel = 0;
static char s_status[128] = "";

void screenInit(void)
{
	memset(s_stack, 0, sizeof(s_stack));
	s_top = 0;
	s_stack[0] = SCREEN_HOME;
	s_settingSel = 0;
	s_status[0] = '\0';
}

void screenPush(AppState state)
{
	if (s_top < 7) {
		s_top++;
		s_stack[s_top] = state;
	}
	g_app.current = state;
}

void screenPop(void)
{
	if (s_top > 0)
		s_top--;
	g_app.current = s_stack[s_top];
}

void screenChange(AppState state)
{
	s_stack[s_top] = state;
	g_app.previous = g_app.current;
	g_app.current = state;
}

static void settingsTestConnection(void)
{
	if (g_app.config.serverUrl[0] == '\0') {
		strncpy(s_status, "No server URL", sizeof(s_status) - 1);
		s_status[sizeof(s_status) - 1] = '\0';
		return;
	}

	JellyfinServerInfo info;
	Result res = jellyfinServerInfo(g_app.config.serverUrl, &info);
	if (R_SUCCEEDED(res)) {
		snprintf(s_status, sizeof(s_status), "%s / %s",
			info.serverName[0] ? info.serverName : "Jellyfin",
			info.version[0] ? info.version : "?");
	} else {
		snprintf(s_status, sizeof(s_status), "Connection failed: %08lX", (unsigned long)res);
	}
	s_status[sizeof(s_status) - 1] = '\0';
}

static void settingsEdit(void)
{
	char buf[256];
	switch (s_settingSel) {
	case SET_SERVER_URL:
		strncpy(buf, g_app.config.serverUrl, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (inputShowKeyboard(buf, sizeof(buf), "Server URL", buf)) {
			strncpy(g_app.config.serverUrl, buf, CONFIG_MAX_URL - 1);
			g_app.config.serverUrl[CONFIG_MAX_URL - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_USERNAME:
		strncpy(buf, g_app.config.username, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (inputShowKeyboard(buf, sizeof(buf), "Username", buf)) {
			strncpy(g_app.config.username, buf, CONFIG_MAX_USER - 1);
			g_app.config.username[CONFIG_MAX_USER - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_API_KEY:
		if (inputShowKeyboardPassword(buf, sizeof(buf), "API Key")) {
			strncpy(g_app.config.apiKey, buf, CONFIG_MAX_KEY - 1);
			g_app.config.apiKey[CONFIG_MAX_KEY - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_DISABLE_SSL:
		g_app.config.disableSslVerify = !g_app.config.disableSslVerify;
		s_status[0] = '\0';
		break;
	default:
		break;
	}
}

void screenUpdate(void)
{
	switch (g_app.current) {
	case SCREEN_SETTINGS:
		if (g_app.kDown & KEY_UP) {
			s_settingSel--;
			if (s_settingSel < 0)
				s_settingSel = SET_COUNT - 1;
		}

		if (g_app.kDown & KEY_DOWN) {
			s_settingSel++;
			if (s_settingSel >= SET_COUNT)
				s_settingSel = 0;
		}

		if (g_app.kDown & KEY_A)
			settingsEdit();

		if (g_app.kDown & KEY_X)
			settingsTestConnection();

		if (g_app.kDown & KEY_START) {
			if (configSave(&g_app.config))
				strncpy(s_status, "Saved.", sizeof(s_status) - 1);
			else
				strncpy(s_status, "Save failed!", sizeof(s_status) - 1);
			s_status[sizeof(s_status) - 1] = '\0';
		}

		if (g_app.kDown & KEY_B)
			screenChange(SCREEN_HOME);
		break;

	case SCREEN_BROWSER:
		browserUpdate();
		break;

	case SCREEN_PLAYER:
		playerUpdate();
		break;

	default:
		break;
	}
}

void screenRender(void)
{
	switch (g_app.current) {
	case SCREEN_HOME:
		printf("Home screen\n\n");
		printf("A: Browser\n");
		printf("SELECT: Settings\n");
		printf("START: Quit\n");
		break;
	case SCREEN_SETTINGS:
		printf("Settings\n\n");
		printf("%s Server URL: %s\n", s_settingSel == SET_SERVER_URL ? ">" : " ", g_app.config.serverUrl);
		printf("%s Username:   %s\n", s_settingSel == SET_USERNAME ? ">" : " ", g_app.config.username);
		printf("%s API Key:    %s\n", s_settingSel == SET_API_KEY ? ">" : " ", g_app.config.apiKey[0] ? "********" : "(empty)");
		printf("%s Disable SSL verify: %s\n", s_settingSel == SET_DISABLE_SSL ? ">" : " ", g_app.config.disableSslVerify ? "Yes" : "No");
		printf("\nUP/DOWN: select, A: edit\n");
		printf("X: test connection, START: save, B: back\n");
		if (s_status[0])
			printf("\n%s\n", s_status);
		break;
	case SCREEN_BROWSER:
		browserRender();
		break;
	case SCREEN_PLAYER:
		playerRender();
		break;
	default:
		break;
	}
}
