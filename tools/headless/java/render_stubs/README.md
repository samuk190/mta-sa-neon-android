# DX view test doubles

These minimal classes run the production `MtaDxView` methods on a host JVM.
They model command ordering, bitmap ownership, opaque source-over composition,
font loading and line placement. They do not validate Android glyph rendering,
GPU composition, SVG parsing or chat color parsing.

The runner first compiles the production view against the real Android SDK,
then compiles these doubles separately. They are outside `app/src` and never
ship in the APK.

From the repository root:

```sh
python3 tools/headless/run_dx_view_tests.py \
  --android-jar "$ANDROID_HOME/platforms/android-34/android.jar" \
  --build-dir /tmp/neon-dx-view-tests
```
