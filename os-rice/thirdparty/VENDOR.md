# Vendored third-party sources

Everything in this directory is upstream code, checked in verbatim. Nothing
here is edited locally: refreshing a dependency means replacing the files and
updating the record below, so a diff against upstream stays meaningful.

## bearssl.h -- BearSSL 0.6, amalgamated

* Upstream: <https://bearssl.org/> (Thomas Pornin), MIT licence, reproduced in
  the header comment of `bearssl.h`.
* Tarball: <https://bearssl.org/bearssl-0.6.tar.gz>,
  sha256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`.
  Upstream publishes no checksum of its own; the hash above is of the tarball
  fetched on 2026-09-07 and is here so a later refresh is a deliberate change
  rather than a silent one.
* Taken: `inc/` and `src/`, run through `amalgamate_bearssl.py` into one
  single-header library the way `yaml.h` is (`#define BEARSSL_IMPLEMENTATION`
  in exactly one unit, `lib/bearssl.c`). The `tools/`, `test/`, `samples/`
  and build files are not used and were left behind.
* Why: Windows XP's schannel tops out at TLS 1.0, and TLS 1.1/1.2 were never
  backported to it, so WinINet cannot complete a handshake with any host worth
  fetching from today. BearSSL is the TLS 1.2 stack `lib/tls.c` speaks over a
  raw WinSock socket on that tier. Modern Windows keeps using WinINet.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `bearssl.h` by hand -- every local change lives in `amalgamate_bearssl.py`,
  so a version bump stays a re-run rather than a merge. What the script does
  to upstream is listed in the comment it writes at the top of the header.
* Build note: `lib/bearssl.c` is compiled at `-std=c99 -w` (see `nob.c`), not
  the C89 the rest of this tree holds itself to -- upstream declares
  variables after statements. Including the declarations costs nothing: the
  script rewrites the public headers' `static inline` accessors to plain
  `static`, so `lib/tls.c` builds at `-std=c89` like everything else.

## cacert.pem -- Mozilla CA bundle

* Upstream: <https://curl.se/ca/cacert.pem> (curl's extract of Mozilla's
  root store), dated 2026-08-13, 121 roots.
* sha256 `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`,
  matching <https://curl.se/ca/cacert.pem.sha256> as published.
* Why: BearSSL ships no trust store. XP's own root store is long expired, so
  the bundle travels with the binary: `nob` turns this file into a byte array
  (`build/cacert_pem.c`) and `lib/tls.c` decodes it into trust anchors at
  first use. Refreshing trust is therefore a file swap plus a rebuild.

## yaml.h -- see amalgamate_yaml.py
