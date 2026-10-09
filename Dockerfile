# Build:
# docker build -o . .
#
# Produces usbloader_gx.zip in the repository root. This is the same image and
# the same build mode the CI workflow uses, so the boot.dol it writes matches
# the one CI publishes. .dockerignore keeps build output out of the context;
# .git stays in it because makexml.sh reads the commit for source/version.h.

# The toolchain: the official devkitPPC image (devkitPPC r50, libogc 3 and the
# ppc-* portlibs GX uses), devkitppc-crtls 2.1.0, and the libraries deps/build.sh
# builds from pinned upstream sources. CI, .devcontainer and scripts/*.sh all
# build GX in this stage, so this is the one place the toolchain is defined.
FROM devkitpro/devkitppc:20260503 AS toolchain
# crtls 2.0.0 in this image leaves the DOL segment sizes unaligned; IOS, the
# apploaders and Dolphin read each segment rounded up to 32 bytes, past the end
# of boot.dol, and Dolphin refuses to boot it. 2.1.0 pads them (devkitPro
# devkitppc-crtls 11ee160d). Drop this line once an image ships 2.1.0.
RUN dkp-pacman -U --noconfirm https://pkg.devkitpro.org/packages/devkitppc-crtls-2.1.0-1-any.pkg.tar.zst
COPY deps/ /tmp/deps/
RUN bash /tmp/deps/build.sh /opt/devkitpro/portlibs/usbloadergx && rm -rf /tmp/deps

# Copy current folder into container, then compile
FROM toolchain AS usbloadergx
COPY . /projectroot/
WORKDIR /projectroot
RUN make zip -j$(nproc)

# Copy the ZIP file out of the container
FROM scratch AS export-stage
COPY --from=usbloadergx /projectroot/usbloader_gx.zip /
