# Ship candidate: manual test checklist (spike/floor-ship)

APK: `app.status.ffloor` (pr release, non-debuggable, R8, e2 Qt kit + order files), installed on the Redmi A5 with a test profile (password `<test password>`), logged out.
Branch `spike/floor-ship` (worktree `~/Repos/status-desktop.worktrees/spike-floor-ship`, tip 1e0ae314cb) = origin/master 518d92d76f + 52 signed commits (floor-final levers + L1–L4, no replay tooling, no markers, no reverted pairs). Commit ids below are on that branch.
Rebase: no textual conflicts. One functional fix found in the Lenovo smoke and squashed into 45457d8a0f: the first-run welcome page (now created by URL) was pushed with a transition before the view had its size and rendered shifted half a screen left (buttons off-screen); it is now placed with `StackView.Immediate`.

Legend: ✔ = already seen working on a device (where in brackets); ☐ = to check. "Popup opens" means: it appears, shows real content, closes with X/back, and the screen behind is unchanged. Watch logcat for `is not a type`, `TypeError`, `DeferredComponent:`, `UnsatisfiedLink`, `FATAL` (filter: `adb logcat | grep -E "qrc:/|DeferredComponent|FATAL|UnsatisfiedLink"`).

## 0. Basics
| | what to do | expect | commit |
|---|---|---|---|
| ✔ | Cold start → login screen → password → Enter | login screen in ~2 s, wallet with token logos | all [Redmi real A/B, 10 runs, earlier build of the same levers] |
| ✔ | Section smoke: wallet, market, browser (status.app), settings → profile, chat, home grid render | all [Lenovo, this APK; logcat clean of qrc/FATAL/DeferredComponent errors] |
| ☐ | Rotate the phone on the wallet, chat, settings | layout switches, no blank panels, state kept | 1b69af8587 |
| ☐ | Background the app 1 min, come back | same screen, no reload/blank | a5cfbbf46c |
| ☐ | Sign out from Settings → Sign out & Quit | sign-out dialog opens; app quits | 48b3765feb (SignOutConfirmationDialog by URL) |

## 1. Known regressions (look for them)
| | what to do | expect / what to look for | commit |
|---|---|---|---|
| ☐ | First QR scan after a cold start: wallet header scan icon (or Send → scan) | camera preview appears; **known: ~0.9 s slower on the first open (CameraX init)**. Note the delay; a later open should be fast | 389b58d3e7, 6a634cd253 |
| ☐ | First UI sound after start: Settings → Notifications → move the Volume slider and release | sound plays; **known: first sound ~0.45 s late (SoundPool decode)**; second one immediate | 7860a0e7eb, 1e0ae314cb |

## 2. "Must verify before shipping" (PATH-TO-150.md)
| | what to do | expect | commit |
|---|---|---|---|
| ☐ | Scan a real QR code (another phone showing a Status profile / address QR) | decoded, navigates to profile / fills address | 389b58d3e7, 6a634cd253 |
| ☐ | Rotate while the QR scanner is open | preview keeps running, correct orientation, still decodes | 389b58d3e7 |
| ☐ | Play a video: a collectible with video (Wallet → Collectibles → open one), or a video in a chat message → tap | video plays with sound; seeking works | 281f00981f, f71bb0a264 |
| ☐ | https video (same, a remote https one) | plays (FFmpeg + OpenSSL stub loaded on first use) | 4281cb025f, 281f00981f |
| ☐ | Collectibles images: Wallet → Collectibles | images load (platform TLS) | ea6027ada6 |
| ☐ | Link previews: send a message with an https link in a chat | preview card with image | ea6027ada6 |
| ☐ | Community images: Communities portal, open a community | banners/logos load | ea6027ada6, a8f4baa36f |
| ☐ | Avatars / chat images from the media server (plain HTTP loopback) | own avatar, contact avatars, images in messages show | a8f4baa36f |
| ☐ | Keycard: Settings → Keycard → any flow with a real card (NFC) | keycard drawer appears on tap, card is read | 0aabe8960a, b278d88300, 0d0521a78b |
| ☐ | Save an image: long-press an image in chat → Save | saved to gallery (loads OpenSSL on use) | ea6027ada6 |
| ☐ | Copy an image: long-press an image → Copy | pasted elsewhere OK | ea6027ada6 |
| ☐ | Browser: open status.app, a dApp page, go back to wallet | renders; returning works; native WebView visible | a5cfbbf46c |
| ☐ | Any other native view (file picker, share sheet, camera) shows over the app | visible, not black/invisible | a5cfbbf46c |
| ☐ | Back gesture (edge swipe) on chat/settings/browser | goes back, no conflict with in-app swipes | a5cfbbf46c |
| ☐ | Basic style look: buttons, switches, combos, text fields, sliders, scroll bars across Settings | look as before (no system/Material look, no missing indicators) | be7ed86263 |
| ☐ | ▴▾ price-change triangles in the wallet asset list | shown next to % changes | dfbf7694bb |
| ☐ | ENS: Settings → ENS usernames → search a name | flow works (StateMachine removed) | 9b8f6edd40 |
| ☐ | Syncing: Settings → Syncing → "Setup syncing" / show sync code | flow works | 9b8f6edd40, d1f031e4d6 |
| ☐ | Onboarding "Log in by syncing" and "Log in with recovery phrase" (fresh install or Add profile on the login screen) | flows open and proceed | 9f0e0ed492 |

