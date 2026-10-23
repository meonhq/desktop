
The aoo, juce, and ff_meters sub-directories here are all managed subrepos
from other github repos, using git-subrepo
(https://github.com/ingydotnet/git-subrepo).

The aoo and JUCE ones are forks of mine in github, the ff_meters is the
upstream author's repo.

This way all the code for the dependencies of sonobus is completely
constained in this git repo, avoiding complexities of dealing with
submodules, but having the benefit of being able to pull/push changes back
from/to them at any time.

The Opus dependency is handled here with some pre-built static library
versions of Opus for Mac, Windows and iOS, which can be found in the
mac/windows/ios subdirectories here. For the linux build the opus library
must be installed and available via pkg-config.


MEON (R11) adds copies made from upstream release sources (not git-subrepo):

- ixwebsocket/ — IXWebSocket v12.0.1 (commit 64fae7676bd8fe31f7cb4bcde7a6841892dad65e),
  https://github.com/machinezone/IXWebSocket, BSD-3-Clause (LICENSE.txt).
  Only the ixwebsocket/ source folder is copied, unchanged.
- mbedtls/ — Mbed TLS 3.6.7 (LTS), release tarball mbedtls-3.6.7.tar.bz2 from
  https://github.com/Mbed-TLS/mbedtls/releases, Apache-2.0 (LICENSE).
  Only include/ and library/ (.c/.h) are copied, unchanged; it is built with the default
  mbedtls_config.h by our CMakeLists.txt (meon_mbedtls).
- monocypher/ — Monocypher 4.0.2, git tag 4.0.2 (commit 0d85f98c9d9b0227e42cf795cb527dff372b40a4) of
  https://github.com/LoupVaillant/Monocypher, dual BSD-2-Clause / CC0-1.0 (LICENCE.md; we use BSD-2-Clause).
  Copied unchanged into one folder: src/monocypher.{c,h} and src/optional/monocypher-ed25519.{c,h}
  (the git tag's headers say "version __git__"; the release tarball fills in the number, the code is the same).
  Built by our CMakeLists.txt (meon_monocypher). Used for Ed25519 (RFC 8032, SHA-512) signature checks of
  update manifests: crypto_ed25519_check (Source/meon/MeonUpdateManifest.cpp).

To update: replace these folders with the new release's files and update the versions here
and in Source/meon/resources/ThirdPartyNotices.txt.
