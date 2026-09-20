# Security Policy

## Supported code

Security fixes are evaluated against the current public `main` branch and the current release line. Older releases may not receive separate fixes.

The public repository covers the Such frontend, CLI, public build tooling, and stable runtime client boundary. The production runtime and Such SDK are maintained separately by Heritage Inc.

## Reporting a vulnerability

Please do not publish exploit details, private runtime information, credentials, or proof-of-concept material in a public GitHub issue before coordinated review.

Report security concerns privately to Heritage Inc. through the contact information at:

https://heritage-labs.net

A useful report should include:

- affected Such version and platform
- reproduction steps
- expected and observed behavior
- impact assessment
- relevant logs or crash information with secrets removed
- proof of concept, if safe to share privately

Heritage Inc. will review the report and determine whether the issue belongs to the public source tree, the private runtime, the private SDK, or another component.

## Scope

Examples of security-relevant issues include unsafe path handling, unintended file disclosure, privilege-boundary mistakes, code execution, insecure update or packaging behavior, runtime loading vulnerabilities, and leakage across the public/private component boundary.

General feature requests, performance issues, and non-security bugs should use the normal public issue or contribution workflow.
