#!/usr/bin/env python3
"""Compile the real DX view against the SDK, then test it with Android doubles."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--android-jar", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--javac", default="javac")
    parser.add_argument("--java", default="java")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tools/headless/java/render_stubs"
    game = root / "app/src/main/java/com/saonline/neonlauncher/game"
    product = args.build_dir / "product"
    doubles = args.build_dir / "doubles"
    product.mkdir(parents=True, exist_ok=True)
    doubles.mkdir(parents=True, exist_ok=True)
    # Compile production methods with the real SDK signatures. The SVG/chat
    # adapters aren't exercised here and keep this test independent of Gradle.
    source = [game / name for name in ("MtaDxView.java", "DxFrameQueue.java", "DxFontCache.java", "DxTextLayout.java",
                                       "InterfaceScale.java", "HudLayout.java")]
    source += list((fixtures / "com").rglob("*.java"))
    subprocess.run([args.javac, "-cp", str(args.android_jar), "-d", str(product), *map(str, source)], check=True)
    source = list((fixtures / "android").rglob("*.java"))
    source += [root / "tools/headless/java/DxViewRegressionTest.java"]
    subprocess.run([args.javac, "-cp", os.pathsep.join(map(str, (product, args.android_jar))),
                    "-d", str(doubles), *map(str, source)], check=True)
    subprocess.run([args.java, "-cp", os.pathsep.join(map(str, (doubles, product, args.android_jar))),
                    "com.saonline.neonlauncher.game.DxViewRegressionTest"], check=True)


if __name__ == "__main__":
    main()
