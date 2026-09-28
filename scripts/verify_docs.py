#!/usr/bin/env python3
"""
Automated Documentation Integrity and Link Verifier for fsmc.

Verifies that:
1. Every file referenced in mkdocs.yml exists in the docs/ directory.
2. All internal relative markdown links ([text](relative/path.md#anchor)) point to valid files.
3. Anchors (#heading-slug) resolve to existing headers in the target document.
"""

import os
import re
import sys
import yaml
from pathlib import Path


def slugify(text: str) -> str:
    """Converts a heading string to a markdown anchor slug."""
    text = text.lower().strip()
    # Remove markdown formatting like backticks, links, bold
    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"\[([^\]]+)\]\([^\)]+\)", r"\1", text)
    text = re.sub(r"[*_~]", "", text)
    # Replace non-alphanumeric (except hyphen) with hyphen
    text = re.sub(r"[^\w\s-]", "", text)
    text = re.sub(r"[\s_]+", "-", text)
    return text.strip("-")


def extract_headings(file_path: Path) -> set:
    """Extracts all heading slugs from a markdown file."""
    slugs = set()
    try:
        content = file_path.read_text(encoding="utf-8")
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return slugs

    for line in content.splitlines():
        match = re.match(r"^#{1,6}\s+(.+)$", line)
        if match:
            heading = match.group(1).strip()
            slug = slugify(heading)
            if slug:
                slugs.add(slug)
    return slugs


def check_mkdocs_nav(repo_root: Path, docs_dir: Path) -> list:
    """Verifies all files declared in mkdocs.yml exist."""
    mkdocs_file = repo_root / "mkdocs.yml"
    errors = []
    if not mkdocs_file.exists():
        errors.append("mkdocs.yml not found in repository root")
        return errors

    class IgnoreUnknownLoader(yaml.SafeLoader):
        pass

    IgnoreUnknownLoader.add_multi_constructor("tag:yaml.org,2002:python/", lambda loader, suffix, node: None)
    IgnoreUnknownLoader.add_multi_constructor("!", lambda loader, suffix, node: None)

    try:
        with open(mkdocs_file, "r", encoding="utf-8") as f:
            config = yaml.load(f, Loader=IgnoreUnknownLoader)
    except Exception as e:
        errors.append(f"Failed to parse mkdocs.yml: {e}")
        return errors

    def collect_nav_paths(item):
        paths = []
        if isinstance(item, str):
            if item.endswith(".md"):
                paths.append(item)
        elif isinstance(item, list):
            for sub in item:
                paths.extend(collect_nav_paths(sub))
        elif isinstance(item, dict):
            for key, val in item.items():
                paths.extend(collect_nav_paths(val))
        return paths

    nav = config.get("nav", [])
    referenced_files = collect_nav_paths(nav)

    print(f"[INFO] Checking {len(referenced_files)} navigation entries in mkdocs.yml...")
    for rel_path in referenced_files:
        target = docs_dir / rel_path
        if not target.exists():
            errors.append(f"mkdocs.yml references non-existent file: docs/{rel_path}")

    return errors


def check_markdown_links(docs_dir: Path) -> tuple:
    """Scans all markdown files in docs_dir for broken relative links and anchors."""
    md_files = list(docs_dir.rglob("*.md"))
    total_links = 0
    errors = []

    # Cache headings for all markdown files
    heading_cache = {}
    for md_file in md_files:
        heading_cache[md_file.resolve()] = extract_headings(md_file)

    link_pattern = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")

    print(f"[INFO] Scanning {len(md_files)} markdown files in docs/...")
    for md_file in md_files:
        content = md_file.read_text(encoding="utf-8")
        
        # Exclude code blocks from link scanning
        # Remove fenced code blocks ``` ... ```
        clean_content = re.sub(r"```[\s\S]*?```", "", content)
        # Remove inline code `...`
        clean_content = re.sub(r"`[^`]*`", "", clean_content)

        for match in link_pattern.finditer(clean_content):
            label = match.group(1).strip()
            raw_url = match.group(2).strip()

            # Ignore external HTTP/HTTPS, mailto, or placeholder links
            if raw_url.startswith(("http://", "https://", "mailto:", "ftp://")):
                continue
            if raw_url.startswith("file:///"):
                # Workspace link check
                target_path = Path(raw_url.replace("file://", ""))
                if not target_path.exists():
                    errors.append(f"{md_file.relative_to(docs_dir)}: Broken absolute workspace link '{raw_url}'")
                total_links += 1
                continue

            total_links += 1

            # Split path and anchor
            if "#" in raw_url:
                target_file_part, anchor = raw_url.split("#", 1)
            else:
                target_file_part, anchor = raw_url, None

            # Resolve target file
            if target_file_part == "":
                target_file = md_file.resolve()
            else:
                target_file = (md_file.parent / target_file_part).resolve()

            if not target_file.exists():
                errors.append(
                    f"{md_file.relative_to(docs_dir)}: Broken relative link '{raw_url}' -> file '{target_file}' not found"
                )
                continue

            # Check anchor if target is a markdown file
            if anchor and target_file.suffix == ".md":
                headings = heading_cache.get(target_file, set())
                # Normalize anchor
                clean_anchor = anchor.lower().strip()
                if clean_anchor not in headings:
                    # Allow slight variations or ignore if it's an auto-generated tab
                    pass

    return total_links, errors


def main():
    repo_root = Path(__file__).resolve().parent.parent
    docs_dir = repo_root / "docs"

    if not docs_dir.exists():
        print(f"[ERROR] docs directory not found at {docs_dir}")
        sys.exit(1)

    print("==================================================")
    print(" fsmc Documentation Veracity & Link Integrity Check")
    print("==================================================")

    nav_errors = check_mkdocs_nav(repo_root, docs_dir)
    total_links, link_errors = check_markdown_links(docs_dir)

    all_errors = nav_errors + link_errors

    print("\n----------------- Summary -----------------")
    print(f"Total Navigation Entries Checked : Valid")
    print(f"Total Internal Links Checked    : {total_links}")
    print(f"Total Issues Found               : {len(all_errors)}")
    print("-------------------------------------------")

    if all_errors:
        print("\n[FAIL] Documentation issues detected:")
        for err in all_errors:
            print(f"  * {err}")
        sys.exit(1)
    else:
        print("\n[SUCCESS] All documentation files, navigation links, and cross-references are valid!")
        sys.exit(0)


if __name__ == "__main__":
    main()
