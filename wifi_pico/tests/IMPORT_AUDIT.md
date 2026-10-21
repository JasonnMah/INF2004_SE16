# Audit of the previous 599-file template import

IMPORT_AUDIT.csv has exactly one row per file in previous_template_import.json,
the original 599-file manifest. It records the old hash, disposition against the
pinned course port, purpose, and measured use in the milestone 1 build.

Not every imported file is required by this boot test. The complete tracked
upstream snapshot is retained to preserve its Make dependencies, licence mapping
and reference material. Non-RP2040 ports, disabled drivers, upstream applications
and optional networking are explicitly identified as reference/optional material.
They are not claimed as runtime requirements. No old-template source is overlaid
onto the pinned port.

The audit distinguishes compiler dependencies (measured from generated .d files),
parsed Make recipes, linker/UF2 inputs, optional/reference material, and removed
local-template-only paths. A compiler dependency does not mean a disabled
translation unit contributes functions to the ELF.

course/ORIGIN.json records upstream and vendored hashes for the entire snapshot;
patches/milestone1-board-config.patch records the only three altered course files.

Desktop layout: upstream build_make recipes are stored under build_rules.
ORIGIN.json maps stored_path to logical source paths. Wrappers recreate ignored
build_make. portable_sha256 normalizes text CRLF to LF; original byte hashes and
all 599 old-import audit rows are retained.
