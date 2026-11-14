#!/usr/bin/env python3
"""Keep command dispatch independent of process-wide retry policy."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
errors = []
legacy = re.compile(r"\b(?:setDefaultAskRetry|defaultAskRetry|s_defaultAskRetry)\b")
paths = list((root / "launcher/cli").glob("*"))
paths += [root / "launcher/Application.cpp", root / "launcher/net/NetJob.cpp", root / "launcher/net/NetJob.h"]
for path in paths:
    if path.suffix not in {".cpp", ".h"}:
        continue
    content = path.read_text()
    if legacy.search(content):
        errors.append(f"{path}: legacy global retry policy")
    if path.parent.name == "cli" and path.suffix == ".cpp" and path.name != "TaskRunner.cpp":
        if re.search(r"\brunTaskSync\s*\(|->start\s*\(", content):
            errors.append(f"{path}: task launch bypasses the unattended boundary")
    if path.parent.name == "net" and "isHeadlessRun" in content:
        errors.append(f"{path}: process-wide headless retry fallback")
if errors:
    sys.exit("\n".join(errors))
print("CLI operations use the unattended boundary without global retry policy")
