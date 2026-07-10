# iOS default web browser entitlement (`com.apple.developer.web-browser`)

Research question: Can the Status app — a crypto messenger + wallet that also contains a
full in-app browser section (URL bar with direct navigation, multiple tabs, favorites,
downloads, rendered via a WebKit-based webview) — plausibly qualify for Apple's default
web browser entitlement? What are Apple's actual requirements, and is there a
"primary purpose = browsing" rule or is that folklore?

All claims below cite Apple primary sources only. Apple's documentation pages are
JS-rendered SPAs; the verbatim text was pulled from Apple's own documentation JSON API
(`developer.apple.com/tutorials/data/documentation/...json`), which is the same content
the HTML page renders.

## Primary sources

- Preparing your app to be the default web browser (Xcode docs):
  https://developer.apple.com/documentation/xcode/preparing-your-app-to-be-the-default-browser
- Entitlement reference `com.apple.developer.web-browser`:
  https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.web-browser
- Request form (login-gated):
  https://developer.apple.com/contact/request/default-browser-entitlement/
- App Review Guidelines, guideline 2.5.6:
  https://developer.apple.com/app-store/review/guidelines/
- Alternative browser engines (EU/Japan, separate program):
  https://developer.apple.com/support/alternative-browser-engines/
- Default apps updates (history of default-app categories):
  https://developer.apple.com/documentation/updates/defaultapps
- BrowserEngineKit (alternative-engine hosting, separate program):
  https://developer.apple.com/documentation/BrowserEngineKit

## The entitlement itself (verbatim)

From the entitlement reference page:

> An entitlement that indicates whether the app can act as the user's default web browser.

- Type: Boolean. Property List Key `com.apple.developer.web-browser`.
- Availability: iOS 14.0+, iPadOS 14.0+.
- It is a **managed entitlement** — you cannot self-assign it; Apple grants it on request,
  and the page's only "Discussion" content points back to
  "Preparing your app to be the default web browser."

Source: https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.web-browser

## Requirements — exact quotes (verbatim requirement list)

All quotes in this section are from
https://developer.apple.com/documentation/xcode/preparing-your-app-to-be-the-default-browser

Overview:

> In iOS 14 and later, users can select an app to be their default web browser. To make
> your app a choice, confirm that your app meets the requirements below, then request a
> managed entitlement.

Framing (note the wording — "web browsing apps," describing the role, not a
"primary purpose" test):

> The system invokes the default web browser in iOS whenever the user opens an HTTP or
> HTTPS link. Because this app becomes the user's primary gateway to the internet, Apple
> requires that web browsing apps meet specific functional criteria to protect user
> privacy and ensure proper access to internet resources.

### "Fulfill default browser requirements"

> Apps that register as a default web browser option must satisfy the following criteria:
>
> - Your app must specify the HTTP and HTTPS schemes in its `Info.plist` file.
> - Your app can't use `UIWebView`.
> - On launch, the app must provide a text field for entering a URL, search tools for
>   finding relevant links on the internet, or curated lists of bookmarks.
>
> When opening an HTTP or HTTPS URL in its default configuration:
>
> - The app must navigate directly to the specified destination and render the expected
>   web content. Apps that redirect to unexpected locations or render content not
>   specified in the destination's source code don't meet the requirements of a default
>   web browser.
> - Apps designed to operate in a parental controls or locked down mode may restrict
>   navigation to comply with those goals.
> - Your app may present a "Safe Browsing" or other warning for content suspected of
>   phishing or other problems.
> - Your app may offer a native authentication UI for a site that also offers a native web
>   sign-in flow.

### "Adhere to browser restrictions" (verbatim)

> Apps that have the `com.apple.developer.web-browser` managed entitlement may not claim
> to respond to Universal Links for specific domains. The system will ignore any such
> claims. Apps with the entitlement can still open Universal Links to other apps as usual.

> Because of their privileged position in a user's web browsing, browser apps should avoid
> unnecessary access to personal data. Apps that use any of the following Info.plist keys
> while using the `com.apple.developer.web-browser` managed entitlement will be rejected:

The rejection-triggering `Info.plist` keys (verbatim from the same page):

- `NSPhotoLibraryUsageDescription` — must use `NSPhotoLibraryAddUsageDescription` only;
  for individual photos use `PHPickerViewController` (no full photo-library access).
- `NSLocationAlwaysUsageDescription`, `NSLocationAlwaysAndWhenInUseUsageDescription` —
  "Browsers are restricted from always-on location access." Use
  `NSLocationWhenInUseUsageDescription`.
