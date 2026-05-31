// tdeck_ui.h — LilyGO T-Deck on-device UI (enable with -DFD_ENABLE_TDECK_UI).
#pragma once

#include "protocol.h"

#ifdef FD_ENABLE_TDECK_UI

void tdeck_ui_begin();
void tdeck_ui_note(const Detection &d);
void tdeck_ui_loop();

#endif
