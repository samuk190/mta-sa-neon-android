#!/usr/bin/env python3
"""Run the production Kotlin browser/address code with JVM JSON and coroutine libraries."""
import argparse
import os
from pathlib import Path
import re
import socket
import subprocess


def _udp_self_test(ip: str) -> bool:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, \
             socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind((ip, 0))
            port = server.getsockname()[1]
            server.settimeout(0.15)
            client.settimeout(0.15)
            client.connect((ip, port))
            client.send(b"r")
            data, peer = server.recvfrom(16)
            if data != b"r":
                return False
            server.sendto(b"ok", peer)
            return client.recv(16) == b"ok"
    except OSError:
        return False


def _pick_udp_test_address() -> str | None:
    if _udp_self_test("127.0.0.1"):
        return None
    candidates: list[str] = []
    try:
        out = subprocess.check_output(["ip", "-4", "-o", "addr", "show"], text=True, stderr=subprocess.DEVNULL)
        for m in re.finditer(r"\binet\s+(\d+\.\d+\.\d+\.\d+)/", out):
            ip = m.group(1)
            if ip != "127.0.0.1" and ip not in candidates:
                candidates.append(ip)
    except (OSError, subprocess.SubprocessError):
        pass
    for ip in candidates:
        if _udp_self_test(ip):
            return ip
    return None


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
    build_config = root / "app/build/generated/source/buildConfig/debug/com/saonline/neonlauncher/BuildConfig.java"
    if not build_config.is_file():
        build_config = root / "app/build/generated/source/buildConfig/release/com/saonline/neonlauncher/BuildConfig.java"
    if not build_config.is_file():
        build_config = args.build_dir / "BuildConfig.java"
        build_config.write_text(
            "package com.saonline.neonlauncher;\n"
            "public final class BuildConfig {\n"
            "  public static final boolean DEBUG = true;\n"
            "  public static final String DEFAULT_SERVER_NAME = \"World RPG\";\n"
            "  public static final String DEFAULT_SERVER_IP = \"127.0.0.1\";\n"
            "  public static final int DEFAULT_SERVER_PORT = 22003;\n"
            "  public static final String DEFAULT_SERVER_LOGO_URL = \"\";\n"
            "}\n",
            encoding="utf-8",
        )
    subprocess.run([args.javac, "-d", str(args.build_dir), str(build_config)], check=True)
    classpath = os.pathsep.join(map(str, (stdlib, coroutines, annotations, args.json_jar, args.build_dir)))
    compiler_classpath = os.pathsep.join(map(str, (compiler, stdlib, reflect, runtime, trove, annotations)))
    source = root / "app/src/main/java/com/saonline/neonlauncher/network"
    files = [source / (name + ".kt") for name in ("ServerAddress", "ServerInfo", "ServerQuery", "NeonServerRegistry", "ServerBrowserService")]
    files.append(root / "tools/headless/kotlin/ServerBrowserRegressionTest.kt")
    subprocess.run([args.java, "-cp", compiler_classpath, "org.jetbrains.kotlin.cli.jvm.K2JVMCompiler", "-no-stdlib", "-no-reflect",
                    "-jvm-target", "17", "-classpath", classpath, "-d", str(args.build_dir), *map(str, files)], check=True)
    command = [args.java, "-cp", classpath, "com.saonline.neonlauncher.network.ServerBrowserRegressionTestKt"]
    if args.registry_fixture:
        command.append(str(args.registry_fixture))
    env = os.environ.copy()
    if not env.get("NEON_TEST_UDP_ADDRESS"):
        picked = _pick_udp_test_address()
        if picked:
            env["NEON_TEST_UDP_ADDRESS"] = picked
    subprocess.run(command, check=True, env=env)


if __name__ == "__main__":
    main()