- `NSHomeKitUsageDescription` — "Browsers can't access the user's HomeKit database."
- `NSBluetoothAlwaysUsageDescription` — "Browsers can't poll for Bluetooth devices when
  the app is in the background." Use `NSBluetoothWhileInUseUsageDescription`.
- `NSHealthShareUsageDescription`, `NSHealthUpdateUsageDescription` — "Browsers can't
  access the user's health database."

### Capabilities granted (verbatim)

> Apps that use the `com.apple.developer.web-browser` managed entitlement can:
>
> - Be an option for the user to choose as their default browser.
> - Load pages from all domains with full script access.
> - Use Service Workers in `WKWebView` instances.
> - Offer the Add to Home Screen action in a share sheet by including the current
>   `WKWebView` in the activityItems array when creating a `UIActivityViewController`.

### App Review Guideline 2.5.6 (verbatim)

> Apps that browse the web must use the appropriate WebKit framework and WebKit
> JavaScript. You may apply for an entitlement to use an alternative web browser engine in
> your app. Learn more about these entitlements for the EU and Japan.

Source: https://developer.apple.com/app-store/review/guidelines/ (guideline 2.5.6).
This is the WebKit-rendering rule. It is NOT the default-browser entitlement — it applies
to *any* app that renders web content. It is satisfied for free by using `WKWebView`
(system WebKit). Alternative engines (BrowserEngineKit / EU-Japan program) are a separate
entitlement family and are irrelevant to Status.

## The request / application process

- The app expresses capability via the **managed** `com.apple.developer.web-browser`
  entitlement, but you must **request** it: "confirm that your app meets the requirements
  below, then request a managed entitlement."
  (Preparing your app to be the default web browser, Overview.)
- Requested by filling out the request form, verbatim from the page's Important aside:
  > Request the default browser entitlement by filling out the [Default Browser
  > Entitlement Request form]. In that form you can also request the
  > `com.apple.developer.browser.app-installation` entitlement. If you do that and your
  > request for the default browser entitlement is accepted you get both the default
  > browser entitlement and the app-installation entitlement for your browser app.
- Form URL: https://developer.apple.com/contact/request/default-browser-entitlement/ —
  this **redirects to Apple ID sign-in (`idmsa.apple.com`)**, i.e. it is gated behind an
  authenticated Apple Developer account. The field list could not be captured from a
  primary source without signing in, so it is not quoted here. (Confirmed: HTTP 302 to
  `idmsa.apple.com/IDMSWebAuth/signin.html`.)
- Because it is a managed entitlement, once granted it is tied to the account/App ID and
  materializes in the **provisioning profile**; you generate a new profile (development and
  distribution) carrying the entitlement rather than relying on Xcode automatic signing.
  Apple's primary docs describe it only as a "managed entitlement" requested via the form;
  the auto-signing / manual-profile mechanics and any processing-time figure are **not
  stated on the primary pages** — treat those as unconfirmed (commonly reported on the
  Apple Developer Forums, which are user posts, not Apple documentation).

## Assessment for the Status app

Status is a WebKit-backed (`WKWebView`) in-app browser section inside a messenger + wallet
app. Judged against each Apple requirement:

| Apple requirement (source: Preparing your app to be the default web browser, unless noted) | Verdict for Status | Notes |
|---|---|---|
| "Your app must specify the HTTP and HTTPS schemes in its `Info.plist` file." | Judgment call (actionable) | Purely a manifest change; add both schemes. Trivially satisfiable. |
| "Your app can't use `UIWebView`." | Clearly met | Status uses `WKWebView`-based webview on iOS, not the deprecated `UIWebView`. |
| "On launch, the app must provide a text field for entering a URL, search tools … or curated lists of bookmarks." | Judgment call | Status's browser section has a URL text field + favorites, but this is a *sub-section*, not what the app shows *on launch*. Apple says "On launch." If the app opens to chat, a strict reviewer could read this as unmet. See risk note below. |
| Guideline 2.5.6: "Apps that browse the web must use the appropriate WebKit framework and WebKit JavaScript." | Clearly met | System WebKit via `WKWebView`. No alternative engine involved. |
| "The app must navigate directly to the specified destination and render the expected web content" (no unexpected redirects / injected content). | Clearly met (design-dependent) | Standard `WKWebView` navigation renders the destination. Must ensure no interstitial redirect (e.g. bouncing http/https links into a wallet/dapp flow instead of loading them). |
| "may not claim to respond to Universal Links for specific domains" while holding the entitlement (system ignores such claims). | Judgment call / conflict risk | Status may rely on Universal Links / associated domains for deep-linking (invites, wallet, deep links). The entitlement makes iOS **ignore** the app's domain-specific Universal Link claims. This is a real functional trade-off, not just a checkbox. |
| Rejection keys: no `NSPhotoLibraryUsageDescription`, no always-on location, no HomeKit, no background Bluetooth, no Health keys. | Judgment call — highest rejection risk | A messenger+wallet plausibly requests **photo library** (attachments/avatars), **background Bluetooth** (hardware wallets), and possibly **location**. If any of the banned `Info.plist` keys are present, Apple states the app "will be rejected." This is the most concrete way Status could fail. |