## 3. Popups / views now loaded on first open (by section)
Expect each to open normally; the first open may take a few ms more. Opening once is enough.

### Login screen / onboarding
| | UI path | commit |
|---|---|---|
| ✔ | Language selector (globe "EN" top right) → list shows all languages incl. 한국어; pick one, confirm | 3467eff941 [Redmi replay screenshot] |
| ✔ | Fresh install: Create profile → "Let's go" → password page (Save password off) → wallet | 87369e75a3 [Lenovo + Redmi setup] |
| ☐ | Login screen profile dropdown → "Create new profile" | f6bbc5db3d, 87369e75a3 |
| ☐ | Profile dropdown → "Log in" (new account login page) | 87369e75a3 |
| ✔ | Fresh install only: welcome page (first screen) renders fully (logo, Create profile / Log in, carousel) | 45457d8a0f [Lenovo + Redmi setup, this APK] |
| ☐ | Welcome page → privacy policy / terms / third-party services info links | 45457d8a0f |
| ☐ | Keycard login paths (if a keycard profile exists): keycard lost page, convert keycard account | 87369e75a3 |
| ☐ | Corrupt/locked accounts error dialog (hard to trigger; skip unless known) | 2e529e87a1 |

### App shell
| | UI path | commit |
|---|---|---|
| ☐ | Search (magnifier in a section header) → type → results | 2c5298f5da |
| ☐ | Profile button (bottom-left avatar) → status menu (online/offline, view profile, copy link) | 45457d8a0f |
| ☐ | Activity center (bell) → notification list | 6cb70d25d3 |
| ☐ | Enable notifications prompt (fresh profile or after "Maybe later"): Continue / Maybe later | 45457d8a0f |
| ☐ | Enable biometrics prompt (if offered after login) | 45457d8a0f |
| ☐ | Backup banner → "Back up now" → backup seed modal | 6cb70d25d3 |

### Wallet
| | UI path | commit |
|---|---|---|
| ☐ | Send → simple send modal → sign | a2647a321c, 8172f9ad3b |
| ☐ | Receive card → receive modal (QR, copy address) | 45457d8a0f |
| ☐ | Swap → swap modal | a2647a321c |
| ☐ | Buy → buy crypto modal | 6cb70d25d3 |
| ☐ | "+" next to Wallet → add account popup; import keypair | a2647a321c |
| ☐ | Account row long-press/menu → Remove account → confirmation (watch-only account easiest) | 45457d8a0f |
| ☐ | Saved addresses (left panel) → list; Add / Edit / Remove saved address; open a saved address' activity | 45457d8a0f, 16cdfee628 |
| ☐ | Onchain friends (following addresses) | — (stays as before) |
| ☐ | Tap an asset → asset details view | 6cb70d25d3 |
| ☐ | Collectibles tab → tap one → collectible details | 6cb70d25d3, ef97f64341 |
| ☐ | History tab → activity list | a2647a321c |
| ☐ | Network filter (chain icons, top right) → network list | — |
| ☐ | First token received popup (receive a token on a test account; optional) | 16cdfee628 |
| ☐ | Wallet skeletons while loading (fresh start, slow network) appear and disappear | 61b468d4dc |
| ☐ | dApps button → connect via WalletConnect (pair modal, copy instructions); connect dApp modal; disconnect confirmation; sign request | ef97f64341, 16cdfee628, 48b3765feb |
| ☐ | Testnet mode toggle (Settings → Wallet → Testnet) → testnet popup | 16cdfee628 |

