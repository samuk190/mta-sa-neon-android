#!/usr/bin/env python3
"""Run the production Kotlin browser/address code with JVM JSON and coroutine libraries."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json-jar", type=Path, required=True, help="JVM org.json jar (Android supplies JSON on device)")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--registry-fixture", type=Path, help="Optional downloaded Neon registry JSON")
    parser.add_argument("--gradle-cache", type=Path, default=Path.home() / ".gradle/caches/modules-2/files-2.1")
    parser.add_argument("--java", default="java")
    parser.add_argument("--javac", default="javac")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]

    def jar(group, name, version):
        return next((args.gradle_cache / group / name / version).glob("*/*.jar"))

    compiler = jar("org.jetbrains.kotlin", "kotlin-compiler-embeddable", "1.9.20")
    stdlib = jar("org.jetbrains.kotlin", "kotlin-stdlib", "1.9.20")
    reflect = jar("org.jetbrains.kotlin", "kotlin-reflect", "1.6.10")
    runtime = jar("org.jetbrains.kotlin", "kotlin-script-runtime", "1.9.20")
    trove = jar("org.jetbrains.intellij.deps", "trove4j", "1.0.20200330")
    coroutines = jar("org.jetbrains.kotlinx", "kotlinx-coroutines-core-jvm", "1.7.3")
    annotations = jar("org.jetbrains", "annotations", "13.0")
    args.build_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.javac, "-d", str(args.build_dir), str(root / "app/build/generated/source/buildConfig/debug/com/worldrpg/neonlauncher/BuildConfig.java")], check=True)
    classpath = os.pathsep.join(map(str, (stdlib, coroutines, annotations, args.json_jar, args.build_dir)))
    compiler_classpath = os.pathsep.join(map(str, (compiler, stdlib, reflect, runtime, trove, annotations)))
    source = root / "app/src/main/java/com/worldrpg/neonlauncher/network"
    files = [source / (name + ".kt") for name in ("ServerAddress", "ServerInfo", "ServerQuery", "NeonServerRegistry", "ServerBrowserService")]
    files.append(root / "tools/headless/kotlin/ServerBrowserRegressionTest.kt")
    subprocess.run([args.java, "-cp", compiler_classpath, "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                    "-jvm-target", "17", "-classpath", classpath, "-d", str(args.build_dir), *map(str, files)], check=True)
    command = [args.java, "-cp", classpath, "com.worldrpg.neonlauncher.network.ServerBrowserRegressionTestKt"]
    if args.registry_fixture:
        command.append(str(args.registry_fixture))
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
