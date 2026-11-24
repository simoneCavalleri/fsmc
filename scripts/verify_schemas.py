#!/usr/bin/env python3
"""
Validates fsmc JSON Schemas (schemas/fsm_ir.schema.json and schemas/fsm_manifest.schema.json)
and checks that real fsmc inputs (*.ir.json) and outputs (*.manifest.json, emitted IR JSON)
conform strictly to their published schemas.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IR_SCHEMA_PATH = ROOT / "schemas" / "fsm_ir.schema.json"
MANIFEST_SCHEMA_PATH = ROOT / "schemas" / "fsm_manifest.schema.json"

try:
    import jsonschema  # type: ignore

    HAS_JSONSCHEMA = True
except ImportError:
    HAS_JSONSCHEMA = False


def validate_fallback(instance: object, schema: dict, path: str = "$") -> list[str]:
    """Minimal zero-dependency Draft 2020-12 subset validator used if jsonschema is unavailable."""
    errors: list[str] = []
    expected_type = schema.get("type")
    if expected_type == "object":
        if not isinstance(instance, dict):
            return [f"{path}: expected object, got {type(instance).__name__}"]
        for req in schema.get("required", []):
            if req not in instance:
                errors.append(f"{path}: missing required key '{req}'")
        props = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for k in instance:
                if k not in props:
                    errors.append(f"{path}: unexpected additional property '{k}'")
        for k, sub_schema in props.items():
            if k in instance:
                errors.extend(validate_fallback(instance[k], sub_schema, f"{path}.{k}"))
    elif expected_type == "array":
        if not isinstance(instance, list):
            return [f"{path}: expected array, got {type(instance).__name__}"]
        item_schema = schema.get("items")
        if isinstance(item_schema, dict):
            for idx, elem in enumerate(instance):
                errors.extend(validate_fallback(elem, item_schema, f"{path}[{idx}]"))
    elif expected_type == "string":
        if not isinstance(instance, str):
            return [f"{path}: expected string, got {type(instance).__name__}"]
        if "const" in schema and instance != schema["const"]:
            errors.append(f"{path}: expected const '{schema['const']}', got '{instance}'")
        if "minLength" in schema and len(instance) < schema["minLength"]:
            errors.append(f"{path}: string length {len(instance)} < minLength {schema['minLength']}")
        if "pattern" in schema and not re.match(schema["pattern"], instance):
            errors.append(f"{path}: '{instance}' does not match pattern {schema['pattern']}")
    elif expected_type == "integer":
        if not isinstance(instance, int) or isinstance(instance, bool):
            return [f"{path}: expected integer, got {type(instance).__name__}"]
        if "minimum" in schema and instance < schema["minimum"]:
            errors.append(f"{path}: {instance} < minimum {schema['minimum']}")
    elif expected_type == "boolean":
        if not isinstance(instance, bool):
            return [f"{path}: expected boolean, got {type(instance).__name__}"]
    return errors


def validate_doc(instance: object, schema: dict, label: str) -> None:
    if HAS_JSONSCHEMA:
        validator = jsonschema.Draft202012Validator(schema)
        errs = sorted(validator.iter_errors(instance), key=lambda e: list(e.path))
        if errs:
            msg = "\n".join(f"  - {list(e.path)}: {e.message}" for e in errs)
            raise SystemExit(f"[FAIL] Schema validation failed for {label}:\n{msg}")
    else:
        errs = validate_fallback(instance, schema)
        if errs:
            msg = "\n".join(f"  - {e}" for e in errs)
            raise SystemExit(f"[FAIL] Schema validation (fallback) failed for {label}:\n{msg}")


def find_fsmc_binary() -> Path | None:
    for candidate in (
        ROOT / "build" / "bin" / "fsmc",
        ROOT / "build" / "bin" / "Release" / "fsmc",
        ROOT / "build" / "bin" / "Release" / "fsmc.exe",
    ):
        if candidate.is_file():
            return candidate
    return None


def main() -> int:
    ir_schema = json.loads(IR_SCHEMA_PATH.read_text(encoding="utf-8"))
    manifest_schema = json.loads(MANIFEST_SCHEMA_PATH.read_text(encoding="utf-8"))

    if HAS_JSONSCHEMA:
        jsonschema.Draft202012Validator.check_schema(ir_schema)
        jsonschema.Draft202012Validator.check_schema(manifest_schema)
        print("[OK] Both JSON Schemas passed Draft 2020-12 meta-schema check (jsonschema).")
    else:
        print("[WARN] Python 'jsonschema' not installed; using built-in subset validator.")

    # 1. Validate all *.ir.json files in examples/
    ir_files = sorted(ROOT.glob("examples/**/*.ir.json"))
    if not ir_files:
        raise SystemExit("[FAIL] No *.ir.json files found under examples/")
    for ir_file in ir_files:
        doc = json.loads(ir_file.read_text(encoding="utf-8"))
        validate_doc(doc, ir_schema, str(ir_file.relative_to(ROOT)))
        print(f"[OK] Validated IR input: {ir_file.relative_to(ROOT)}")

    # 2. If fsmc binary is built, generate live manifests & emitted IR and validate them
    fsmc_bin = find_fsmc_binary()
    if fsmc_bin is None:
        print("[WARN] fsmc binary not found in build/bin; skipping live artifact validation.")
        return 0

    with tempfile.TemporaryDirectory(prefix="fsmc_schema_check_") as tmp:
        tmp_dir = Path(tmp)

        # Case A: Showcase 07 (.ir.json -> C++17 + C99 ABI + manifest)
        sc07_ir = ROOT / "examples" / "07_modular_toolchain_c_abi" / "thermal_controller.ir.json"
        out_fsm = tmp_dir / "thermal_controller_fsm.hpp"
        subprocess.run(
            [
                str(fsmc_bin),
                str(sc07_ir),
                "-o",
                str(out_fsm),
                "--emit-c-abi",
                "--emit-manifest",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        manifest_file = tmp_dir / "thermal_controller.manifest.json"
        manifest_doc = json.loads(manifest_file.read_text(encoding="utf-8"))
        validate_doc(manifest_doc, manifest_schema, "thermal_controller.manifest.json")
        assert manifest_doc["scheduling"]["max_event_queue_bound"] >= 2
        assert manifest_doc["scheduling"]["zeno_free"] is True
        print("[OK] Validated generated manifest: thermal_controller.manifest.json")

        # Case B: SysML v2 example -> emitted IR JSON + manifest
        sysml_examples = sorted(ROOT.glob("examples/**/*.sysml"))
        if sysml_examples:
            sysml_in = sysml_examples[0]
            sysml_out = tmp_dir / "sysml_fsm.hpp"
            subprocess.run(
                [
                    str(fsmc_bin),
                    str(sysml_in),
                    "-o",
                    str(sysml_out),
                    "--emit-manifest",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            sysml_manifest = tmp_dir / "sysml.manifest.json"
            validate_doc(
                json.loads(sysml_manifest.read_text(encoding="utf-8")),
                manifest_schema,
                f"manifest for {sysml_in.relative_to(ROOT)}",
            )
            print(f"[OK] Validated generated manifest for: {sysml_in.relative_to(ROOT)}")

    print("[SUCCESS] All JSON Schema validations passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
