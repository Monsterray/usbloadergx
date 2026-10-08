# Build:
# docker build -o . .
#
# Produces usbloader_gx.zip in the repository root. This is the same image and
# the same build mode the CI workflow uses, so the boot.dol it writes matches
# the one CI publishes. .dockerignore keeps build output out of the context;
# .git stays in it because makexml.sh reads the commit for source/version.h.

# Use an official image
FROM devkitpro/devkitppc:20250527 AS usbloadergx

# Copy current folder into container, then compile
COPY . /projectroot/
WORKDIR /projectroot
RUN make zip -j$(nproc)

# Copy the ZIP file out of the container
FROM scratch AS export-stage
COPY --from=usbloadergx /projectroot/usbloader_gx.zip /
