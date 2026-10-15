# USB Loader GX memory and cache map

Where everything in RAM lives, who owns it, and when fixed addresses are written.
Figures marked *measured* come from Dolphin on 2026-10-14 (build 0d646f9a, the
86-click all-buttons run). Everything else comes from the source. Re-measure
before relying on a number: see "Measuring" at the end.

Wii RAM is MEM1 (24 MiB at 0x80000000) and MEM2 (64 MiB at 0x90000000). The
uncached views are 0xC0000000 and 0xD0000000. For bandwidth and CPU cache
measurements see `@Shared/wiixplorer-wii-hardware-research-*/MEMORY.md`.

## MEM1

| Range | What | Notes |
| --- | --- | --- |
| 0x80000000-0x80003F00 | Low memory: OS globals, exception vectors (0x80003000) | Fixed writes listed below |
| 0x80001800 | HBC return stub (`lstub.cpp`); a game's Ocarina code handler goes here instead | Written at boot only |
| 0x80002F20 | hbc agent netlog block (`HBC_NETLOG_ADDR`) | Written by HBC and the agent |
| 0x80003F00-0x80B00000 | About 11 MB the game, apploader or booted app loads into | **Idle while the GUI runs.** Nothing of GX's lives here |
| 0x80B00000-~0x8111C600 | GX's image: `.text` 3.3 MB, `.rodata` 1.9 MB, `.eh_frame` 0.4 MB, `.data` 72 KB, `.bss` 0.7 MB | Linked with `--section-start,.init=0x80B00000` (Makefile). `.bss` holds the 128 KB main stack (`s_mainStack`) and the network pbuf pool (~200 KB) |
| BSS end-0x81700000 | newlib heap (sbrk from `SYS_GetArena1Lo()`) | *measured*: Arena1Lo 0x81335000 at start, 0x814C0000 at peak, heap ~3.7 MB |
| 0x81700000-0x81800000 | Kept free | The apploader writes the game's FST at the top of MEM1 while GX's heap is still in use; `MAX_MEM1_ARENA_LO` in `mem2.cpp` keeps the heap below 0x81700000 |
| 0x817FE000 | BI2 (channel boot writes and clears it) | Boot only |

Arena1Hi is 0x81800000 (`__Arena1Hi`). The first heap blocks are the two XFBs
(`SYS_AllocateFramebuffer`, 614,400 B each at 640x480, 734,720 B at 640x574) and the 768 KB GX FIFO
(`GP_FIFO_SIZE` in `video.cpp`). They are allocated in `InitVideo()` before
`MEM2_init()`, so they are always in MEM1, as VI needs.

## MEM2

| Range | What | Notes |
| --- | --- | --- |
| 0x90000000-0x90200000 | IOS reload area | Never used by GX (`IOS_RELOAD_AREA`) |
| 0x90200000-0x91800000 | GX heap, lower pool (22 MB) | Tried first |
| 0x91800000-0x91801140 | hbc agent records: netlog copy, crash block, last log | Left out of the heap. Only the 32-byte netlog copy is written while GX runs (by `hbc.py log`); see `@Shared/handoff-hbc-agent-mem2-records.md` |
| 0x91801140-heap end | GX heap, upper pool (27.1 MB) | Keeps the large free block |
| heap end-Arena2Hi | 768 KB left for libogc (`LIBOGC_ARENA2_RESERVE`) | *measured* use: 233 KB, i.e. Bluetooth buffers 85 KB per `WPAD_Init()` (twice at start-up) and the 64 KB net heap |
| 0x933E0000-0x93400000 | libogc IPC buffer | |
| 0x93400000-0x94000000 | IOS | GX only reads it (`sys.cpp` 0x938B2xxx, `neek.cpp` 0x939F0000) |

Heap end = `SYS_GetArena2Hi()` (0x933E0000) − 768 KB = 0x93320000. *measured*:
51,506,880 bytes in the heap; 48.8 MB free after the all-buttons run.

### Fixed MEM2 addresses inside the heap

These are written only after the heap is dead, at the end of `RunAppbooter()`,
after `SYS_ResetSystem()` with interrupts off. Never write them earlier.

| Address | What |
| --- | --- |
| 0x92000000 (`EXECUTE_ADDR`) | Homebrew image, up to 16 MB, moved from its heap block |
| 0x93000000 (`BOOTER_ADDR`) | App booter (`app_booter_bin`) |
| 0x93200000 (`ARGS_ADDR`) | `struct __argv` and command line, at most 1 MB |

## How allocation routes

The `malloc`, `calloc`, `memalign`, `realloc` and `free` wrappers are in
`source/memory/mem2.cpp`; the Makefile links with `-wrap`.

- Up to 30 KB: the MEM1 heap, then MEM2 if MEM1 fails.
- Over 30 KB (`MEM2_PRIORITY_SIZE`), or when the request would push the MEM1
  heap past 0x81700000: MEM2 first, then MEM1.
- `free`, `realloc` and `malloc_usable_size` pick the bank by address bit
  0x10000000, then the pool by range. A MEM2 pointer no pool owns is logged
  and left alone.
- Explicit banks: `MEM1_alloc`/`MEM1_memalign` and `MEM2_alloc`/`MEM2_realloc`/`MEM2_free`.
- `MALLOC_MEM2 = 0` keeps newlib's sbrk in MEM1.

