#!/usr/bin/env python3
"""
Automated Test Suite Catalog Generator for fsmc.

Extracts in-code `@brief Test Intent:` and `Scenario:` Doxygen comments from all
Google Test source files and generates `docs/TEST_SUITE_CATALOG.md`.

Usage:
    python3 scripts/generate_test_catalog.py [--output docs/TEST_SUITE_CATALOG.md] [--check]
"""

import argparse
import os
import re
import sys
from pathlib import Path

# Subsystem directory mappings
SUBSYSTEMS = [
    ("Core Runtime Subsystem", "tests/backend/cpp/runtime"),
    ("C++ Backend Codegen Subsystem", "tests/backend/cpp"),
    ("Diagram & Emitter Backend Subsystem", "tests/backend/diagram"),
    ("Formal Model Checking & nuXmv Subsystem", "tests/backend/formal"),
    ("Requirements Traceability (RTM) Subsystem", "tests/backend/rtm"),
    ("Diagnostic Engine Subsystem", "tests/diagnostic"),
    ("Frontend Parser Subsystem", "tests/frontend"),
    ("Formal IR Subsystem", "tests/ir"),
    ("Middle-End Verification & Transformation Subsystem", "tests/middleend"),
    ("Integration & Build Subsystem", "tests/integration"),
    ("System Examples & Workflows", "examples"),
]

# Regex patterns for extracting test documentation
DOC_BLOCK_PATTERN = re.compile(
    r"/\*\*(.*?)\*/\s*(?:TEST(?:_F)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)|int\s+main\s*\()",
    re.DOTALL,
)


def extract_test_info(content: str):
    tests = []
    for match in DOC_BLOCK_PATTERN.finditer(content):
        raw_doc, suite_name, test_name = match.groups()
        lines = [line.strip().lstrip("*").strip() for line in raw_doc.split("\n")]
        doc_text = "\n".join(lines).strip()

        # Extract Brief Intent
        brief_match = re.search(r"@brief\s+Test\s+Intent:\s*(.*?)(?=\n\n|\n[A-Z@]|Scenario:|$)", doc_text, re.DOTALL | re.IGNORECASE)
        brief = brief_match.group(1).strip() if brief_match else ""

        # Extract Scenario
        scenario_match = re.search(r"Scenario:\s*(.*?)(?=\n\n|\n[A-Z@]|$)", doc_text, re.DOTALL | re.IGNORECASE)
        scenario = scenario_match.group(1).strip() if scenario_match else ""

        display_name = f"{suite_name}.{test_name}" if suite_name and test_name else "Main Test Suite"
        tests.append({
            "name": display_name,
            "brief": brief,
            "scenario": scenario,
        })
    return tests


def generate_catalog(root_dir: Path) -> str:
    lines = []
    lines.append("# Master Test Suite & Behavioral Verification Catalog")
    lines.append("")
    lines.append("> **Note**: This catalog is automatically generated from the in-code `@brief Test Intent` comments across `tests/`.")
    lines.append("> To update this file, run: `cmake --build build --target generate_test_catalog` or `python3 scripts/generate_test_catalog.py`.")
    lines.append("")

    total_files = 0
    total_tests = 0
    all_data = []
    seen_files = set()

    for subsystem_name, subpath in SUBSYSTEMS:
        full_dir = root_dir / subpath
        if not full_dir.exists():
            continue

        files_data = []
        for file_path in sorted(full_dir.glob("**/*.cpp")):
            if file_path in seen_files:
                continue
            seen_files.add(file_path)

            content = file_path.read_text(encoding="utf-8")
            tests = extract_test_info(content)
            if tests or file_path.name.startswith("test_") or "example" in file_path.name:
                rel_path = file_path.relative_to(root_dir)
                files_data.append((rel_path, tests))
                total_files += 1
                total_tests += len(tests)

        if files_data:
            all_data.append((subsystem_name, files_data))

    lines.append(f"**Total Documented Subsystems**: {len(all_data)}  ")
    lines.append(f"**Total Test Suites & Binaries**: {total_files}  ")
    lines.append(f"**Total Documented Test Cases**: {total_tests}  ")
    lines.append("")
    lines.append("---")
    lines.append("")

    for subsystem_name, files in all_data:
        lines.append(f"## {subsystem_name}")
        lines.append("")
        for rel_path, tests in files:
            lines.append(f"### [`{rel_path.name}`](../{rel_path}) (`{rel_path}`)")
            if not tests:
                lines.append("- *(Executable binary test verification)*")
                lines.append("")
                continue

            for t in tests:
                lines.append(f"#### `{t['name']}`")
                if t["brief"]:
                    lines.append(f"**Test Intent**: {t['brief']}")
                if t["scenario"]:
                    lines.append("")
                    lines.append("**Scenario**:")
                    for s_line in t["scenario"].split("\n"):
                        s_line = s_line.strip()
                        if s_line:
                            if not s_line.startswith("-"):
                                s_line = f"- {s_line}"
                            lines.append(f"  {s_line}")
                lines.append("")
        lines.append("---")
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate test_suite_catalog.md from in-code test comments.")
    parser.add_argument(
        "--output",
        default="docs/reference/test_suite_catalog.md",
        help="Output path for the generated catalog.",
    )
    parser.add_argument("--check", action="store_true", help="Check if the catalog is up-to-date without writing.")

    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent
    out_file = root_dir / args.output

    generated_content = generate_catalog(root_dir)

    if args.check:
        if not out_file.exists():
            print(f"Error: {out_file} does not exist. Run generator to create it.", file=sys.stderr)
            sys.exit(1)
        existing_content = out_file.read_text(encoding="utf-8")
        if existing_content.strip() != generated_content.strip():
            print("Error: TEST_SUITE_CATALOG.md is out of date with in-code test comments!", file=sys.stderr)
            print("Run 'python3 scripts/generate_test_catalog.py' to synchronize.", file=sys.stderr)
            sys.exit(1)
        print("Catalog is up-to-date with all in-code test documentation.")
        sys.exit(0)

    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(generated_content, encoding="utf-8")
    print(f"Successfully generated {out_file} from in-code test comments.")


if __name__ == "__main__":
    main()
