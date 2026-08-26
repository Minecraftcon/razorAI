#!/usr/bin/env python3
"""
Razor Skill Packager
Packages all active skills, plugins, and custom agent tool definitions
from global and workspace locations into the local assets/ directory.
"""

import os
import sys
import json
import shutil
import re
from pathlib import Path

def parse_skill_frontmatter(skill_md_path):
    name = ""
    description = ""
    try:
        with open(skill_md_path, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
        if lines and lines[0].strip() == "---":
            for line in lines[1:]:
                line_str = line.strip()
                if line_str == "---":
                    break
                if line_str.startswith("name:"):
                    name = line_str[5:].strip().strip("\"'")
                elif line_str.startswith("description:"):
                    description = line_str[12:].strip().strip("\"'")
    except Exception as e:
        print(f"Warning: Failed to parse frontmatter for {skill_md_path}: {e}")

    if not name:
        name = Path(skill_md_path).parent.name
    return name, description

def main():
    repo_root = Path(__file__).resolve().parent.parent
    assets_dir = repo_root / "assets"
    skills_dest = assets_dir / "skills"
    plugins_dest = assets_dir / "plugins"

    skills_dest.mkdir(parents=True, exist_ok=True)
    plugins_dest.mkdir(parents=True, exist_ok=True)

    home = os.path.expanduser("~")
    razor_skills_dest = Path(home) / ".razor" / "skills"
    razor_skills_dest.mkdir(parents=True, exist_ok=True)
    search_roots = [
        ("global_skills", Path(home) / ".gemini" / "config" / "skills"),
        ("plugins", Path(home) / ".gemini" / "config" / "plugins"),
        ("builtin", Path(home) / ".gemini" / "antigravity-ide" / "builtin" / "skills"),
        ("workspace", repo_root / ".agents" / "skills"),
    ]

    manifest = []
    total_copied = 0

    print("=" * 60)
    print(" Razor Skill Packager")
    print(f" Destination: {assets_dir}")
    print("=" * 60)

    for source_type, root_path in search_roots:
        if not root_path.exists():
            continue

        print(f"\n[Scanning] {source_type} -> {root_path}")

        # Find all SKILL.md or skill.md files
        for skill_file in list(root_path.rglob("SKILL.md")) + list(root_path.rglob("skill.md")):
            skill_dir = skill_file.parent
            name, desc = parse_skill_frontmatter(skill_file)

            # Determine plugin or category
            rel_to_root = ""
            try:
                rel_to_root = str(skill_file.relative_to(root_path))
            except ValueError:
                rel_to_root = skill_file.name

            plugin_name = source_type
            if source_type == "plugins":
                # Find plugin directory name
                parts = skill_file.relative_to(root_path).parts
                if len(parts) > 0:
                    plugin_name = parts[0]

            # Copy skill directory to assets
            # Copy to assets and ~/.razor/skills
            if source_type == "plugins":
                dest_dir = plugins_dest / plugin_name / "skills" / skill_dir.name
            else:
                dest_dir = skills_dest / skill_dir.name

            dest_dir.mkdir(parents=True, exist_ok=True)
            razor_dest_dir = razor_skills_dest / skill_dir.name
            razor_dest_dir.mkdir(parents=True, exist_ok=True)

            # Copy all files from skill_dir (scripts, examples, references, etc.)
            for item in skill_dir.iterdir():
                dest_item = dest_dir / item.name
                razor_item = razor_dest_dir / item.name
                if item.is_dir():
                    if dest_item.exists():
                        shutil.rmtree(dest_item)
                    shutil.copytree(item, dest_item)
                    if razor_item.exists():
                        shutil.rmtree(razor_item)
                    shutil.copytree(item, razor_item)
                else:
                    shutil.copy2(item, dest_item)
                    shutil.copy2(item, razor_item)

            manifest_entry = {
                "name": name,
                "description": desc,
                "source_type": source_type,
                "plugin": plugin_name,
                "skill_path": str(dest_dir / skill_file.name),
                "razor_path": str(razor_dest_dir / skill_file.name),
                "rel_path": str((dest_dir / skill_file.name).relative_to(repo_root)),
            }
            manifest.append(manifest_entry)
            total_copied += 1
            print(f"  ✓ Packaged: {name:<35} [{plugin_name}]")

    # Write manifest.json
    manifest_path = assets_dir / "skills_manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    # Also copy manifest to ~/.razor/
    shutil.copy2(manifest_path, Path(home) / ".razor" / "skills_manifest.json")

    print("\n" + "=" * 60)
    print(f" Packaging Complete!")
    print(f" Total Skills Packaged : {total_copied}")
    print(f" Manifest Generated    : {manifest_path.relative_to(repo_root)}")
    print(f" Assets Location       : {assets_dir.relative_to(repo_root)}")
    print(f" Razor User Location   : {razor_skills_dest}")
    print("=" * 60)

if __name__ == "__main__":
    main()
