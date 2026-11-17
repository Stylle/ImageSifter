# Development contracts

- Local native C++/Qt app. Never introduce Docker, cloud dependencies or photo uploads.
- Read README and ARCHITECTURE before changes. Qt/CMake is authoritative; the former qmake prototype is backed up outside this repository.
- Project catalog, locks, marks, pairing and file journal are persistent contracts. Do not reset catalogs to simplify changes. Schema upgrades must be transactional and tested.
- A selected photo and its paired JPEG/RAW are normally moved together. Format cleanup deliberately moves only that format. Preserve pairing after organization.
- Discard uses a dedicated, categorized folder, never the OS recycle bin or permanent deletion. Successful transport and journal completion must precede UI success. Cross-volume moves verify content; failures retain data and actionable records. Restore never overwrites an existing file.
- Original photos, test screenshots with local paths, catalogs, caches, logs, IDE files and credentials are not repository assets. Use synthetic fixtures; do not operate on real photos during automated tests.
- Background work must not capture unguarded view lifetimes. Bound decoding tasks/cache; allocate LibRaw objects on the heap because worker thread stacks are small. Close waits for safe task completion.
- Keep raster preview neutral, color/alpha/orientation intact. RAW preview may be embedded JPEG or half-size rendering; never claim full development/ICC support without implementation and validation.
- Build, meaningful core/UI tests, native screenshot review and clean-machine-style deployed-plugin tests are required before publishing. Keep release artifacts versioned and never replace another project's repository/assets.
- Source is MIT. Preserve vendor licenses; distribute Qt/LibRaw dynamically with notices, corresponding library sources and replacement/rebuild instructions. Do not remove upstream notices from vendor sources.
