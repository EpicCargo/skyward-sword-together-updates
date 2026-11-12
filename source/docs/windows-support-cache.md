# Restore a Windows support cache offline

The pinned SkyRecomp 2.0.2 Windows support download returned HTTP 404. If you
already have a matching SST dependency-cache backup and its separately preserved
SHA256 receipt, import it from this checkout's `source` directory:

```sh
python3 scripts/import-windows-support.py \
  --archive /path/to/verified-windows-v202-support-cache.zip \
  --sha256 HASH_FROM_THE_BACKUPS_SEPARATE_RECEIPT
```

Use the `archive_sha256` from the backup's `RESTORATION.json`, or the independently
preserved checksum from whoever supplied your trusted backup. A checksum embedded
only inside an otherwise untrusted ZIP does not establish its provenance.

The importer checks the outer ZIP hash, every member's inner SHA256, the selected
Windows release recipe, the complete support manifest and its CRC/SHA256 checks.
It rejects duplicate names, symlinks, traversal paths, unsupported members and
oversized archives. It imports only the allowlisted support files, manifests and
all required third-party notices. Archived generated import libraries and source
configuration are skipped. No executable is started and no HTTP request is made.

Existing identical files are reused. Conflicting files cause an error before
publication; keep them and use a fresh matching source checkout. Publication
cannot overwrite a file delivered concurrently. An interruption during publication
can leave verified files in the cache; rerun the same import to finish. The complete
support manifest is published last.

After a successful import, the existing command can reuse the complete cache and
notices without contacting the unavailable release:

```sh
python3 scripts/fetch-windows-support.py
```

The real preserved backup passed import and reuse with HTTP disabled: 341 archived
file hashes, 85 support files and 100 imported files including manifests/notices.
Twenty-one synthetic regressions also pass normally and with `python3 -O`.
See [the scalar validation record](../validation/windows-support-import-20261111.json).

This recovers dependencies from an existing backup. It does not provide a new
download source, cross compiler, system headers, game files or a complete Windows
build. Full builds retain their existing resource guards.
