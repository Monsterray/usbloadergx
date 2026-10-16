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
| 0x80003F00-0x80004000 | Left alone | |
| 0x80004000-0x80B00000 | Texture pool, low part (11 MB): GUI texture pixels while the menus run; the game, apploader or booted app loads here afterwards | Opened after start-up, closed at the top of `AppCleanUp()`. See "MEM1 texture pool" |
| 0x80B00000-~0x8111C600 | GX's image: `.text` 3.3 MB, `.rodata` 1.9 MB, `.eh_frame` 0.4 MB, `.data` 72 KB, `.bss` 0.7 MB | Linked with `--section-start,.init=0x80B00000` (Makefile). `.bss` holds the 128 KB main stack (`s_mainStack`) and the network pbuf pool (~200 KB) |
| BSS end-0x81700000 | newlib heap (sbrk from `SYS_GetArena1Lo()`) | *measured*: Arena1Lo 0x81335000 at start, 0x814C0000 at peak, heap ~3.7 MB |
| 0x81700000-0x81800000 | Texture pool, high part (1 MiB) | The apploader writes the game's FST at the top of MEM1 while GX's heap is still in use; `MAX_MEM1_ARENA_LO` in `mem2.cpp` keeps the heap below 0x81700000, and `TEX_init()` lowers Arena1Hi to 0x81700000 for good |
| 0x817FE000 | BI2 (channel boot writes and clears it) | Boot only |

Arena1Hi is 0x81800000 (`__Arena1Hi`) until `TEX_init()` lowers it to
0x81700000. The first heap blocks are the two XFBs
(`SYS_AllocateFramebuffer`, 614,400 B each at 640x480, 734,720 B at 640x574) and the 768 KB GX FIFO
(`GP_FIFO_SIZE` in `video.cpp`). They are allocated in `InitVideo()` before
`MEM2_init()`, so they are always in MEM1, as VI needs.

## MEM1 texture pool

