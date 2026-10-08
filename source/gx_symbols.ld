/* Linked as an input file, so it adds to devkitPPC's linker script (rvl.ld)
   and does not replace it. libogc 2's script defined etext and edata; libogc 3's
   does not. Disc_JumpToEntrypoint() clears GX's read-only and initialised data,
   from the end of .text to the start of .sbss, before it starts a game. */
etext = ADDR(.rodata);
edata = ADDR(.sbss);
