# Third-party components

ImageSifter application code and original vector artwork are MIT licensed. No Adobe code, marks or UI assets are used.

## Qt 6.8.3

Copyright The Qt Company and other contributors. Qt Core, GUI, Widgets, SQL, Concurrent, SVG and Image Formats are dynamically linked under the applicable LGPLv3 terms. Corresponding Qt license texts and bundled third-party notices are in `licenses/Qt`. These directories include Qt's codecs/font/rendering dependencies; consult their individual notices.

The matching Qt base, SVG and imageformats source archives are provided in the `third-party-sources` release asset alongside the binary packages. They are also available from https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ . The Qt libraries are unmodified upstream aqt/Qt binaries. Build with a matching toolchain, then replace the compatible DLLs/plugins in the application directory; `qt.conf` keeps plugin discovery relative to that directory. ImageSifter performs no signature/integrity check that prevents replacement. Debugging/reverse engineering for modification of these LGPL components is permitted under their licenses. The complete application source and build instructions are provided in the repository and release source archive.

## LibRaw 0.22.3

Copyright LibRaw LLC and contributors. Distributed as `libsifter_raw.dll` under LGPL2.1-or-later (the upstream alternative CDDL license is also included for reference). `licenses/LibRaw` contains upstream COPYRIGHT and license texts. The unchanged source archive is provided in the third-party source release asset and at https://github.com/LibRaw/LibRaw/tree/0.22.3 . The local build uses `cmake/LibRaw.cmake`, not the optional GPL-licensed LibRaw-cmake helper project. Compatible rebuilt libraries can replace the dynamic library; the source and pinned archive hash are available in CMake.

Optional Adobe DNG SDK, GoPro SDK, JPEG-XL, RawSpeed and other external decoder SDKs are not bundled/enabled. Not every compressed camera encoding is supported by this configuration. Embedded camera previews are preferred; fallback is a half-size rendering for selection.

## easyexif

Copyright (c) 2010–2016 Mayank Lahiri and contributors. BSD-style terms are preserved in full at the top of `vendor/easyexif/exif.h` and `exif.cpp`; a copy is supplied under `licenses/easyexif`. Source is included in the repository. Used for basic JPEG metadata.

## MinGW-w64 / GCC runtime

Windows packages use the Qt MinGW13.1 toolchain and dynamically distribute libgcc/libstdc++/winpthreads as required by windeployqt. Original notices, GCC Runtime Library Exception and MinGW runtime licensing are in `licenses/MinGW`. GCC runtime exceptions apply to eligible compiled application code; application source remains MIT. The Qt toolchain distribution is available at https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw1310/ ; compiler source is available at https://gcc.gnu.org/releases.html .

The installer is built with Inno Setup. It does not introduce a service, Docker dependency or photo upload. The distribution is not code signed.
