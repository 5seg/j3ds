#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "app.h"

void screenInit(void);
void screenPush(AppState state);
void screenPop(void);
void screenChange(AppState state);
void screenUpdate(void);
void screenRender(void);
void screenRenderTop(void);

#endif
