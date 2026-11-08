/* Stand-in for libStatusReplay in replay_recorder_test: the same C entry points, no Qt. */
#include <stddef.h>

typedef struct {
    void (*contextProperty)(void*, const char*, const void*);
    void (*metacall)(void*, int, int, void**);
    void (*modelData)(void*, int, int, const void*);
} StatusReplayHooks;

static void onContextProperty(void* engine, const char* name, const void* value) { (void)engine; (void)name; (void)value; }
static void onMetacall(void* object, int call, int index, void** argv) { (void)object; (void)call; (void)index; (void)argv; }
static void onModelData(void* model, int row, int role, const void* value) { (void)model; (void)row; (void)role; (void)value; }

StatusReplayHooks statusreplay_recorder_attach(void* engine)
{
    (void)engine;
    StatusReplayHooks hooks = { onContextProperty, onMetacall, onModelData };
    return hooks;
}

void statusreplay_recorder_markMoment(const char* name) { (void)name; }
