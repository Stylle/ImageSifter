# Changelog

## 2.0.0

First public release of the rebuilt native desktop workbench.

- Qt6/CMake architecture with independent SQLite projects and source folders.
- Lightroom-inspired grid, loupe, pinned-reference comparison and filmstrip.
- Persistent ratings, pick/reject flags, color labels, filters and recent projects.
- Asynchronous, bounded thumbnail/preview loading, stale-result protection, GIF/WebP animation lifecycle and neutral preview rendering.
- LibRaw0.22.3 for multi-brand RAW identification and embedded/half-size previews.
- JPEG/RAW separation with persistent pairing; dedicated format-cleanup actions.
- Categorized discard folders, paired removal, in-app browsing/restoration, non-overwriting recovery and a durable file journal.
- Verified-copy transport, interruption review, project record backup and database migration safeguards.
- Original SVG/ICO branding, compact-window side panels and keyboard-first selection.
- Windows x64 portable and per-user installer releases with third-party notices/source archives. No Docker.

Limitations: no full RAW development module, no automatic time-based matching of renamed files, no HEIC/AVIF decoder, and no one-click complete project/archive migration. Back up both project records and original photos.
