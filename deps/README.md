
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


MEON (R11) adds two copies made from upstream release sources (not git-subrepo):

- ixwebsocket/ — IXWebSocket v12.0.1 (commit 64fae7676bd8fe31f7cb4bcde7a6841892dad65e),
  https://github.com/machinezone/IXWebSocket, BSD-3-Clause (LICENSE.txt).
  Only the ixwebsocket/ source folder is copied, unchanged.
- mbedtls/ — Mbed TLS 3.6.7 (LTS), release tarball mbedtls-3.6.7.tar.bz2 from
  https://github.com/Mbed-TLS/mbedtls/releases, Apache-2.0 (LICENSE).
  Only include/ and library/ (.c/.h) are copied, unchanged; it is built with the default
  mbedtls_config.h by our CMakeLists.txt (meon_mbedtls).

To update: replace these folders with the new release's files and update the versions here
and in Source/meon/resources/ThirdPartyNotices.txt.