## Owners and caches in RAM

| Owner | Bank | Size and lifetime |
| --- | --- | --- |
| XFBs, GX FIFO | MEM1 | ~2.2 MB, from start to exit |
| Filesystem cache per mounted FAT/NTFS/ext2 partition (`PartitionHandle.cpp` `CACHE 32`, `SECTORS 64`) | MEM2 (32 KB pages, allocated by the prebuilt libs through the wrappers) | 1 MiB per partition at 512-byte sectors, 8 MiB at 4 KiB. SD + up to 8 USB partitions, kept while mounted |
| cIOS USB bounce buffer (`usbstorage2.c` `mem2_ptr`) | MEM2 | 512 KB, from first use until `USBStorage2_Deinit()` |
| IOS58 USB heap (`usbstorage_libogc.c`) | MEM2 | 18 KB, kept for good (freeing it froze the bench Wii) |
| GUI textures (`GuiImageData`, `GDImageToRGBA8`) | MEM2 when > 30 KB | Width × height × 4. *measured* largest: 1,228,800 (640x480), 711,040, 604,160, 443,520, 307,200 B. Freed with the window |
| Decoded theme images (`ResourceManager`) | as above | Shared by refcount, keyed by source pointer |
| Theme files as loaded (`Resources::LoadFiles`) | heap | Raw files from the theme folder, kept until the next theme load |
| Covers (`GuiImageAsync`) | MEM2 | One texture per visible slot, loaded by its own thread. A queue, not a cache |
| Channel grid banners (`BannerAsync`) | MEM2/MEM1 | 12 per page plus prefetch, each a full banner layout with textures; deleted through `DeleteQueued()` |
| `opening.bnr` (`BNRInstance`) | heap | One banner at a time; on-card copies live in `cache_bnr/` |
| Glyph cache (`FreeTypeGX::fontData`) | heap | One texture per (size, character), freed only by `unloadFont()` when the font object goes: it only grows |
| Font file | heap | `font.ttf` or the built-in font, kept |
| Game lists (`gameList`, `GameTitles`) | heap | `struct discHdr` is ~380 B (it holds `path[260]`), so about 380 KB per 1,000 games |
| GameTDB (`wiitdb.xml`) | heap | The node offset table (`NodeCount` entries, cached in `wiitdb_offsets.bin`) stays in RAM; nodes are read from the file on demand |
| Sound decoders (`SoundDecoder`) | heap | 8 × 8 KB ring per decoder, plus a resample buffer |
| Downloads (`struct download`, `https.c`) | MEM2 when large | Whole body in one growing block (being changed to stream to the card) |
| Homebrew image to boot (`BootHomebrew.cpp`) | MEM2 | Up to 16 MB, from `CopyHomebrewMemory()` to the boot |
| libogc late buffers | SYS arena above the heap | Bluetooth 85 KB per `WPAD_Init()`, never freed; net heap 64 KB once |

## Caches on the card

All are under the config path unless a setting moves them.

| File or folder | Holds | Setting |
| --- | --- | --- |
| `cache/WII.cache`, `GAMECUBE.cache`, `EMUNAND.cache` | Game header lists, versioned by `GAME_HEADER_CACHE_VERSION` | `GameHeaderCachePath` |
| `cache_bnr/` | Banners (`opening.bnr`) for the channel grid and game window | `BNRCachePath` |
| `TitlesCache.bin` | Game titles from `wiitdb.xml` | `titlestxt_path` |
| `wiitdb_offsets.bin` | Node offsets into `wiitdb.xml`; they size allocations, so treat the file as untrusted | next to `wiitdb.xml` |
| `images/`, covers paths | Cover PNGs | the cover path settings |

## CPU cache rules that bite here

- **Fixed addresses.** After writing data that a device or the next program
  reads (homebrew image, booter, args, low-memory globals), call `DCFlushRange`.
  For code, also call `ICInvalidateRange`. `RunAppbooter()` and `disc.c` show the
  pattern.
- **Device-written buffers.** `IOS_Ioctl` and `WDVD_*` handle the cache for
  their own buffers. Do not invalidate a range that shares a 32-byte line with
  live data: allocate such buffers 32-byte aligned and padded.
- **Uncached aliases.** The XFBs are used through K1 (`MEM_K0_TO_K1`). Pass only
  K0 pointers to `free()`.

## Measuring

- `MEM_Report(where)` logs one line per call, at start-up, after start-up and at
  each menu change:
  `mem <where>: arena1 lo-hi heap A used U free F | pool base+size base+size free N largest L | arena2 lo-hi`.
  It goes to gecko output, and to the hbc agent's log on a Wii.
- `EXTRA_CFLAGS=-DDEBUG_MEM scripts/diag.sh autoinput` builds a DOL that logs
  every allocation of 256 KB or more as `mem <kind> <size> -> <ptr> from <caller>`.
  Resolve the callers with `powerpc-eabi-addr2line -f -C -e boot.elf <addr>` in
  the `usbloadergx-toolchain` image.
- The hbc agent's status `mem1_free` and `mem2_free` are the unclaimed SYS
  arenas, not GX's free heap. Use the `mem` lines instead.
