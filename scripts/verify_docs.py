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
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None


def extract_nav_paths_from_text(text: str) -> list:
    """Parses .md paths from the nav section of mkdocs.yml using stdlib regex (zero external dependencies)."""
    in_nav = False
    paths = []
    for line in text.splitlines():
        if re.match(r"^nav:\s*$", line):
            in_nav = True
            continue
        if in_nav and re.match(r"^[a-zA-Z0-9_]+:\s*", line):
            break
        if in_nav:
            m = re.search(r":\s*([a-zA-Z0-9_\-\./]+\.md)\s*$", line)
            if not m:
                m = re.search(r"-\s*([a-zA-Z0-9_\-\./]+\.md)\s*$", line)
            if m:
                paths.append(m.group(1).strip())
    return paths


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

    referenced_files = []
    if yaml is not None:
        try:
            class IgnoreUnknownLoader(yaml.SafeLoader):
                pass

            IgnoreUnknownLoader.add_multi_constructor("tag:yaml.org,2002:python/", lambda loader, suffix, node: None)
            IgnoreUnknownLoader.add_multi_constructor("!", lambda loader, suffix, node: None)

            with open(mkdocs_file, "r", encoding="utf-8") as f:
                config = yaml.load(f, Loader=IgnoreUnknownLoader)

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

            nav = config.get("nav", []) if isinstance(config, dict) else []
            referenced_files = collect_nav_paths(nav)
        except Exception:
            referenced_files = []

    if not referenced_files:
        try:
            content = mkdocs_file.read_text(encoding="utf-8")
            referenced_files = extract_nav_paths_from_text(content)
        except Exception as e:
            errors.append(f"Failed to parse mkdocs.yml: {e}")
            return errors

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
        # Replace inline code `...` with placeholder so [`code`](url) links remain matchable
        clean_content = re.sub(r"`[^`]*`", "CODE", clean_content)

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
                    errors.append(f"{md_file.relative_to(docs_dir).as_posix()}: Broken absolute workspace link '{raw_url}'")
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
                    f"{md_file.relative_to(docs_dir).as_posix()}: Broken relative link '{raw_url}' -> file '{target_file}' not found"
                )
                continue

            # Check anchor if target is a markdown file
            if anchor and target_file.suffix == ".md":
                headings = heading_cache.get(target_file, set())
                # Normalize anchor
                clean_anchor = anchor.lower().strip()
                if clean_anchor not in headings:
                    errors.append(
                        f"{md_file.relative_to(docs_dir).as_posix()}: Broken anchor '#{anchor}' in link '{raw_url}'"
                    )

    return total_links, errors


