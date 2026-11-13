#!/bin/bash
# Builds the macOS installer: PeakTime-<version>-macOS.pkg
# usage: build_pkg.sh <version> <artefacts dir> <output dir>
# Installs VST3 + AU into /Library/Audio/Plug-Ins and the standalone app into /Applications.
# Package identifiers never change, so a newer version installs straight over an older one.
set -euo pipefail

VERSION="$1"
ART="$2"
OUT="$3"
ID="com.peaktimeaudio.peaktime"
WORK="$(mktemp -d)"
mkdir -p "$OUT"

# ad-hoc sign so Apple Silicon hosts will load the bundles
for b in "$ART/VST3/PeakTime.vst3" "$ART/AU/PeakTime.component" "$ART/Standalone/PeakTime.app"; do
    codesign --force --deep --sign - "$b"
done

mkdir -p "$WORK/vst3" "$WORK/au" "$WORK/app" "$WORK/scripts-au"
cp -R "$ART/VST3/PeakTime.vst3"      "$WORK/vst3/"
cp -R "$ART/AU/PeakTime.component"   "$WORK/au/"
cp -R "$ART/Standalone/PeakTime.app" "$WORK/app/"

# refresh the Audio Unit cache after install so Logic / Ableton see the new version
cat > "$WORK/scripts-au/postinstall" <<'EOS'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOS
chmod +x "$WORK/scripts-au/postinstall"

# keep bundles at their fixed install locations (never "relocate" to an old copy elsewhere)
for c in vst3 au app; do
    pkgbuild --analyze --root "$WORK/$c" "$WORK/$c.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$c.plist"
done

pkgbuild --root "$WORK/vst3" --component-plist "$WORK/vst3.plist" --identifier "$ID.vst3" --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/PeakTime-VST3.pkg"
pkgbuild --root "$WORK/au" --component-plist "$WORK/au.plist" --identifier "$ID.au" --version "$VERSION" \
         --scripts "$WORK/scripts-au" --install-location "/Library/Audio/Plug-Ins/Components" "$WORK/PeakTime-AU.pkg"
pkgbuild --root "$WORK/app" --component-plist "$WORK/app.plist" --identifier "$ID.app" --version "$VERSION" \
         --install-location "/Applications" "$WORK/PeakTime-App.pkg"

productbuild --synthesize \
    --package "$WORK/PeakTime-VST3.pkg" --package "$WORK/PeakTime-AU.pkg" --package "$WORK/PeakTime-App.pkg" \
    "$WORK/distribution.xml"
perl -0pi -e "s|<installer-gui-script minSpecVersion=\"1\">|<installer-gui-script minSpecVersion=\"1\">\n    <title>PeakTime $VERSION</title>|" "$WORK/distribution.xml"

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT/PeakTime-$VERSION-macOS.pkg"
echo "Built $OUT/PeakTime-$VERSION-macOS.pkg"