### Is there a "primary purpose = web browsing" requirement?

No. That is **folklore**. Apple's primary text never conditions the entitlement on the
app's primary purpose or main function being web browsing. The operative sentences are
functional, not purpose-based:

- "Apps that register as a default web browser option must satisfy the following criteria:"
  (then a list of *functional* criteria).
- "Apple requires that web browsing apps meet specific functional criteria …"

The phrase "this app becomes the user's primary gateway to the internet" describes the
*role a default browser plays for the user*, not a requirement that browsing be the app's
primary feature. Nothing in the entitlement reference, the preparation article, or
guideline 2.5.6 says the app must be *primarily* a browser. So on the literal text, a
messenger that also contains a compliant browser is not disqualified by category.

Caveat: the entitlement is **managed** and granted by human review via a gated request
form. Apple reviewers have discretion. The "On launch … URL text field / search / curated
bookmarks" clause is the closest thing to a de-facto "the browser must be front and center"
signal, and a reviewer could apply it strictly against an app whose launch screen is a
chat list. But that is a launch-surface requirement, not a "primary purpose" rule, and it
is arguably satisfiable by routing the browser to a prominent, first-class entry point.

### Criteria a non-primarily-browser app is most likely to fail

1. **Banned `Info.plist` privacy keys** (hard rejection). Status very likely wants photo
   library, background Bluetooth (hardware wallets), and/or location — all of which Apple
   lists as automatic rejection triggers while holding this entitlement. This is the
   single biggest blocker and is a genuine architectural constraint, not a formality.
2. **"On launch" browser affordance** (judgment call). The app must present a URL field /
   search / bookmarks on launch; a chat-first launch surface is in tension with this.
3. **Universal Links suppression** (functional trade-off). Holding the entitlement makes
   iOS ignore the app's own domain Universal Link claims — Status would lose its
   domain-specific universal-link handling.
4. **Reviewer discretion** on a managed, human-granted entitlement — no purely mechanical
   guarantee even if all written criteria are met.

## Verdict (10-line summary)

1. Apple's default-browser entitlement (`com.apple.developer.web-browser`, iOS 14+) has a
   published, purely **functional** requirement list — no "primary purpose = browsing" rule.
2. The "primary purpose" belief is **folklore**; Apple's text says "web browsing apps must
   meet specific functional criteria," not "the app must primarily be a browser."
3. On category alone, a messenger-with-a-real-browser-tab is **not disqualified**.
4. Clearly met: WebKit/`WKWebView` (2.5.6), no `UIWebView`, direct navigation/rendering.
5. Easy to fix: declaring HTTP/HTTPS schemes in `Info.plist`.
6. Biggest risk (likely hard rejection): the **banned privacy `Info.plist` keys** — photo
   library, always-on location, HomeKit, background Bluetooth, Health. A wallet+messenger
   probably needs some of these (esp. photo library and background Bluetooth for hardware
   wallets); their presence means automatic rejection while holding the entitlement.
7. Judgment call: the "**on launch** provide a URL field / search / bookmarks" clause vs. a
   chat-first launch surface.
8. Functional trade-off: the entitlement makes iOS **ignore Status's Universal Link**
   domain claims.
9. It's a **managed** entitlement — requested via a login-gated Apple Developer form
   (which also optionally bundles the app-installation entitlement), granted per-account by
   human review, and carried in the provisioning profile.
10. Bottom line: **plausible but not clean.** The category is fine and most criteria are
    met or trivially fixable; the practical blockers are the banned privacy keys and the
    "on launch" browser-surface expectation, plus reviewer discretion — not a
    primary-purpose rule.
