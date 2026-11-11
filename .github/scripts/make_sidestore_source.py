#!/usr/bin/env python3
"""Writes the SideStore / AltStore source (source.json) for one iOS build.

The version numbers come from the built app's Info.plist, because SideStore checks
that the downloaded app matches what the source says.

Usage:
  make_sidestore_source.py --app myDAW.app --ipa myDAW-iOS.ipa --repo owner/name \
                           --tag build-42 --sha <commit> --out source.json
"""

import argparse
import datetime
import json
import os
import plistlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, help="the built .app bundle")
    parser.add_argument("--ipa", required=True, help="the packaged .ipa")
    parser.add_argument("--repo", required=True, help="GitHub owner/name")
    parser.add_argument("--tag", required=True, help="release tag the .ipa is attached to")
    parser.add_argument("--sha", required=True, help="commit the build was made from")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    with open(os.path.join(args.app, "Info.plist"), "rb") as f:
        info = plistlib.load(f)

    version = info["CFBundleShortVersionString"]
    build = info["CFBundleVersion"]
    bundle_id = info["CFBundleIdentifier"]
    min_os = info.get("MinimumOSVersion", "15.0")

    repo_url = f"https://github.com/{args.repo}"
    ipa_url = f"{repo_url}/releases/download/{args.tag}/{os.path.basename(args.ipa)}"
    icon_url = f"https://raw.githubusercontent.com/{args.repo}/{args.sha}/docs/icon.png"
    size = os.path.getsize(args.ipa)
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    notes = f"Build {build} ({args.sha[:7]}). Changes: {repo_url}/commits/{args.sha}"

    description = (
        "パターン（小節のブロック）を作って、パズルのように並べて曲を作る DAW です。"
        "サンプルの打ち込み、ピアノロール、AUv3 プラグイン、サンプラー、AI ステム分離に対応。\n\n"
        "A pattern-based DAW: build patterns from samples and AUv3 instruments, "
        "arrange them like puzzle pieces, sample and split songs into stems with AI."
    )

    source = {
        "name": "myDAW",
        "identifier": f"{bundle_id}.source",
        "subtitle": "myDAW for iPhone and iPad",
        "description": description,
        "iconURL": icon_url,
        "website": repo_url,
        "tintColor": "#4FC3F7",
        "apps": [
            {
                "name": "myDAW",
                "bundleIdentifier": bundle_id,
                "developerName": args.repo.split("/")[0],
                "subtitle": "Pattern-based DAW",
                "localizedDescription": description,
                "iconURL": icon_url,
                "tintColor": "#4FC3F7",
                "category": "entertainment",
                "screenshots": [],
                "versions": [
                    {
                        "version": version,
                        "buildVersion": build,
                        "date": date,
                        "localizedDescription": notes,
                        "downloadURL": ipa_url,
                        "size": size,
                        "minOSVersion": min_os,
                    }
                ],
                # the unsigned build declares no entitlements or privacy permissions
                "appPermissions": {"entitlements": [], "privacy": {}},
                # older SideStore / AltStore versions read these instead of "versions"
                "version": version,
                "versionDate": date,
                "versionDescription": notes,
                "downloadURL": ipa_url,
                "size": size,
            }
        ],
        "news": [],
    }

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(source, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"{args.out}: {bundle_id} {version} ({build}), {size} bytes -> {ipa_url}")


if __name__ == "__main__":
    main()
