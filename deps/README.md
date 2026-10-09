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
| hbc agent | hbc-reborn ecb4de2 (HBC 1.10.2) | HBC's in-app agent |

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
alone. They link and run with libogc 3: the libogc functions and structures they use
(`LWP_Mutex*`, `DISC_INTERFACE`, `sys_resetinfo`, `DCInvalidateRange` aside) did not
change. Rebuilding any of them is a project of its own, and it needs tests on a Wii.

| File | What it is | Origin | To rebuild |
|---|---|---|---|
| `source/libs/libfat/libcustomfat.a` | libfat 1.1.5 with `_FAT_get_fragments()` (`fatfile_frag.h`) | Last replaced by upstream GX in 2025 (a24d7d92). No published source. | devkitPro's libfat 1.1.5 plus the fragment patch, which has to be written again from the header and from how `source/usbloader/frag.c` uses it. |
| `source/libs/libntfs/libcustomntfs.a` | libntfs-wii (NTFS-3G) with `_NTFS_get_fragments()`, MEM2 allocation and sectors over 512 bytes | Same commit as libcustomfat. No published source. | libntfs-wii (code.google export, for example rhyskoedijk/libntfs-wii) plus the GX changes listed in the GX history (2011-06, 2013-04). |
| `source/libs/libext2fs/libcustomext2fs.a` | libext2fs-wii with `_EXT2_get_fragments()` | GX, 2012-02 (R1153). No published source. | libext2fs-wii (code.google export) plus the fragment function. |
| `source/mload/modules/ehcmodule_5.c` | Hermes/rodries EHCI module for cIOS 222/223, as a C array | GX 2010-10, from WiiFlow | Hermes' cIOS sources, devkitARM. |
| `source/mload/modules/odip_frag.c`, `dip_plugin_249.c` | DIP plugins for cIOS 249 and Hermes cIOS, as C arrays | GX 2010-10, from WiiFlow | Their cIOS sources, devkitARM. |
| `source/patches/codehandler*.c`, `codehandleronly.h`, `kenobiwii.h` | Gecko/Ocarina cheat code handlers, as C arrays | GX 2009-2011 | Gecko OS code handler assembly. |
| `data/binary/app_booter.bin` | Homebrew app booter | dimok, WiiXplorer, 2011-07 | WiiXplorer's app booter source. |
| `data/binary/stub.bin` | Return-to-loader stub | upstream GX, 2021-08 (bcfac02d) | Source not published with it. |
| `data/magic_patcher.o` | AHBPROT patcher for DVD access on IOS58 | GX, 2011-01 | Source not in the repository. |

The filesystem libraries matter most: they write to users' drives, so a rebuild needs
testing on FAT32, NTFS and ext2/3/4 drives on a Wii before it replaces these files.
