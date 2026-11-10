# Dependencies

USB Loader GX gets its libraries in three ways.

## 1. devkitPro packages

The Dockerfile's `toolchain` stage starts from `devkitpro/devkitppc:20260503`. That image
has libogc 3 and devkitPro's `ppc-*` portlibs, and GX links these from it:

| Library | Package | Used for |
|---|---|---|
| zlib | ppc-zlib | zip files, PNG |
| libpng | ppc-libpng (libpng16) | images |
| FreeType | ppc-freetype | fonts |
| libgd | ppc-libgd | cover and image conversion |
| libjpeg-turbo | ppc-libjpeg-turbo | JPEG covers |
| libogg, Tremor | ppc-libogg, ppc-libvorbisidec | OGG music |
| libmad | ppc-libmad | MP3 music |
| bzip2, brotli, libwebp | ppc-bzip2, ppc-brotli, ppc-libwebp | needed by FreeType and libgd |

The stage also installs `devkitppc-crtls` 2.1.0: the image still has 2.0.0, which
leaves the DOL sections unaligned (see the Dockerfile).

To update these, change the image tag in the Dockerfile.

## 2. Built from upstream source: `deps/build.sh`

devkitPro does not package these. `deps/build.sh` downloads each one at a pinned
version, checks its sha256 and builds it into `$DEVKITPRO/portlibs/usbloadergx`. The
toolchain stage runs it, so CI, the dev container and `scripts/*.sh` all get the same
libraries.

| Library | Version | Used for |
|---|---|---|
| wolfSSL | 5.9.4 | HTTPS (`source/network/https.c`) |
| pugixml | 1.16 | `wiitdb.xml`, categories, homebrew `meta.xml`, Wiinnertag |
| minizip | from zlib 1.3.2 | zip extraction (`ZipFile`, `utils/minizip/miniunz.c`) |
| hbc agent | hbc-reborn 1.15.11 (develop b01e0c8) | HBC's in-app agent |
| libfat (`libcustomfat.a`) | extremscorner/libfat c756e1e (libFAT 1.2.0+) | FAT32 drives and SD |
| libntfs | extremscorner/libntfs 417e8dd (NTFS-3G 2026.9.28) | NTFS drives |
| libext2fs | this repository's `libs` branch, 910f6f5 (`libcustomext2fs`) | ext2/3/4 drives |

The three filesystem libraries replace the prebuilt `.a` files that used to sit in
`source/libs/`. Sources, pinned by sha256 (see `deps/build.sh` for the full URLs):

| Library | Tarball sha256 | Patches in `deps/libfs/` |
|---|---|---|
| libfat | `46a7bc78938c7d57a61f1712da8125bc0f7d22912029f3c67d52a98bfbd0356f` | `fat/gx-classic-disc-interface.patch`, `fat/fatfile_frag.[ch]`, `fat/libfatversion.h` |
| libntfs | `5fbaa98b9a834f82c7ca224e96b8622c8adf88d7f5025f15fe2bfdda794c3d29` | `ntfs/gx-classic-disc-interface.patch`, `ntfs/*_frag.*` |
| libext2fs | `19a582f8d90b118e8104369ab31746b741111547d9e3f45349aa0f00693a146c` | `ext2/gx-libogc3.patch` |

Why each patch exists:

- Classic `DISC_INTERFACE` (fat, ntfs): libogc 3's `DISC_INTERFACE` is the original one, and
  the upstream forks use Extrems' libogc2 variant (`disc` argument, flush call, variable sector
  size). The patch moves them back. It also keeps the 1.1.5 boot-sector range check on the
  sector size and uses a plain alignment test instead of `SYS_IsDMAAddress`.
- Fragment functions (`*_frag.*`): `_FAT_get_fragments()`, `_NTFS_get_fragments()` and
  `_EXT2_get_fragments()` give GX the sector runs of a game file, which the cIOS reads
  directly. Upstream has none of them; the code is from the `libs` branch.
- Recursive locks (fat, ext2): the old ext2 library locked its volume mutex twice on a failed
  mount (`ext2DeinitVolume` then `ext2Sync`) and deadlocked, which froze GX at start-up with an
  ext partition. The volume locks are recursive now.
- AddDevice slot fix (ext2): a failed `AddDevice` leaked the device and left the slot taken.
- ext2 double-free fix: the mount error path and `ext2Unmount` closed the filesystem twice
  and freed the cache twice; cleanup now runs once.

`libfat` installs as `libcustomfat.a` and its `fat.h` goes ahead of libogc 3's: the image's own
`libfat.a` is a FatFs build behind another `fat.h`. Measured in Dolphin with the harness on a
FAT32 image: mount and unmount each write one sector (the FSInfo sector), the same as the old
prebuilt 1.1.5.

The wolfSSL configuration is `deps/wolfssl/user_settings.h`. Three of its settings are
not optional:

- `WORDS_BIGENDIAN`: without it every hash comes out wrong and `wc_InitRng()` fails its
  self-test.
- `SP_INT_BITS 4096`: art.gametdb.com has a 4096-bit RSA key.
- `CUSTOM_RAND_GENERATE_SEED`: the Wii has no random source the PowerPC can read, so GX
  supplies the seed (`source/network/seed.c`).

To update one of these libraries, change its version, URL and sha256 at the top of
`deps/build.sh`. Then build, and test what it is used for. HTTPS can be tested in Dolphin,
whose sockets reach the host:

```
scripts/diag.sh autoinput
DOL=.dev/autoinput/boot.dol AUTOINPUT=<script> scripts/dolphin_run.sh <out> 60
```

with a script such as:

```
24000 press B
34000 download https://art.gametdb.com/wii/cover/US/RMCE01.png
```

`press B` closes the start-up USB prompt; the network starts only after it.

With a native devkitPro instead of Docker, run `deps/build.sh` once with the same
devkitPPC and libogc as the Dockerfile.

## 3. Prebuilt, in the repository

These have no source in the repository, or none that can be rebuilt with devkitPPC
alone. Rebuilding any of them is a project of its own, and it needs tests on a Wii.

| File | What it is | Origin | To rebuild |
|---|---|---|---|
| `source/mload/modules/ehcmodule_5.c` | Hermes/rodries EHCI module for cIOS 222/223, as a C array | GX 2010-10, from WiiFlow | Source (ehcmodule, tinyehci, mload) on this repository's `ehcmodule_rodries` branch (2011); devkitARM. |
| `source/mload/modules/odip_frag.c`, `dip_plugin_249.c` | DIP plugins for cIOS 249 and Hermes cIOS, as C arrays | GX 2010-10, from WiiFlow | Their cIOS sources, devkitARM. |
| `source/patches/codehandler*.c`, `codehandleronly.h`, `kenobiwii.h` | Gecko/Ocarina cheat code handlers, as C arrays | GX 2009-2011 | Gecko OS code handler assembly. |
| `data/binary/app_booter.bin` | Homebrew app booter | dimok, WiiXplorer, 2011-07 | WiiXplorer's app booter source. |
| `data/binary/stub.bin` | Return-to-loader stub | upstream GX, 2021-08 (bcfac02d) | Source not published with it. |
| `data/magic_patcher.o` | AHBPROT patcher for DVD access on IOS58 | GX, 2011-01 | Source not in the repository. |

The `libs` and `ehcmodule_rodries` branches are kept for these sources; they are separate trees, not
branches of the loader, and never merge into `master`.

The filesystem libraries write to users' drives, so their change from prebuilt to source
needs testing on FAT32, NTFS and ext2/3/4 drives on a Wii (Dolphin has no USB; it was tested
there with the SD interface standing in for USB).
