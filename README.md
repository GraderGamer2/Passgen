<p align="center"><img src="icon.png" width="128" alt="PassGen icon"></p>

# PassGen

A tiny, offline password generator. Nothing else.

- **Linux (Wayland):** one ~56 KB native binary. No GTK/Qt/libwayland: it speaks the Wayland protocol directly and renders its own UI. Links only libc + libm.
- **Android:** one ~50 KB APK, plain framework APIs only (no AndroidX, no Gradle, no WebView).
- Both: length 1–128, lowercase/uppercase/digits/symbols, "avoid look-alikes", entropy meter, light/dark toggle (sun/moon, top right), Quicksand font.

## Security notes
- Randomness: `getrandom(2)` on Linux, `SecureRandom` on Android. Unbiased selection (rejection sampling), Fisher-Yates shuffle.
- Nothing is stored or logged. Clipboard is flagged as sensitive and cleared after 30 s (Linux: only if your password is still the selection). Android blocks screenshots (`FLAG_SECURE`).
- **Check** button (opt-in, the only network use): queries [Have I Been Pwned's Pwned Passwords](https://haveibeenpwned.com/API/v3#PwnedPasswords) with k-anonymity. Only the first 5 hex chars of the SHA-1 leave the device. On Linux it shells out to `curl` (button hidden if missing).

## Get it
Push to GitHub: the **build** workflow produces both artifacts (Actions tab). Push a tag like `v1.0` to publish a Release with the Linux binary and the APK.

### Linux
```sh
sh linux/build.sh        # needs gcc + libc headers; output: linux/passgen
sh linux/install.sh      # optional: installs to ~/.local/bin and adds an app-menu entry
```
The icon (`linux/passgen.svg`, a padlock with dice pips) is installed by `install.sh`; Android uses vector versions in `android/res`.
Window: drag the header to move, Esc quits. `PASSGEN_SCALE=1.8 passgen` changes the default size.

### Android
```sh
ANDROID_HOME=/path/to/sdk sh android/build.sh    # output: android/passgen.apk
```
In CI no setup is needed. To keep one stable signing key (so updates install over old versions), add repo secrets `ANDROID_KEYSTORE_B64` (`base64 -w0 release.jks`) and `ANDROID_KEYSTORE_PASSWORD`. Create a key with:
`keytool -genkeypair -keystore release.jks -alias passgen -keyalg RSA -keysize 4096 -validity 36500`

## Licenses
Code: MIT (see `LICENSE`). Quicksand font: SIL OFL 1.1 (`OFL-Quicksand.txt`). `linux/stb_truetype.h`: public domain / MIT (Sean Barrett).
