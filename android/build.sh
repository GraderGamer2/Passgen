#!/bin/sh
# Needs: JDK 17+, Android SDK via ANDROID_HOME (preinstalled on GitHub runners). Output: passgen.apk
# Optional: KS=/path/release.jks KSPASS=secret to sign with your own key (otherwise a debug key is generated).
set -e; cd "$(dirname "$0")"
SDK=${ANDROID_HOME:-$ANDROID_SDK_ROOT}; [ -n "$SDK" ] || { echo "set ANDROID_HOME"; exit 1; }
BT=$(ls -d "$SDK"/build-tools/*/ | sort -V | tail -1); BT=${BT%/}
JAR=$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -1)
KS=${KS:-debug.jks}; KSPASS=${KSPASS:-android}

# Shrink Quicksand to one weight + ASCII (only if the bundled copy is missing)
if [ ! -f assets/quicksand.ttf ]; then
  curl -fL -o /tmp/q.ttf 'https://github.com/google/fonts/raw/main/ofl/quicksand/Quicksand%5Bwght%5D.ttf'
  fonttools varLib.instancer /tmp/q.ttf wght=500 -o /tmp/q500.ttf
  pyftsubset /tmp/q500.ttf --unicodes=U+0020-007E --layout-features= --no-hinting --desubroutinize --output-file=assets/quicksand.ttf
fi

rm -rf obj && mkdir obj
javac --release 8 -Xlint:-options -cp "$JAR" -d obj PassGen.java
"$BT"/d8 --release --min-api 24 --lib "$JAR" obj/app/passgen/*.class --output obj
"$BT"/aapt2 compile --dir res -o obj/res.zip
"$BT"/aapt2 link -o obj/base.apk -I "$JAR" --manifest AndroidManifest.xml -A assets obj/res.zip
(cd obj && zip -q -9 base.apk classes.dex)
"$BT"/zipalign -f 4 obj/base.apk obj/aligned.apk
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass "$KSPASS" -keypass "$KSPASS" -alias k -keyalg RSA -keysize 2048 -validity 10000 -dname CN=PassGen
"$BT"/apksigner sign --ks "$KS" --ks-pass pass:"$KSPASS" --v1-signing-enabled false --out passgen.apk obj/aligned.apk
ls -l passgen.apk
