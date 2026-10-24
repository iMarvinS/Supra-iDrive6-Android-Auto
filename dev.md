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

---

## 8. Talking to the head unit over SSH (sshpass) & bulk transfer

The HU runs an SSH server, but it is a constrained QNX box:

- The host key changes across reflashes, so skip host-key checks.
- **The sshd falls over if you hammer it with many rapid connections.** Do each job in **as
  few SSH connections as possible** — ideally one. This is the main constraint below.
- Everything that should run on the HU must be wrapped in `on -f /net/hu-omap/ …` so it
  executes on the omap node (the QNX network node that owns `/dev`, the daemon, etc.).
- The HU `/bin/sh` is minimal: **no `scp`, `tar`, `pax`, `head`, `tail`, `printf`**. Available:
  `echo`, `cat`, `find`, `grep`, `wc`, `gzip`/`gunzip`, `cpio`, `pidin`, `slay`, `on`, `waitfor`.

### 8.1 The connection pattern (non-interactive, password in env)

```sh
export SSH_PASS='<hu-root-password>'
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10"

SSHPASS="$SSH_PASS" sshpass -e ssh $SSH_OPTS root@169.254.199.99 \
  "on -f /net/hu-omap/ /bin/sh -c 'pidin ar | grep -i nbt-aad'"
```

- **`sshpass -e`** reads the password from the `SSHPASS` env var, *not* from argv — so it never
  shows up in the HU/host process list (unlike `sshpass -p`).
- Keep the remote command as a **single `/bin/sh -c '…'`**. Nested quoting is fragile on the HU
  shell; when a command needs its own quotes (e.g. `find … -iname "…"`), prefer flipping the
  outer quotes to single and the inner to double, or — better — ship a script file (§8.4).
- Add **`-tt`** only when you want a live foreground stream (and Ctrl-C to reach the remote
  process). **Never** use `-tt` for binary streams (§8.2/8.3) — a pseudo-tty mangles bytes
  (CR/LF translation).

### 8.2 Copy ONE file TO the head unit (cat-pipe, no scp)

```sh
cat ./nbt-aad | SSHPASS="$SSH_PASS" sshpass -e ssh $SSH_OPTS root@169.254.199.99 \
  "on -f /net/hu-omap/ /bin/sh -c 'cat > /tmp/nbt-aad && chmod +x /tmp/nbt-aad'"
```

### 8.3 Pull ONE file FROM the head unit (gzip over the pipe)

`gzip` exists on both ends; it also compresses the transfer. No `-tt`.

```sh
SSHPASS="$SSH_PASS" sshpass -e ssh $SSH_OPTS root@169.254.199.99 \
  "on -f /net/hu-omap/ /bin/sh -c 'gzip -c /opt/hmi/bin/zipped/HmiMain3D'" | gunzip > HmiMain3D
```

### 8.4 Transfer MANY files / a whole directory in ONE connection

There is no `tar`/`pax` on the HU, and the sshd can't take a connection-per-file. So stream a
**self-describing archive** built with shell + `gzip` in a single connection, and unpack it
locally. Per file the HU emits `size\n`, `path\n`, then the raw bytes; everything is gzipped
once. Locally, `gunzip` the stream and read each header line + exactly `size` bytes.

```sh
# --- remote side (one SSH): find every file, emit size/path/bytes, gzip the whole stream ---
REMOTE_DIR=/opt/conn/lib/carplay
remote='cd "'"$REMOTE_DIR"'" && find . -type f | while IFS= read -r f; do
  sz=$(wc -c < "$f"); sz=${sz##* };      # strip leading spaces from wc
  echo "$sz"; echo "$f"; cat "$f";        # QNX sh has no printf -> use echo
done | gzip -c'

# --- local side: one connection, parse the gunzipped stream into ./out/ ---
mkdir -p out
exec 3< <(SSHPASS="$SSH_PASS" sshpass -e ssh $SSH_OPTS root@169.254.199.99 \
            "on -f /net/hu-omap/ /bin/sh -c '$remote'" | gunzip)
while IFS= read -r size <&3; do
  IFS= read -r path <&3 || break
  size="${size//[^0-9]/}"; path="${path#./}"
  [ -n "$path" ] || continue
  mkdir -p "out/$(dirname "$path")"
  if [ "${size:-0}" -gt 0 ]; then head -c "$size" <&3 > "out/$path"; else : > "out/$path"; fi
done
exec 3<&-
```

Key points:
- **`echo` instead of `printf`** (QNX sh has no `printf`); **`gzip` not `tar`/`pax`** (absent).
- **No `-tt`** anywhere — the stream is binary.
- Reading on the local side uses one `bash` process: `read` consumes exactly the two header
  lines, then `head -c "$size"` consumes exactly the payload (both from fd 3), so text headers
  and raw binary interleave safely in a single pipe.
- `cpio` is a fallback if present: remote `find . | cpio -o` → local `cpio -idmv`.

This is the technique used by the out-of-tree helper `tools/pull-dir.sh`; the deploy/run and
"bring the whole chain up" helpers (`tools/deploy-run.sh`, `tools/hu-up.sh`) use §8.1–8.2.

### 8.5 Running the daemon over SSH: set `LD_LIBRARY_PATH`

A non-interactive `/bin/sh -c` does **not** source the login profile, so it inherits no
`LD_LIBRARY_PATH`. The daemon then fails to `dlopen` the colibry/conn libs ("Library cannot be
found"). Set it explicitly in the remote command:

```sh
… "on -f /net/hu-omap/ /bin/sh -c 'cd /tmp && \
  LD_LIBRARY_PATH=/opt/sys/lib:/opt/conn/lib:/opt/conn/lib/carplay:/opt/mm/lib:/lib:/usr/lib \
  NBT_WIRELESS=1 ./nbt-aad'"
```
