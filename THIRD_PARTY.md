# Third-party software

WallFlow is licensed under the GNU General Public License v3.0 (see LICENSE). It uses:

## libmpv (with FFmpeg and other libraries), `libmpv-2.dll`

Video playback. The DLL in WallFlow's release zip is the unmodified prebuilt `libmpv-2.dll` from the mpv-winbuild-cmake project:

- Build: release `20261111`, file `mpv-dev-x86_64-20261111-git-bcde6b93cc.7z` (SHA-256 `273c079b11ea5575822f972756f4c9a71149a5c10130353abeb8586738938452`), from https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261111
- mpv source: commit `bcde6b93cc`, https://github.com/mpv-player/mpv/tree/bcde6b93cc
- Build scripts, and the versions of FFmpeg and every other library inside the DLL: https://github.com/shinchiro/mpv-winbuild-cmake (tag `20261111`)

This build of FFmpeg is configured with `--enable-gpl --enable-version3`, so the combined work is distributed under GPLv3. mpv itself is licensed GPLv2 or later (LGPL builds are possible; this one is not). The source code of all of the above is available at the links above; if a link ever stops working, open an issue on the WallFlow repository and the sources will be provided.

## nlohmann/json 3.12.0

Reading and writing `settings.json`. Compiled into `wallflow.exe`. https://github.com/nlohmann/json

MIT License. Copyright (c) 2013-2025 Niels Lohmann. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Microsoft C++ runtime

Linked statically into `wallflow.exe` (MSVC `/MT`), as permitted by the Visual Studio licence terms.

## GoogleTest

Used only to build and run the unit tests; not part of the release. BSD 3-Clause License. https://github.com/google/googletest
