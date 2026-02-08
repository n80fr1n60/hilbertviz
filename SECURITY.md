# Security Policy

## Reporting a Vulnerability

We take the security of `hilbertviz` seriously. If you discover a security vulnerability, please do not open a public issue. Instead, please report it through one of the following channels:

*   **Email**: we do not yet have an email for this

## Defensive Posture

`hilbertviz` is built with a "security-first" approach for C11 development. Our defensive measures include:

### 1. Memory and Integer Safety
*   **Bounded Arithmetic**: All calculations for buffer sizes, image dimensions, and offsets use overflow-checked helpers (`hv_mul_u64`, `hv_add_u64`).
*   **Memory Caps**: A default memory allocation cap (256MB) prevents Denial-of-Service via large images. This is configurable via `HILBERTVIZ_MAX_IMAGE_BYTES`.
*   **Safe I/O**: We avoid unsafe C functions (e.g., `gets`, `sprintf`). We use `snprintf` and `vsnprintf` exclusively.

### 2. Path and File System Security
*   **Destructive Alias Prevention**: The tool uses `fstat` and inode comparison (`st_dev`, `st_ino`) to detect if an output path points to the same physical file as the input, preventing data loss.
*   **Race Condition Mitigation**: Validation of file properties is performed on open file descriptors to mitigate Time-of-Check Time-of-Use (TOCTOU) vulnerabilities.
*   **Path Normalization**: `realpath` is used to normalize paths before comparison to prevent directory traversal bypasses.

### 3. Build-Time Protections
*   **Hardening Flags**: Release builds are compiled with `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, and `-fPIE` (Position Independent Executable) to mitigate exploitation of memory bugs.
*   **Strict Warnings**: We use `-Wall -Wextra -Wconversion -Wformat=2` and treat warnings as errors in CI.

### 4. Continuous Testing
*   **Fuzzing**: The repository includes integration for AFL++ and libFuzzer.
*   **Sanitizers**: AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) are used during development and testing.
