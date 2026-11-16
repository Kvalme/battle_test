# pugixml 1.16

Vendored from the official release tag:
https://github.com/zeux/pugixml/tree/v1.16

Upstream files, unchanged:

- `src/pugixml.hpp` → `pugixml.hpp`
- `src/pugixml.cpp` → `pugixml.cpp`
- `src/pugiconfig.hpp` → `pugiconfig.hpp`
- `LICENSE.md` → `LICENSE.md` (MIT)

CMake uses an INTERFACE target with `PUGIXML_HEADER_ONLY` and
`PUGIXML_NO_XPATH`. In this upstream-supported mode, `pugixml.hpp` includes
`pugixml.cpp`; the `.cpp` file must remain next to the header, but is not compiled
as a separate translation unit or linked as an installed library.

No FetchContent, submodules, package installation or network access is needed to
build the project. To update, replace all four upstream files from a fixed release,
retain its license, update this version and the checksums below, then run CTest.

SHA-256 of upstream files:

```text
90974fbb120e966b452654eaae3ba6ee55f3647db3b28918a7649733ee99da97  LICENSE.md
e6c51fbc9a765608bd214ea47d229d3bbd6b650036cdffc94b96b498c202495c  pugiconfig.hpp
bd94cd9ec6ac67c0723422640bb243eab4a7c2d9fd5584dab77305a50fa31ef4  pugixml.cpp
0b7c94701df3d50981ffafe57f177c77b940e347c6e5d01073e4161f61299212  pugixml.hpp
```
