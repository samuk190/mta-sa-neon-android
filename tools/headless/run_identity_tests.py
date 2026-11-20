#!/usr/bin/env python3
"""Compile and run the production Neon Identity API against an in-memory service."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json-jar", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--android-jar", type=Path, required=True,
                        help="Android SDK android.jar to compile the production proof provider")
    parser.add_argument("--gradle-cache", type=Path, default=Path.home() / ".gradle/caches/modules-2/files-2.1")
    parser.add_argument("--java", default="java")
    args = parser.parse_args()

    def jar(group, name, version):
        return next((args.gradle_cache / group / name / version).glob("*/*.jar"))

    compiler = jar("org.jetbrains.kotlin", "kotlin-compiler-embeddable", "1.9.20")
    stdlib = jar("org.jetbrains.kotlin", "kotlin-stdlib", "1.9.20")
    reflect = jar("org.jetbrains.kotlin", "kotlin-reflect", "1.6.10")
    runtime = jar("org.jetbrains.kotlin", "kotlin-script-runtime", "1.9.20")
    trove = jar("org.jetbrains.intellij.deps", "trove4j", "1.0.20200330")
    coroutines = jar("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.7.3")
    annotations = jar("org.jetbrains", "annotations", "13.0")
    root = Path(__file__).resolve().parents[2]
    args.build_dir.mkdir(parents=True, exist_ok=True)
    classpath = os.pathsep.join(map(str, (stdlib, coroutines, annotations, args.json_jar, args.build_dir)))
    compiler_classpath = os.pathsep.join(map(str, (compiler, stdlib, reflect, runtime, trove, annotations)))
    network = root / "app/src/main/java/com/worldrpg/neonlauncher/network"
    proof = root / "app/src/main/java/org/mtamobile/identity/AndroidTicketProof.kt"
    files = [network / "NeonIdentityApi.kt", network / "ServerAddress.kt", proof,
             root / "tools/headless/kotlin/NeonIdentityRegressionTest.kt"]
    subprocess.run([args.java, "-cp", compiler_classpath, "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                    "-jvm-target", "17", "-classpath", classpath + os.pathsep + str(args.android_jar), "-d", str(args.build_dir), *map(str, files)], check=True)
    subprocess.run([args.java, "-cp", classpath, "com.worldrpg.neonlauncher.network.NeonIdentityRegressionTestKt"], check=True)


if __name__ == "__main__":
    main()
