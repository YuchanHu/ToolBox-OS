#pragma once
#include "sony/connection.h"

namespace sony::preview {
extern bool saved, confirmed;
extern CameraCredentials submitted;
extern std::vector<CaptureRequest> commands;
extern std::vector<BodyCommand> bodyCommands;
extern unsigned bodyClears;
extern MediaWindow mediaWindow;
extern std::shared_ptr<const MediaItem> mediaRemoved;
extern unsigned mediaCloses;
extern std::vector<AutoAction> automationCommands;
extern IntervalPlan intervalPlan;
extern unsigned automationCancels;
extern unsigned stateReads;
void report(ConnectionState state);
}