`TEX_init()` (`main.cpp`, after `StartUpProcess::Run()`) opens two
`CMEM2Alloc` pools in MEM1 that nothing else uses while the menus run:
0x80004000 up to `__app_start` (GX's `.init`, 0x80B00000), and
0x81700000-0x81800000. Only texture pixels go there (`TEX_alloc`,
`TEX_adopt`): never a buffer that libogc, IOS or a device reads or writes, and
never anything a boot path still uses after `AppCleanUp()`.

`GuiTexturePoolClose()` is the first step of `AppCleanUp()`. It closes the
pool for good (no new blocks, every later `free()` of a pool pointer is
dropped, nothing walks the block list again), then waits up to 2 s for a cover
the `GuiImageAsync` thread is decoding into a block it got before the close.
The banner thread is joined a few lines later in `AppCleanUp()`. After that
nothing of GX writes the pool, so whatever a boot path loads there stays as
loaded. A quick game boot never opens the pool: `AppCleanUp()` closes it
first, and `TEX_init()` does nothing after a close.

The pool opens after start-up because start-up reloads IOS; no IOS reload
happens between `TEX_init()` and `AppCleanUp()` (the others are in
`GameBooter::BootGame()`, `BootDevolution()`, `RunAppbooter()` and
`Channels::LoadChannel()`, all after `AppCleanUp()`).

### What writes this MEM1, and when

Audit of 2026-10-15 (code at 0d646f9a..da73b711). "After" means the pool is
already closed when the write happens.

| Path | What it writes | Range | Pool closed? |
| --- | --- | --- | --- |
| libogc start-up (`SYS_Init`) | OS globals, exception vectors, DSP init code copied through 0x81000000 | 0x80000000-0x80003F00, 0x81000000 (GX's image) | Before the pool opens; outside it |
| libogc at run time | Exception context `__ppc_excpt_buf` 0x800000D0, table 0x80003000, IOS globals 0x80003140; crash screen XFB at 0x81700000 only on a crash | Low globals; top MiB on a crash | Outside the pool; a crash ends GX |
| GX's own image and heap | `.init` to `.bss` 0x80B00000-0x8112DF20, newlib heap above it | 0x80B00000-0x81700000 | Outside the pool |
| hbc agent, HOME overlay | netlog 0x80002F20; HBC stub at 0x80001800 (`loadStub()`, also from `HbcAgent_Home()` while the GUI runs); overlay into GX's spare XFB | Low globals, heap | Outside the pool |
| Disc mount in the GUI (`Disc_Mount`, `Disc_Open`) | Disc ID and header into 0x80000000, restored | 0x80000000-0x80000060 | Outside the pool |
| `StartUpProcess` bad boot | Priiloader word 0x8132FFFB and 0x817FEFF0 | Heap, top MiB | Before the pool opens |
| `BootDevolution()` before `ExitApp()` | `DEVO_CFG` at 0x80000020, 32 bytes of disc 1 at 0x80000000 | 0x80000000-0x80000040 | Outside the pool |
| `BootGame()` after `AppCleanUp()`: cIOS reload, `MountGamePartition`, BNR load, wip, Ocarina codes, BCA | Heap and MEM2 buffers; Ocarina codes are copied later by `load_handler()` | Heap, MEM2 | After |
| `BootPartition()`: `Disc_SetLowMem()` | OS globals | 0x80000020-0x80003188 | After |
| Apploader (`Apploader_Run`) | Apploader code at 0x81200000 (GX's heap); the game's DOL sections wherever the apploader puts them (0x80004000 up), the FST at the top of MEM1 (0x817xxxxx), BI2 | Low pool, top MiB, heap | After |
| Alternate DOL (`load_dol_image`, `Load_Dol_from_disc`) | DOL sections | Low pool and up | After |
| `gamepatches()`, Kirby patch, 480p fix, Wiimmfi | In place in the loaded game (Kirby: 0x80175D3C-0x80225F0C) | Low pool | After |
| `load_handler()` (Ocarina / Gecko code handler, multidol, hooks) | 0x80001000-0x80003000, code list 0x800022A8-0x80003000 or wherever a `gameconfig.txt` puts it | Low globals, or anywhere | After |
| Channel boot (`Channels::LoadChannel`) | Channel DOL sections, its BSS cleared up to 0x81800000, BI2 0x817FE000-0x81800000 cleared, globals | Anywhere in MEM1 | After |
| `Disc_JumpToEntrypoint()` | Exception vectors (`CloseExceptionVectors`), GX's own `.rodata`-`.sdata` zeroed | Low globals, GX's image | After |
| DIOS MIOS (`BootDIOSMIOS`) | Disc ID, `DML_CFG` at 0x80001700 and 0x81200000 | Low globals, heap | After (`ExitApp()` first) |
| Devolution after `ExitApp()` | IOS 58 reload, `loader.bin` runs from its MEM2 heap block and loads the game | Anywhere | After |
| Nintendont | Disc ID at 0x80000000 before `ExitApp()`; then the homebrew booter below | Low globals | Outside, then After |
| neek (`BootNeek`) | Kernel read into a MEM2 heap block before `ExitApp()`; `NEEK_CFG` at 0x81200000 and the ES patch at 0x939F0xxx after | MEM2, heap, IOS | Outside, then After |
| Homebrew (`RunAppbooter`) | `ExitApp()`, IOS reload, stub at 0x80001800, then the image to 0x92000000, args to 0x93200000, booter to 0x93000000; the app booter loads the app's sections anywhere in MEM1 | Anywhere | After |
| HBC, System Menu, Priiloader, reboot, shutdown (`sys.cpp`) | `ExitApp()`, then Priiloader words 0x8132FFFB / 0x817FEFF0 and a title launch | Heap, top MiB | After |
| `hbc.py exit` / `hbc.py run` | The agent calls `exit(0)` without `AppCleanUp()`; HBC then loads over everything | Anywhere | Not closed, but GX no longer runs: the GPU may read stale texture memory for a moment, nothing of GX writes it |

### Measured (Dolphin, 2026-10-15)

AUTOINPUT builds of da73b711 plus the `memreport` command (before) and of the
pool (after), 4:3, the 12-game fixture plus 48 more GameCube stubs and a 3D, 2D
and disc cover for each of the 60. The pool has 12,566,528 bytes (11,517,952
low, 1,048,576 high). MEM2's largest free block stays 28,438,208 throughout.

| Point | MEM1 heap used before / after | MEM2 free before / after | Pool used |
| --- | ---: | ---: | ---: |
| All-buttons, 170 clicks (12 games, no covers), last `menu` line | 3,068,816 / 3,025,312 | 48,693,216 / 50,070,272 | 1,376,448 |
| Carousel, 16 covers up | 3,375,888 / 3,158,400 | 46,212,864 / 50,159,552 | 4,165,024 |
| Carousel, game window open | 3,664,056 / 3,424,336 | 46,023,456 / 50,158,656 | 4,376,928 |
| Grid, 3 rows of covers | 3,459,208 / 3,263,632 | 41,712,864 / 50,160,608 | 8,643,872 |

Both all-buttons runs pass. A Nintendont boot from the banner window runs
`AppCleanUp()` and the app booter the same way on both builds (Dolphin cannot
run Nintendont's kernel, so neither shows Nintendont's screen).

### Things only a real Wii shows

- Every boot path above writes the pool range only after `AppCleanUp()`, by
  reading the code. Dolphin has no cIOS, USB or disc drive, so it boots none
  of the game paths; each needs one real boot on the Wii.
- That IOS writes nothing in 0x80004000-0x80B00000 or 0x81700000-0x81800000
  while the menus run (no IOS reload happens then, see above).
- GX texturing from MEM1 instead of MEM2 on the real GPU: covers, theme
  images and channel-grid banners drawing correctly.

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
  and left alone. A MEM1 pointer inside the texture pool's ranges goes to the
  pool, also after it closed (the free is then dropped); any other MEM1
  pointer goes to newlib.
- Texture pixels: `TEX_alloc` (the MEM1 texture pool, else `memalign(32)`),
  and `TEX_adopt` to move a finished buffer there.
- Explicit banks: `MEM1_alloc`/`MEM1_memalign` and `MEM2_alloc`/`MEM2_realloc`/`MEM2_free`.
- `MALLOC_MEM2 = 0` keeps newlib's sbrk in MEM1.

## Owners and caches in RAM

| Owner | Bank | Size and lifetime |
| --- | --- | --- |
| XFBs, GX FIFO | MEM1 | ~2.2 MB, from start to exit |
| Filesystem cache per mounted FAT/NTFS/ext2 partition (`PartitionHandle.cpp` `CACHE 32`, `SECTORS 64`) | MEM2 (32 KB pages, allocated by the prebuilt libs through the wrappers) | 1 MiB per partition at 512-byte sectors, 8 MiB at 4 KiB. SD + up to 8 USB partitions, kept while mounted |
| cIOS USB bounce buffer (`usbstorage2.c` `mem2_ptr`) | MEM2 | 512 KB, from first use until `USBStorage2_Deinit()` |
| IOS58 USB heap (`usbstorage_libogc.c`) | MEM2 | 18 KB, kept for good (freeing it froze the bench Wii) |
| GUI textures (`GuiImageData`, `GDImageToRGBA8`, TPL images, `GuiImage` colour and copy buffers) | MEM1 texture pool, else as `memalign` | Width × height × 4. *measured* largest: 1,228,800 (640x480), 711,040, 604,160, 443,520, 307,200 B. Freed with the window |
| Banner layouts (`Banner` `banner.bin`, `icon.bin`) | MEM1 texture pool (`TEX_adopt`), else as `memalign` | GX draws their textures straight out of the buffer |
| Decoded theme images (`ResourceManager`) | as GUI textures | Shared by refcount, keyed by source pointer |
| Theme files as loaded (`Resources::LoadFiles`) | heap | Raw files from the theme folder, kept until the next theme load |
| Covers (`GuiImageAsync`) | MEM1 texture pool, else MEM2 | One texture per visible slot, loaded by its own thread. A queue, not a cache |
| Channel grid banners (`BannerAsync`) | layouts in the texture pool; the rest MEM2/MEM1 | 12 per page plus prefetch, each a full banner layout with textures; deleted through `DeleteQueued()` |
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
  `mem <where>: arena1 lo-hi heap A used U free F | pool base+size base+size free N largest L | arena2 lo-hi | tex base+size base+size free T largest TL`.
  The `tex` part is the MEM1 texture pool (free 0 before it opens and after it
  closes). It goes to gecko output, and to the hbc agent's log on a Wii.
- The menu loop's `menu` lines come after a menu has returned, so its textures
  are freed by then. An AUTOINPUT build's `memreport` script line logs
  `mem autoinput <ms>` with a browser and its covers still up.
- `EXTRA_CFLAGS=-DDEBUG_MEM scripts/diag.sh autoinput` builds a DOL that logs
  every allocation of 256 KB or more as `mem <kind> <size> -> <ptr> from <caller>`.
  Resolve the callers with `powerpc-eabi-addr2line -f -C -e boot.elf <addr>` in
  the `usbloadergx-toolchain` image.
- The hbc agent's status `mem1_free` and `mem2_free` are the unclaimed SYS
  arenas, not GX's free heap. Use the `mem` lines instead.
