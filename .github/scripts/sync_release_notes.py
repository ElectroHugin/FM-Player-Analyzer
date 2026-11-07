#!/usr/bin/env python3
"""Write each version's section of CHANGELOG.md into its GitHub release.

Runs in the Release workflow after every push to main. For every
"## X.Y.Z" section whose release vX.Y.Z exists, the release text becomes that
section. A "Full Changelog" line and the code-signing note already on the
release are kept. Releases without a section stay as they are.
Needs GH_TOKEN and GITHUB_REPOSITORY (set by GitHub Actions).
"""
import json
import os
import re
import subprocess
import sys

KEEP = ("**Full Changelog**", "Signed Windows installer.")


def sections(text):
    result = {}
    current = None
    for line in text.splitlines():
        heading = re.match(r"^## (\d+\.\d+\.\d+)\b", line)
        if heading:
            current = heading.group(1)
            result[current] = []
        elif line.startswith("## "):
            current = None
        elif current:
            result[current].append(line)
    return {version: "\n".join(lines).strip() for version, lines in result.items()}


def gh(*args, payload=None):
    return subprocess.run(["gh", *args], input=payload, capture_output=True, text=True)


def main():
    repo = os.environ["GITHUB_REPOSITORY"]
    path = sys.argv[1] if len(sys.argv) > 1 else "CHANGELOG.md"
    with open(path, encoding="utf-8") as f:
        notes = sections(f.read())
    for version, body in notes.items():
        tag = f"v{version}"
        found = gh("api", f"repos/{repo}/releases/tags/{tag}")
        if found.returncode != 0:
            print(f"{tag}: no release yet, skipped")
            continue
        release = json.loads(found.stdout)
        kept = [line for line in (release.get("body") or "").splitlines()
                if line.startswith(KEEP)]
        new_body = "\n\n".join([body, *kept]).strip() + "\n"
        if (release.get("body") or "").strip() == new_body.strip():
            print(f"{tag}: unchanged")
            continue
        update = gh("api", "-X", "PATCH", f"repos/{repo}/releases/{release['id']}",
                    "--input", "-", payload=json.dumps({"body": new_body}))
        if update.returncode != 0:
            print(f"{tag}: update failed: {update.stderr}", file=sys.stderr)
            return 1
        print(f"{tag}: release notes updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
