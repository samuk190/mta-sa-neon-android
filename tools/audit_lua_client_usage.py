#!/usr/bin/env python3
"""Rank missing/stub Lua APIs used by client/shared scripts declared in meta.xml.

Global-call syntax only: comments/strings are masked; lexical scope, aliases, OOP, generated
code and compiled Lua are not resolved. This measures source usage, not runtime
frequency or a guarantee that every listed resource runs on the server.
"""
import argparse
import collections
import json
import pathlib
import re
import xml.etree.ElementTree as ET

LONG = re.compile(r"\[(=*)\[")
CALL = re.compile(r"(?<![\w.:])([A-Za-z_]\w*)\s*\(")


def mask_lua(source):
    result = list(source)
    at = 0
    while at < len(source):
        begin = at
        comment = source.startswith("--", at)
        if comment:
            at += 2
        bracket = LONG.match(source, at)
        if bracket:
            end = source.find("]" + bracket[1] + "]", bracket.end())
            at = len(source) if end < 0 else end + len(bracket[1]) + 2
        elif comment:
            end = source.find("\n", at)
            at = len(source) if end < 0 else end
        elif source[at] in "\"'":
            quote = source[at]
            at += 1
            while at < len(source):
                if source[at] == "\\":
                    at += 2
                elif source[at] == quote:
                    at += 1
                    break
                else:
                    at += 1
        else:
            at += 1
            continue
        for index in range(begin, min(at, len(source))):
            if result[index] != "\n":
                result[index] = " "
    return "".join(result)


def audit(root, catalog):
    apis = {}
    for file in catalog.glob("*.md"):
        for line in file.read_text().splitlines():
            fields = line.split("|")
            if len(fields) > 3 and fields[2].strip() in {"Missing", "*Stub*"}:
                apis[fields[1].strip().strip("`")] = fields[2].strip()
    scripts, errors = {}, []
    for meta in sorted(root.rglob("meta.xml")):
        try:
            document = ET.parse(meta)
        except (OSError, ET.ParseError) as error:
            errors.append({"meta": str(meta.relative_to(root)), "error": str(error)})
            continue
        for script in document.getroot().findall("script"):
            if script.get("type", "server").lower() not in {"client", "shared"}:
                continue
            src = script.get("src", "").replace("\\", "/")
            if not src:
                continue
            literal = meta.parent / src
            try:
                paths = [literal] if literal.is_file() else list(meta.parent.glob(src))
            except (OSError, ValueError, NotImplementedError):
                paths = []
            valid = [p.resolve() for p in paths if p.is_file() and p.resolve().is_relative_to(root)]
            if not valid:
                errors.append({"meta": str(meta.relative_to(root)), "missing_script": src})
            for path in valid:
                scripts[path] = str(meta.parent.relative_to(root))
    calls, files, resources, examples = (collections.defaultdict(int), collections.defaultdict(set),
                                          collections.defaultdict(set), collections.defaultdict(list))
    compiled = 0
    for file, resource in sorted(scripts.items()):
        data = file.read_bytes()
        if data.startswith(b"\x1bLua") or b"\0" in data:
            compiled += 1
            continue
        source = mask_lua(data.decode("utf-8", errors="replace"))
        relative = str(file.relative_to(root))
        for match in CALL.finditer(source):
            name = match[1]
            if name not in apis or re.search(r"\bfunction\s*$", source[max(0, match.start() - 32):match.start()]):
                continue
            calls[name] += 1
            files[name].add(relative)
            resources[name].add(resource)
            if len(examples[name]) < 5:
                examples[name].append({"file": relative, "line": source.count("\n", 0, match.start()) + 1})
    ranking = [{"function": name, "status": apis[name], "resources": len(resources[name]),
                "files": len(files[name]), "calls": count, "examples": examples[name]}
               for name, count in calls.items()]
    ranking.sort(key=lambda row: (-row["resources"], -row["files"], -row["calls"], row["function"]))
    return {"root": str(root), "declared_scripts": len(scripts), "compiled_skipped": compiled,
            "errors": errors, "ranking": ranking}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("--catalog", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent / "docs/lua-functions")
    parser.add_argument("--json-out", type=pathlib.Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()
    if not args.root.is_dir():
        parser.error("resource root must be a directory")
    if not args.catalog.is_dir():
        parser.error("catalog directory not found")
    report = audit(args.root.resolve(), args.catalog)
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(f"Declared client/shared scripts: {report['declared_scripts']}; compiled skipped: {report['compiled_skipped']}; metadata/file errors: {len(report['errors'])}")
    print("Function | Resources | Files | Static calls")
    for row in report["ranking"][:args.top]:
        print(f"{row['function']} | {row['resources']} | {row['files']} | {row['calls']}")


if __name__ == "__main__":
    main()
