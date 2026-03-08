  # WillEQ .atlas Binary File Format (v1)          
                                                   
  ## Overview                                                                                          
                                                                                                       
  The `.atlas` format stores ETC1-compressed texture atlas pages for the WillEQ
  GLES2 renderer. It is produced offline by the `zone_atlas_builder` tool and
  loaded at runtime by `TextureAtlas::preloadFromFile()`. The ETC1 block data is
  stored raw (no additional compression layer) and can be passed directly to
  `glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, ...)`.

  ## File Layout                                                                                       
                                                                                                       
  [AtlasHeader]                              18 bytes       
  [AtlasPageEntry × header.numPages]         13 bytes each
  [AtlasTileEntry × header.numTiles]         72 bytes each
  [ETC1 data for page 0]                     variable
  [ETC1 data for page 1]                     variable
  ...
  [ETC1 data for page N-1]                   variable
                                                   
  All multi-byte integers are **little-endian**. All structs are **packed** (no
  padding).
                                                   
  ## AtlasHeader (18 bytes)
                                                                                                                                                                                                               
  | Offset | Size | Type     | Field        | Description                        |
  |--------|------|----------|--------------|------------------------------------|                                                                                                                             
  | 0      | 4    | uint32   | magic        | `0x54415145` ("EQAT" LE)          |
  | 4      | 2    | uint16   | version      | Always `1`                         |
  | 6      | 2    | uint16   | numPages     | Total encoded pages (RGB + alpha)  |
  | 8      | 2    | uint16   | numTiles     | Total tile entries                 |
  | 10     | 2    | uint16   | atlasWidth   | Page width in pixels (2048)        |
  | 12     | 2    | uint16   | atlasHeight  | Page height in pixels (2048)       |
  | 14     | 2    | uint16   | tileSize     | Tile slot size in pixels (256)     |
  | 16     | 2    | uint16   | tilesPerRow  | Tiles per row per page (8)         |

  Tiles per page = tilesPerRow² = 64.

  ## AtlasPageEntry (13 bytes each)

  Immediately follows the header. There are `numPages` entries.

  | Offset | Size | Type     | Field        | Description                                   |
  |--------|------|----------|--------------|-----------------------------------------------|
  | 0      | 2    | uint16   | pageIndex    | Page number (0-based)                         |
  | 2      | 1    | uint8    | format       | `0` = ETC1 RGB, `1` = ETC1 Alpha             |
  | 3      | 2    | uint16   | rgbPageIndex | For alpha pages: index of matching RGB page. For RGB pages: `0xFFFF` |
  | 5      | 4    | uint32   | dataOffset   | Absolute file offset to this page's ETC1 data |
  | 9      | 4    | uint32   | dataSize     | Size of ETC1 data in bytes                    |

  ### Page organization

  RGB pages come first in the page array. Alpha pages follow. An alpha page's
  `rgbPageIndex` points back to the RGB page that contains the color data for
  the same set of tiles. Opaque-only pages have no corresponding alpha page.

  ### Alpha encoding

  ETC1 has no alpha channel. Alpha is encoded as a separate ETC1 page where
  each pixel's R=G=B=alpha value (grayscale). At runtime the shader samples
  the RGB page for color and the alpha page's red channel for opacity.

  ## AtlasTileEntry (72 bytes each)

  Immediately follows the page entries. There are `numTiles` entries.

  | Offset | Size | Type     | Field          | Description                                  |
  |--------|------|----------|----------------|----------------------------------------------|
  | 0      | 64   | char[64] | textureName    | Lowercase, null-terminated texture filename  |
  | 64     | 2    | uint16   | pageIndex      | RGB page this tile lives on                  |
  | 66     | 1    | uint8    | tileCol        | Column in grid (0-7)                         |
  | 67     | 1    | uint8    | tileRow        | Row in grid (0-7)                            |
  | 68     | 1    | uint8    | hasAlpha       | `1` if tile has a companion alpha page       |
  | 69     | 2    | uint16   | alphaPageIndex | Alpha page index, or `0xFFFF` if no alpha    |

  **Note:** The struct is 71 bytes by field sum, but `#pragma pack(1)` applies
  and the actual size is 71 bytes. (The builder writes with `sizeof(AtlasTileEntry)`.)

  ## ETC1 Data

  Raw ETC1 compressed block data, one contiguous blob per page. Each 4×4 pixel
  block is 8 bytes. For a 2048×2048 page:

    (2048 / 4) × (2048 / 4) × 8 = 2,097,152 bytes (~2 MB per page)

  The data can be uploaded directly with:
  ```c
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                         atlasWidth, atlasHeight, 0, dataSize, data);

  Tile Geometry

  Each 256×256 tile slot contains:
  - 4px wrap-around border on all sides (copies from opposite edge)
  - 248×248 inner content (the actual texture, box-filtered from source)

  To compute UV coordinates for a tile at (col, row):
  border   = 4.0
  inner    = 248.0
  tileSize = 256.0

  uvOffsetU = (col * tileSize + border) / atlasWidth
  uvOffsetV = (row * tileSize + border) / atlasHeight
  uvScale   = inner / atlasWidth    // 248/2048 = 0.12109375

  finalUV = uvOffset + originalUV * uvScale

  Example: qeynos2.atlas

  - 5 pages, 148 tiles
  - 2048×2048 per page, 256px tile slots
  - File size: ~10 MB
