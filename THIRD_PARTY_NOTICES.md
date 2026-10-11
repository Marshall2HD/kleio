# Third-party notices

Kleio is licensed under the MIT License (see `LICENSE`). It incorporates or is derived from the
following third-party works.

## Maccy

<https://github.com/p0deje/Maccy>

Kleio is a port of Maccy, the macOS clipboard manager. Its design, behaviour and much of its logic
(clipboard monitoring and filtering, history model, search, sorting, paste stack, settings) are
ported from Maccy's Swift sources. The sounds in `src/Kleio/Assets/Sounds/` (`Knock.wav`,
`Write.wav`) are Maccy's `Knock.caf` and `Write.caf` converted to WAV.

```
The MIT License (MIT)

Copyright (c) 2025 Alex Rodionov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Fuse-swift

<https://github.com/krisk/fuse-swift>

Kleio's fuzzy search (`src/Kleio/Core/Search.*`) is a C++ port of the Bitap-based fuzzy matching
of Fuse-swift 1.4.0, which Maccy uses.

```
Copyright (c) 2017 Kirollos Risk <kirollos@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## SQLite

<https://www.sqlite.org/>

Kleio stores its history with SQLite through `winsqlite3`, the copy of SQLite that ships with
Windows. SQLite is in the public domain; no license is required.

## Other components

Kleio is built on the Windows App SDK / WinUI 3, C++/WinRT and the Windows Implementation
Libraries (WIL), all published by Microsoft under the MIT License. They are consumed as NuGet
packages and are not redistributed in source form in this repository.
