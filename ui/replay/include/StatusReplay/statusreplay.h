#pragma once

#include <QtCore/qglobal.h>

#if defined(STATUSREPLAY_LIBRARY)
#define STATUSREPLAY_EXPORT Q_DECL_EXPORT
#else
#define STATUSREPLAY_EXPORT Q_DECL_IMPORT
#endif

// C entry points resolved with dlsym by the app when STATUS_RECORD is set.
extern "C" {

typedef void (*StatusReplayContextPropertyHook)(void* engine, const char* name, const void* qvariant);
typedef void (*StatusReplayMetacallHook)(void* qobject, int call, int index, void** argv);

struct StatusReplayHooks
{
    StatusReplayContextPropertyHook contextProperty;
    StatusReplayMetacallHook metacall;
};

// Starts recording into the STATUS_RECORD directory for the given QQmlEngine*.
// Returns the hooks to install into nimqml, or null hooks when recording could not start.
STATUSREPLAY_EXPORT StatusReplayHooks statusreplay_recorder_attach(void* engine);
STATUSREPLAY_EXPORT void statusreplay_recorder_markMoment(const char* name);

STATUSREPLAY_EXPORT int statusreplay_main(int argc, char** argv);
}
