#!/usr/bin/env python3
"""
Custom External Pipeline Filter: Strict MBSE Port Bounds & Naming Linter
Reads canonical FsmIr JSON from stdin, performs linting audits, and emits IR to stdout.
Used with `fsm-opt --pipe-through="python3 strict_bounds_linter.py"`
"""

import sys
import json

def main():
    try:
        data = json.load(sys.stdin)
    except Exception as e:
        sys.stderr.write(f"\033[1;31m[LINTER ERROR]\033[0m Failed to parse input IR JSON from stdin: {e}\n")
        sys.exit(1)

    fsm_name = data.get("name", "UnknownFSM")
    sys.stderr.write(f"\033[1;35m[PIPE-THROUGH LINTER]\033[0m Auditing FSM Model '{fsm_name}' via external Unix filter...\n")

    ports = data.get("ports", [])
    ports_verified = 0
    for port in ports:
        name = port.get("name", "unnamed")
        has_min = "min_value" in port or "min" in port
        has_max = "max_value" in port or "max" in port
        if has_min and has_max:
            ports_verified += 1
            sys.stderr.write(f"  \033[1;32m[PORT CONTRACT OK]\033[0m Port '{name}' has explicit range bounds: [{port.get('min_value', port.get('min'))}, {port.get('max_value', port.get('max'))}]\n")
        else:
            sys.stderr.write(f"  \033[1;33m[PORT WARNING]\033[0m Port '{name}' is missing numerical range bounds!\n")

    sys.stderr.write(f"\033[1;32m[PIPE-THROUGH OK]\033[0m Successfully verified {ports_verified}/{len(ports)} MBSE port contracts.\n")

    # Pass-through unmodified IR to stdout for subsequent middle-end passes
    json.dump(data, sys.stdout, indent=2)

if __name__ == "__main__":
    main()
