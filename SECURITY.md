# Security Policy

## 1. Supported Versions

Security updates and fixes are provided for the latest minor release on the `main` branch:

| Version | Supported          |
| ------- | ------------------ |
| 0.4.x   | :white_check_mark: |
| < 0.4.0 | :x:                |

---

## 2. Reporting a Vulnerability

We take the security and integrity of `fsmc` seriously. If you discover a security vulnerability, potential buffer overflow, undefined behavior, or denial of service in the compiler or runtime library, please report it through coordinated responsible disclosure.

### How to Report

- **Preferred Method**: Open a private **GitHub Security Advisory** under the `Security` tab of the repository.
- **Alternative Contact**: Email the maintainer directly with the subject line `[SECURITY] fsmc Vulnerability Report`.

Please include:
1. A detailed description of the vulnerability.
2. Steps to reproduce or a Minimal Reproducible Example (e.g. malformed model file or input payload).
3. The affected component (`fsmc` CLI, frontend parser, middle-end pass, or runtime library).
4. Potential impact and proposed mitigations, if known.

### Response Timeline

- **Acknowledgment**: Within 48 hours of initial receipt.
- **Assessment**: We will evaluate the report and coordinate an advisory and patch release.
- **Public Disclosure**: Coordinated release after an official patch has been published.

---

## 3. Scope & Non-Liability

The security reporting process is provided in good faith. The maintainer(s) provide no warranty or service-level agreement (SLA) regarding vulnerability remediation timelines, and assume no liability for security issues arising from the use of this software, as specified in `DISCLAIMER.md` and `LICENSE`.