def check_version_synchronization(repo_root: Path) -> list:
    """Verifies that the project version in CMakeLists.txt is strictly synchronized
    across conanfile.py, vcpkg.json, CHANGELOG.md, CLI drivers and docs."""
    errors = []
    cmake_file = repo_root / "CMakeLists.txt"
    if not cmake_file.exists():
        errors.append("CMakeLists.txt not found in repository root")
        return errors

    cmake_content = cmake_file.read_text(encoding="utf-8")
    m = re.search(r"project\s*\(\s*fsmc\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cmake_content)
    if not m:
        errors.append("Could not extract project VERSION from CMakeLists.txt")
        return errors
    current_version = m.group(1)

    # 1. Check conanfile.py
    conan_file = repo_root / "conanfile.py"
    if conan_file.exists():
        conan_content = conan_file.read_text(encoding="utf-8")
        if f'version = "{current_version}"' not in conan_content:
            errors.append(f"conanfile.py version mismatch: expected 'version = \"{current_version}\"'")

    # 2. Check vcpkg.json
    vcpkg_file = repo_root / "vcpkg.json"
    if vcpkg_file.exists():
        vcpkg_content = vcpkg_file.read_text(encoding="utf-8")
        if f'"version-string": "{current_version}"' not in vcpkg_content:
            errors.append(f"vcpkg.json version mismatch: expected '\"version-string\": \"{current_version}\"'")

    # 3. Check CHANGELOG.md
    changelog_file = repo_root / "CHANGELOG.md"
    if changelog_file.exists():
        changelog_content = changelog_file.read_text(encoding="utf-8")
        if f"## [{current_version}]" not in changelog_content:
            errors.append(f"CHANGELOG.md is missing an entry for the current version: '## [{current_version}]'")

    # 4. Check fsmc_driver.hpp and opt_driver.hpp
    fsmc_driver = repo_root / "tools/fsmc/fsmc_driver.hpp"
    if fsmc_driver.exists():
        driver_content = fsmc_driver.read_text(encoding="utf-8")
        if f"version {current_version}" not in driver_content:
            errors.append(f"tools/fsmc/fsmc_driver.hpp version string mismatch: expected 'version {current_version}'")

    opt_driver = repo_root / "tools/fsm-opt/opt_driver.hpp"
    if opt_driver.exists():
        opt_content = opt_driver.read_text(encoding="utf-8")
        if f"v{current_version}" not in opt_content:
            errors.append(f"tools/fsm-opt/opt_driver.hpp version string mismatch: expected 'v{current_version}'")

    # 5. Check docs references (installation.md, integration_guide.md)
    inst_doc = repo_root / "docs/getting_started/installation.md"
    if inst_doc.exists():
        inst_content = inst_doc.read_text(encoding="utf-8")
        if f"fsmc/{current_version}" not in inst_content:
            errors.append(f"docs/getting_started/installation.md stale package version: expected 'fsmc/{current_version}'")

    integ_doc = repo_root / "docs/getting_started/integration_guide.md"
    if integ_doc.exists():
        integ_content = integ_doc.read_text(encoding="utf-8")
        if f"fsmc/{current_version}" not in integ_content:
            errors.append(f"docs/getting_started/integration_guide.md stale package version: expected 'fsmc/{current_version}'")

    return errors


def check_catalog_and_runtime_freshness(repo_root: Path) -> list:
    """Verifies that generated artifacts in docs/ and include/ are in sync with source code."""
    errors = []

    # Check test suite catalog synchronization
    catalog_script = repo_root / "scripts/generate_test_catalog.py"
    if catalog_script.exists():
        ret = subprocess.run([sys.executable, str(catalog_script), "--check"], capture_output=True, text=True)
        if ret.returncode != 0:
            errors.append(
                "docs/reference/test_suite_catalog.md is out of sync with in-code test definitions! "
                "Run: cmake --build build --target generate_test_catalog"
            )

    # Check standalone runtime synchronization
    runtime_script = repo_root / "scripts/generate_standalone_runtime.py"
    if runtime_script.exists():
        ret = subprocess.run([sys.executable, str(runtime_script), "--check"], capture_output=True, text=True)
        if ret.returncode != 0:
            errors.append(
                "Standalone runtime single-headers are out of sync with include/fsm/backend/cpp/runtime/! "
                "Run: cmake --build build --target generate_standalone_runtime"
            )

    return errors


def check_orphaned_docs(repo_root: Path, docs_dir: Path) -> list:
    """Checks that all markdown files in docs/ are referenced in mkdocs.yml."""
    mkdocs_file = repo_root / "mkdocs.yml"
    if not mkdocs_file.exists():
        return []

    try:
        content = mkdocs_file.read_text(encoding="utf-8")
    except Exception:
        return []

    errors = []
    md_files = list(docs_dir.rglob("*.md"))
    for f in md_files:
        rel = f.relative_to(docs_dir).as_posix()
        # Ignore special files or overrides if any
        if rel.startswith("overrides/") or rel.startswith("stylesheets/"):
            continue
        if rel not in content:
            errors.append(f"Orphaned documentation file not included in mkdocs.yml nav: docs/{rel}")

    return errors


def check_content_hygiene(repo_root: Path, docs_dir: Path) -> list:
    """Checks Markdown formatting hygiene and prevents common documentation regressions."""
    errors = []
    gfm_re = re.compile(r"^\s*>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]")
    target_re = re.compile(r"--target\s+(?:rust|c)\b")
    pass_re = re.compile(
        r"\b(?:30|32)\s+(?:built-in\s+|middle-end\s+|optimization\s+|compiler\s+)*(?:passes|pass)\b",
        re.IGNORECASE,
    )

    files = sorted(docs_dir.rglob("*.md"))
    readme = repo_root / "README.md"
    if readme.exists():
        files.append(readme)

    for md_file in files:
        rel = md_file.relative_to(repo_root).as_posix()
        content = md_file.read_text(encoding="utf-8")
        in_fence = False
        fence_char = ""
        fence_len = 0

        for idx, line in enumerate(content.splitlines(), 1):
            fence_match = re.match(r"^\s*(`{3,}|~{3,})(.*)$", line)
            if fence_match:
                marker, rest = fence_match.group(1), fence_match.group(2).strip()
                if not in_fence:
                    in_fence = True
                    fence_char = marker[0]
                    fence_len = len(marker)
                elif marker[0] == fence_char and len(marker) >= fence_len and rest == "":
                    in_fence = False

            if rel.startswith("docs/") and gfm_re.match(line):
                errors.append(f"{rel}:{idx}: Use MkDocs '!!! <type>' admonition instead of GFM alert '{line.strip()}'")
            if rel != "docs/runtime_api/multi_target_roadmap.md" and target_re.search(line):
                errors.append(f"{rel}:{idx}: Unimplemented target flag outside roadmap: '{line.strip()}'")
            if pass_re.search(line):
                errors.append(f"{rel}:{idx}: Stale pass count reference (expected 33 registered / 28 7-stage): '{line.strip()}'")

        if in_fence:
            errors.append(f"{rel}: Unclosed fenced code block detected")

    return errors


def main():
    repo_root = Path(__file__).resolve().parent.parent
    docs_dir = repo_root / "docs"

    if not docs_dir.exists():
        print(f"[ERROR] docs directory not found at {docs_dir}")
        sys.exit(1)

    print("==================================================")
    print(" fsmc Documentation Veracity & Freshness Gate")
    print("==================================================")

    nav_errors = check_mkdocs_nav(repo_root, docs_dir)
    total_links, link_errors = check_markdown_links(docs_dir)
    version_errors = check_version_synchronization(repo_root)
    sync_errors = check_catalog_and_runtime_freshness(repo_root)
    orphan_errors = check_orphaned_docs(repo_root, docs_dir)
    hygiene_errors = check_content_hygiene(repo_root, docs_dir)

    all_errors = nav_errors + link_errors + version_errors + sync_errors + orphan_errors + hygiene_errors

    print("\n----------------- Summary -----------------")
    print(f"Navigation & Hierarchy Check     : {'PASSED' if not nav_errors else 'FAILED'}")
    print(f"Total Internal Links Checked     : {total_links} ({'PASSED' if not link_errors else 'FAILED'})")
    print(f"Version Synchronization Check    : {'PASSED' if not version_errors else 'FAILED'}")
    print(f"Test Catalog & Runtime Sync Gate : {'PASSED' if not sync_errors else 'FAILED'}")
    print(f"Orphaned Documentation Check     : {'PASSED' if not orphan_errors else 'FAILED'}")
    print(f"Content & Hygiene Regression Gate: {'PASSED' if not hygiene_errors else 'FAILED'}")
    print(f"Total Quality Gate Issues Found  : {len(all_errors)}")
    print("-------------------------------------------")

    if all_errors:
        print("\n[FAIL] Documentation quality gate issues detected:")
        for err in all_errors:
            print(f"  * {err}")
        sys.exit(1)
    else:
        print("\n[SUCCESS] Documentation is 100% up-to-date, valid, and synchronized!")
        sys.exit(0)


if __name__ == "__main__":
    main()