### Chat
| | UI path | commit |
|---|---|---|
| ☐ | "+" → create chat view | ef97f64341 |
| ☐ | Chat input: emoji picker, stickers, GIF picker, image attach (file dialog) | 2c5298f5da, 16cdfee628, 48b3765feb, 0479b7097d |
| ☐ | Tap an image in a chat → image viewer; download image | 48b3765feb |
| ☐ | Tap a link in a message → "open external link" confirmation | 48b3765feb |
| ☐ | Tap a video message → video modal | 2c5298f5da |
| ☐ | Chat header menu → pinned messages | 16cdfee628 |
| ☐ | Contact profile → nickname, mark untrusted, verify, remove/block/unblock contact, send/review contact request | 8bc5f1153d |
| ☐ | Your own profile → share profile (QR) | 16cdfee628 |
| ☐ | UI sounds: send a message (send sound), receive one (notification sound) | 7860a0e7eb, 1e0ae314cb |

### Communities
| | UI path | commit |
|---|---|---|
| ✔ | Communities portal opens | [Lenovo smoke w-final] |
| ☐ | Create community; create channel | a2647a321c |
| ☐ | Join a community (join dialog, rules, shared addresses) | 6cb70d25d3, 8bc5f1153d |
| ☐ | Token-gated community you are not a member of → join-community view | 45457d8a0f |
| ☐ | Community fetch dialog: open a community link you haven't loaded (spinner / failed / retry) | 45457d8a0f |
| ☐ | Community menu → invite friends; import community; leave community | 8bc5f1153d, 48b3765feb |
| ☐ | Community profile popup; member messages popup | 16cdfee628 |
| ☐ | Owner flows (if available): export/import control node, transfer ownership, finalise/decline ownership | 16cdfee628, 6cb70d25d3, 48b3765feb |
| ☐ | Banned / control-node-offline views (only if such a community exists) | 45457d8a0f |
| ☐ | Discord import progress (desktop feature; skip on mobile) | 2c5298f5da |

### Browser
| | UI path | commit |
|---|---|---|
| ✔ | Open Browser, load status.app, back to wallet | [Lenovo smoke w-final] |
| ☐ | "⋮" settings menu (mobile) → zoom, find, clear data, incognito, supported formats, settings | 0a57e424a6, 5c773c11cb |
| ☐ | Tabs/bookmarks overview; add favorite; downloads page | 16cdfee628 |
| ☐ | Wallet menu in the browser (account selector) | 8bc5f1153d |
| ☐ | SSL error dialog (open https://expired.badssl.com) | 5c773c11cb |
| ☐ | Privacy mode: Settings → Privacy → turn third-party services off → open Browser and Market | privacy wall shows (loaded on first entry) | 0a57e424a6 |

### Settings
| | UI path | commit |
|---|---|---|
| ☐ | Open every entry once: Profile, Password, Contacts, ENS, Messaging, Wallet, Appearance, Language & Currency, Notifications, Syncing, Browser, Advanced, About (+ terms, privacy), Communities, Keycard, Privacy and security, Backup, P2P network (Logos network) | d1f031e4d6, 8bc5f1153d, a2647a321c, 45457d8a0f |
| ☐ | Profile → edit picture (image crop) | 8bc5f1153d, 0479b7097d |
| ☐ | Privacy → third-party services info popup | 45457d8a0f |
| ☐ | Language & Currency → language selector → pick a language → confirm dialog | 3467eff941 |
| ✔ | Notifications → Volume slider → sound plays | 1e0ae314cb [Redmi real app, logcat `SoundPlayer: playing sound 1`] |
| ☐ | Wallet → saved addresses (settings variant) | 45457d8a0f |
| ☐ | Backup → choose folder (folder dialog) | 0479b7097d |

## 4. L1–L4 specific
| | what to do | expect | commit |
|---|---|---|---|
| ✔ | Login screen → language dropdown | opens, 한국어 visible (first open +5..10 ms) | 3467eff941 [Redmi replay] |
| ✔ | UI sound on Android | plays via SoundPool | 1e0ae314cb [Redmi real app] |
| ☐ | Receive modal from the wallet card and from an account menu | QR + switchable account / fixed account | 45457d8a0f |
| ☐ | Remove a watch-only account | confirmation, then removed | 45457d8a0f |
| ☐ | Saved addresses → send to a saved address | send modal prefilled | 45457d8a0f |
| ☐ | Profile status menu → set Offline, then Automatic | badge colour changes | 45457d8a0f |
| ☐ | Privacy mode (third-party off) → Browser/Market walls | shown | 0a57e424a6 |
