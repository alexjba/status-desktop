#!/bin/bash
set -e
cd "$(dirname "$0")"
BT=~/Library/Android/sdk/build-tools/35.0.0; AJ=~/Library/Android/sdk/platforms/android-35/android.jar
export JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home PATH=/Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home/bin:$PATH
mk(){ pkg=$1 dbg=$2
sed -e "s/@PKG@/$pkg/" -e "s/@DBG@/$dbg/" AndroidManifest.in > out-manifest.xml
rm -rf out && mkdir -p out/cls out/dex
javac -source 8 -target 8 -bootclasspath $AJ -nowarn -d out/cls src/app/status/fplat/LoginActivity.java 2>&1 | grep -v "^warning\|^Note\|^1 warning" || true
d8 --release --min-api 28 --lib $AJ --output out/dex $(find out/cls -name '*.class')
aapt2 link -o out/u.apk -I $AJ --manifest out-manifest.xml
(cd out/dex && zip -q ../u.apk classes.dex)
zipalign -f 4 out/u.apk out/a.apk
apksigner sign --ks ~/.android/debug.keystore --ks-pass pass:android --key-pass pass:android --out $pkg.apk out/a.apk
}
export PATH=$BT:$PATH
mk app.status.fplat.login false
mk app.status.fplat.logindbg true
