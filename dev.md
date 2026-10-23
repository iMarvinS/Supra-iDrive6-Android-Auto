# dev.md — Local Build with Podman (QNX ARMv7LE)

How to build `nbt-aad` (and `test_display`) locally on Linux — no Windows/QNX-Momentics VM —
using **Podman** and a prebuilt QNX 6.5 cross-toolchain image.

The project targets **QNX Neutrino 6.5.0, ARMv7LE** (BMW NBT Evo / TI Jacinto). The host
compiler is proprietary (QNX `qcc`/`ntoarmv7-gcc`), so the whole toolchain plus the QNX SDP
sysroot comes from a container image.

---

## 1. Prerequisites

- **Podman** (rootless is fine). Check: `podman --version`
- **git** and network access (for the one-time image build)
- Host architecture **x86_64** (the image is `linux/amd64`; native build, no QEMU needed)
- ~2 GB of disk space for the image

---

## 2. Build the toolchain image (one time)

The image is **not** published to any registry — it is built from the Dockerfile.

```sh
# Get the toolchain repo:
git clone https://github.com/luka-dev/qnx65-armv7-toolchain.git
cd qnx65-armv7-toolchain

# Build the 4.9 variant (GCC 4.9.4, C/C++ only — best fit for this project):
podman build --platform=linux/amd64 --target base-env --build-arg BASE=base-4.9 \
  -t qnx65-armv7-toolchain:4.9 .
```

Notes:

- The build compiles GCC from source → **~20–40 min** the first time. After that the image
  is cached and is not rebuilt.
- The QNX 6.5 SDP sysroot (incl. OpenSSL headers, `libusbdi`, `libscreen`, …) is baked into
  the image.
- Why 4.9 and not 8.5: the original was built with GCC 4.8.x, the Makefile uses
  `-std=gnu++0x`, and the head unit ships an old `libstdc++` — GCC 4.9 is the closest match.
- The image includes a **`qcc` shim** that translates the Makefile's qcc flags (`-V…cpp`,
  `-Wc,…`, `-lang-c++`) to the cross-GCC, so `make` runs unchanged.

Verify:

```sh
podman images qnx65-armv7-toolchain:4.9
```

---

## 3. Build the project

From the **Milestone-2 directory** (it is mounted as `/src` inside the container):

```sh
cd Milestone-2
podman run --rm --platform=linux/amd64 -v "$PWD":/src \
  localhost/qnx65-armv7-toolchain:4.9 make
```

Result:

- Object files → `Milestone-2/build/*.o`
- Binaries → `Milestone-2/nbt-aad` and `Milestone-2/test_display`

Clean:

```sh
podman run --rm --platform=linux/amd64 -v "$PWD":/src \
  localhost/qnx65-armv7-toolchain:4.9 make clean
```

Optional convenience alias (e.g. in `~/.zshrc`):

```sh
alias qnxmake='podman run --rm --platform=linux/amd64 -v "$PWD":/src localhost/qnx65-armv7-toolchain:4.9 make'
# then, inside Milestone-2:  qnxmake   or  qnxmake clean
```

---

## 4. Verify the build (optional)

Confirm it is a real QNX ARM executable:

```sh
podman run --rm --platform=linux/amd64 -v "$PWD":/src localhost/qnx65-armv7-toolchain:4.9 \
  sh -c 'file nbt-aad; arm-unknown-nto-qnx6.5.0eabi-readelf -d nbt-aad | grep -E "NEEDED|RPATH"'
```

Expected: `ELF 32-bit LSB executable, ARM, EABI5 … interpreter /usr/lib/ldqnx.so.2`,
with NEEDED including `libssl.so.2`, `libcrypto.so.2`, `liba2i_client.so`, `libscreen.so.1`.

---

## 5. Makefile tweaks for the Linux/Podman build

Two QNX-6.5-specific fixes are already in place (no changes to the source code):

1. **`-Wc,-include,cstddef -Wc,-include,cstdint`** in `CXXFLAGS` — QNX 6.5 headers plus a
   modern GCC don't agree on `size_t`/`uintN_t` out of the box.
2. **`libssl`/`libcrypto` linked by explicit path** (`./lib/libssl.so ./lib/libcrypto.so`)
   instead of `-lssl -lcrypto` — the QNX gcc spec searches the sysroot libdir before
   `-L./lib`, and the old sysroot OpenSSL doesn't export `TLSv1_2_client_method`.

Object files also go into `build/` instead of next to the sources.

---

## 6. Deploy to the head unit & run (short form)

```sh
# Copy the binary to the omap node (cat pipe — needs no scp on the device):
cat ./nbt-aad | ssh root@169.254.199.99 \
  "on -f /net/hu-omap/ /bin/sh -c 'cat > /tmp/nbt-aad && chmod +x /tmp/nbt-aad'"

# run:
ssh root@169.254.199.99
on -f /net/hu-omap/ /bin/sh
cd /tmp && ./nbt-aad

# stop (from another terminal):
ssh root@169.254.199.99 "on -f /net/hu-omap/ slay nbt-aad"
```

---

## 7. Runtime caveat

The binary links against `libstdc++.so.6` from GCC 4.9 (≈ 6.0.20). The head unit has an
older `libstdc++.so.6.0.13`. Compiling/linking works fine, but if C++ runtime symbols are
missing at **run time**, link statically: add `-static-libstdc++ -static-libgcc` to
`LDFLAGS`.
