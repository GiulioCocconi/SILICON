# Third-Party Licenses and Notices

This software packages and uses third-party libraries and resources.
The original copyrights and licenses are detailed below, their fulltext is in `resources/app/licenses`.

### Icons
Lucide, under ISC, _Copyright (c) 2026 Lucide Icons and Contributors_

### Font Resources
Each font is licensed under `OFL`
- Chango _Copyright (c) 2011 Fontstage (info@fontstage.com)_
- NovaMono _Copyright (c) 2011, wmk69 (wmk69@o2.pl)_
- Quicksand _Copyright 2011 The Quicksand Project Authors (https://github.com/andrew-paglinawan/QuicksandFamily)_

### Libraries
- FastLZ, under MIT, _Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)_
- LibFST, under MIT, _Copyright (c) 2009-2025 Tony Bybell_
- LZ4, under BSD, _Copyright (C) 2011-2023, Yann Collet_
- Qt, under GPL, _Copyright (C) 2018 The Qt Company Ltd. and other contributors_
- Boost Graph, under BSL, _Copyright 2002 Indiana University_
- libavoid, under LGPL, _Copyright (C) 2004-2013  Monash University_
- OGDF, under GPL, _Copyright (C) 1999–2025_
- Boost Log, under BSL, _Copyright 2007-2015 Andrey Semashev_
- Json for Modern C++, under MIT, _Copyright (c) 2013-2026 Niels Lohmann_
- TOML++, under MIT, _Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>_

### Notes
We made some minor modification to libavoid. To display them run:
```bash
git clone --depth 1 --branch v1.0.6 https://github.com/Aksem/adaptagrams /tmp/adaptagrams_orig
git diff --no-index /tmp/adaptagrams_orig/cola/libavoid ./vendor/libavoid
rm -rf /tmp/adaptagrams_orig
```
