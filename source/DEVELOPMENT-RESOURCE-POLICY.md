# Developer resource policy

- Build with one compiler, nice level 10, a 4096 MiB owned-group RSS cap and at least 6144 MiB free memory. On WSL, check both Windows and Linux. Use the shared developer-build lock.
- Small CPU fixtures use a 512 MiB owned-group cap and a 3072 MiB free reserve on both OSes.
- Run at most one full SkyRecomp game across both OSes; lightweight peers are permitted. Native tests retain a 3072 MiB free reserve.
- Keep immutable candidates and fresh test profiles. Preserve personal game files, saves, settings, prior releases and dirty source. Stop only owned test/build processes when guards fail.
- Boss/miniboss synchronization stays off. Preserve collector-only consumable policy and existing native lifetime/held-context restrictions.
- Publishing source is separate from publishing a binary release or changing the update feed.
