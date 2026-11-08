package app.status.mobile;

/** Build-time switches shared by the UI and the :statusgo process. */
public final class BuildFlags {
    private BuildFlags() {}

    /**
     * debug, profile, PR, replay and recorder (RECORDER=1) packages; never true for normal release
     * or fdroid builds.
     */
    public static final boolean PROFILING = BuildConfig.PR_VARIANT || BuildConfig.REPLAY_VARIANT
            || BuildConfig.RECORDER
            || (!"release".equals(BuildConfig.BUILD_TYPE) && !"fdroid".equals(BuildConfig.BUILD_TYPE));

    /**
     * Replay variant (docs/replay-mode.md): the UI replays a recording, there is no status-go
     * service and no stub library, so StatusGoStub must never be touched (its class init
     * loads libstatus_stub).
     */
    public static final boolean REPLAY = BuildConfig.REPLAY_VARIANT;
}
