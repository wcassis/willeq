#include "client/graphics/ui/item_icon_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/gpu_upload_thread.h"
#endif
#include <fstream>
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <cstring>
#include <algorithm>

namespace eqt {
namespace ui {

ItemIconLoader::ItemIconLoader() = default;

ItemIconLoader::~ItemIconLoader() {
    stopWorker();
    clear();
}

bool ItemIconLoader::init(irr::video::IVideoDriver* driver, const std::string& eqClientPath) {
    driver_ = driver;
    eqClientPath_ = eqClientPath;

    if (!driver_) {
        LOG_ERROR(MOD_UI, "No video driver provided");
        return false;
    }

    if (eqClientPath_.empty()) {
        LOG_ERROR(MOD_UI, "No EQ client path provided");
        return false;
    }

    LOG_DEBUG(MOD_UI, "Initialized with path: {}", eqClientPath_);
    return true;
}

void ItemIconLoader::startWorker() {
    if (queue_) return;  // already started
    queue_ = std::make_unique<EQT::Graphics::BackgroundWorkQueue<SheetRequest, SheetResult>>(
        [this](SheetRequest&& req) -> SheetResult { return processSheet(std::move(req)); });
    queue_->start();
    LOG_INFO(MOD_UI, "Icon sheet background worker started");
}

void ItemIconLoader::stopWorker() {
    if (!queue_) return;
    queue_->stop();
    queue_.reset();
    LOG_INFO(MOD_UI, "Icon sheet background worker stopped");
}

ItemIconLoader::SheetResult ItemIconLoader::processSheet(SheetRequest&& req) {
    // Try each path: readFileToBuffer + decodeTGA (all thread-safe, no Irrlicht calls)
    auto sheet = std::make_unique<SheetData>();
    bool found = false;

    for (const auto& path : req.paths) {
        std::vector<uint8_t> rawFileData;
        if (readFileToBuffer(path, rawFileData)) {
            if (decodeTGA(rawFileData, sheet->pixels, sheet->width, sheet->height)) {
                LOG_DEBUG(MOD_UI, "Worker: decoded {} sheet {} from {} ({}x{})",
                          req.isSpellSheet ? "spell" : "item", req.sheetNumber,
                          path, sheet->width, sheet->height);
                found = true;
                break;
            }
        }
    }

    if (!found) {
        LOG_ERROR(MOD_UI, "Worker: failed to load {} sheet {} from any path",
                  req.isSpellSheet ? "spell" : "item", req.sheetNumber);
    }

    int sheetKey = req.isSpellSheet ? -req.sheetNumber : req.sheetNumber;
    SheetResult result;
    result.sheetKey = sheetKey;
    result.data = found ? std::move(sheet) : nullptr;
    return result;
}

void ItemIconLoader::queueSheetRequest(int sheetNumber, bool isSpellSheet) {
    int sheetKey = isSpellSheet ? -sheetNumber : sheetNumber;

    // Dedup check (main-thread only set, no mutex needed)
    if (requestedSheetKeys_.count(sheetKey)) return;
    requestedSheetKeys_.insert(sheetKey);

    SheetRequest req;
    req.sheetNumber = sheetNumber;
    req.isSpellSheet = isSpellSheet;

    if (isSpellSheet) {
        req.paths = {
            eqClientPath_ + "/uifiles/default/spells0" + std::to_string(sheetNumber) + ".tga",
            eqClientPath_ + "/uifiles/default/spells" + std::to_string(sheetNumber) + ".tga",
            eqClientPath_ + "/uifiles/default_old/spells0" + std::to_string(sheetNumber) + ".tga",
            eqClientPath_ + "/uifiles/default_old/spells" + std::to_string(sheetNumber) + ".tga"
        };
    } else {
        req.paths = {
            eqClientPath_ + "/uifiles/default/dragitem" + std::to_string(sheetNumber) + ".tga"
        };
    }

    if (queue_) queue_->submit(std::move(req));

    LOG_DEBUG(MOD_UI, "Queued {} sheet {} for background load",
              isSpellSheet ? "spell" : "item", sheetNumber);
}

bool ItemIconLoader::pollCompletedSheet() {
    if (!queue_) return false;
    SheetResult result;
    if (!queue_->pollOne(result)) return false;
    if (result.data) {
        sheets_[result.sheetKey] = std::move(result.data);
    }
    return true;
}

bool ItemIconLoader::hasPendingSheets() const {
    if (!queue_) return false;
    return !queue_->isIdle() || queue_->getCompletedCount() > 0;
}

irr::video::ITexture* ItemIconLoader::getIcon(uint32_t iconId) {
    // Disabled: always return placeholder
    if (!enabled_) {
        return getPlaceholderIcon();
    }

    // Constrained mode: use constrained texture cache as single source of truth
    if (constrainedCache_) {
        std::string texName = "icon_" + std::to_string(iconId);
        irr::video::ITexture* cached = constrainedCache_->getTexture(texName);
        if (cached) {
            return cached;
        }

        // Cache miss — queue for lazy extraction and return placeholder
        if (lazyIconQueued_.find(iconId) == lazyIconQueued_.end()) {
            lazyIconQueue_.push_back(iconId);
            lazyIconQueued_.insert(iconId);

            // Ensure the right sheet is queued for background loading
            int sk = sheetKeyForIcon(iconId);
            if (sk < 0) {
                int sheetNumber = -sk;
                if (sheets_.find(-sheetNumber) == sheets_.end()) {
                    queueSheetRequest(sheetNumber, true);
                }
            } else {
                if (sheets_.find(sk) == sheets_.end()) {
                    queueSheetRequest(sk, false);
                }
            }
        }
        return getPlaceholderIcon();
    }

    // Non-constrained mode: use original iconCache_ path
    auto it = iconCache_.find(iconId);
    if (it != iconCache_.end()) {
        if (it->second == nullptr) {
            LOG_DEBUG(MOD_UI, "getIcon({}) - returning cached nullptr", iconId);
        }
        return it->second;
    }

    // EQ item icons start at 500 (icons below 500 are spell icons)
    // dragitem1.tga contains icons 500-535, dragitem2.tga contains 536-571, etc.
    // spellbook01.tga contains spell icons 0-35, spellbook02.tga contains 36-71, etc.

    if (iconId < ICON_ID_BASE) {
        // Spell icon - load from spellbook*.tga
        return getSpellIcon(iconId);
    }

    // Calculate which sheet and local index (subtract 500 base)
    int adjustedId = iconId - ICON_ID_BASE;
    int sheetNumber = (adjustedId / ICONS_PER_SHEET) + 1;  // Sheets are 1-indexed (dragitem1.tga, etc.)
    int localIndex = adjustedId % ICONS_PER_SHEET;

    int row = localIndex / ICONS_PER_ROW;
    int col = localIndex % ICONS_PER_ROW;
    LOG_DEBUG(MOD_UI, "ItemIconLoader Icon {} -> adjusted={} sheet={} index={} (row={} col={}) pos=({},{})",
              iconId, adjustedId, sheetNumber, localIndex, row, col, row * ICON_SIZE, col * ICON_SIZE);

    // If sheet not loaded, queue it for background loading and return nullptr.
    if (sheets_.find(sheetNumber) == sheets_.end()) {
        queueSheetRequest(sheetNumber, false);
        return nullptr;  // Not ready yet — don't cache nullptr (will retry)
    }

    // Extract and cache the icon
    irr::video::ITexture* tex = extractIcon(iconId, sheetNumber, localIndex);
    iconCache_[iconId] = tex;
    return tex;
}

irr::video::ITexture* ItemIconLoader::getSpellIcon(uint32_t iconId) {
    // Check cache first
    auto it = iconCache_.find(iconId);
    if (it != iconCache_.end()) {
        return it->second;
    }

    // Spell icons use spellbook*.tga sheets
    // spellbook01.tga: icons 0-35, spellbook02.tga: icons 36-71, etc.
    int sheetNumber = (iconId / ICONS_PER_SHEET) + 1;
    int localIndex = iconId % ICONS_PER_SHEET;

    // Use negative sheet numbers to differentiate spell sheets from item sheets
    int spellSheetKey = -sheetNumber;

    // If sheet not loaded, queue it for background loading and return nullptr.
    // The caller (SpellGemPanel) will re-render when the sheet becomes available.
    if (sheets_.find(spellSheetKey) == sheets_.end()) {
        queueSheetRequest(sheetNumber, true);
        return nullptr;  // Not ready yet — don't cache nullptr (will retry)
    }

    int row = localIndex / ICONS_PER_ROW;
    int col = localIndex % ICONS_PER_ROW;
    LOG_DEBUG(MOD_UI, "SpellIcon {} -> sheet={} index={} (row={} col={})",
              iconId, sheetNumber, localIndex, row, col);

    // Extract and cache the icon
    irr::video::ITexture* tex = extractIcon(iconId, spellSheetKey, localIndex);
    if (!tex) {
        LOG_WARN(MOD_UI, "SpellIcon {} - extractIcon returned nullptr (sheetKey={}, localIndex={})",
                 iconId, spellSheetKey, localIndex);
    }
    iconCache_[iconId] = tex;
    return tex;
}

bool ItemIconLoader::loadSpellSheet(int sheetNumber) {
    // Try multiple paths for spell icon sheets
    // Spell icons are in spells*.tga files (not spellbook*.tga which is UI)
    std::vector<std::string> paths = {
        eqClientPath_ + "/uifiles/default/spells0" + std::to_string(sheetNumber) + ".tga",
        eqClientPath_ + "/uifiles/default/spells" + std::to_string(sheetNumber) + ".tga",
        eqClientPath_ + "/uifiles/default_old/spells0" + std::to_string(sheetNumber) + ".tga",
        eqClientPath_ + "/uifiles/default_old/spells" + std::to_string(sheetNumber) + ".tga"
    };

    auto sheet = std::make_unique<SheetData>();

    for (const auto& path : paths) {
        if (loadTGA(path, sheet->pixels, sheet->width, sheet->height)) {
            LOG_DEBUG(MOD_UI, "Loaded spell sheet {} from {} ({}x{})",
                      sheetNumber, path, sheet->width, sheet->height);
            // Use negative key to differentiate from item sheets
            sheets_[-sheetNumber] = std::move(sheet);
            return true;
        }
    }

    LOG_ERROR(MOD_UI, "Failed to load spell sheet {}", sheetNumber);
    return false;
}

bool ItemIconLoader::loadSheet(int sheetNumber) {
    std::ostringstream path;
    path << eqClientPath_ << "/uifiles/default/dragitem" << sheetNumber << ".tga";

    auto sheet = std::make_unique<SheetData>();
    if (!loadTGA(path.str(), sheet->pixels, sheet->width, sheet->height)) {
        LOG_ERROR(MOD_UI, "ItemIconLoader Failed to load sheet: {}", path.str());
        return false;
    }

    LOG_DEBUG(MOD_UI, "ItemIconLoader Loaded sheet {} ({}x{})", sheetNumber, sheet->width, sheet->height);
    sheets_[sheetNumber] = std::move(sheet);
    return true;
}

bool ItemIconLoader::readFileToBuffer(const std::string& path, std::vector<uint8_t>& buffer) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto fileSize = file.tellg();
    if (fileSize <= 0) return false;

    buffer.resize(static_cast<size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return file.good();
}

bool ItemIconLoader::decodeTGA(const std::vector<uint8_t>& buffer, std::vector<uint8_t>& pixels,
                                int& width, int& height) {
    if (buffer.size() < 18) return false;

    const uint8_t* data = buffer.data();
    size_t pos = 0;

    // TGA header
    uint8_t idLength = data[0];
    uint8_t imageType = data[2];
    width = data[12] | (data[13] << 8);
    height = data[14] | (data[15] << 8);
    uint8_t bitsPerPixel = data[16];
    uint8_t descriptor = data[17];
    pos = 18;

    // Skip ID field
    pos += idLength;

    if (width <= 0 || height <= 0) {
        LOG_ERROR(MOD_UI, "Invalid TGA dimensions: {}x{}", width, height);
        return false;
    }

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        LOG_ERROR(MOD_UI, "Unsupported TGA bit depth: {}", static_cast<int>(bitsPerPixel));
        return false;
    }

    int bytesPerPixel = bitsPerPixel / 8;
    bool isRLE = (imageType == 10);
    bool topOrigin = (descriptor & 0x20) != 0;

    pixels.resize(width * height * 4);

    if (isRLE) {
        int pixelCount = width * height;
        int currentPixel = 0;

        while (currentPixel < pixelCount && pos < buffer.size()) {
            uint8_t packetHeader = data[pos++];
            int count = (packetHeader & 0x7F) + 1;
            bool isRunLength = (packetHeader & 0x80) != 0;

            if (isRunLength) {
                if (pos + bytesPerPixel > buffer.size()) break;
                uint8_t pixel[4] = {0, 0, 0, 255};
                std::memcpy(pixel, data + pos, bytesPerPixel);
                pos += bytesPerPixel;

                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    int idx = currentPixel * 4;
                    pixels[idx + 0] = pixel[2];  // R (TGA is BGR)
                    pixels[idx + 1] = pixel[1];  // G
                    pixels[idx + 2] = pixel[0];  // B
                    pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;
                }
            } else {
                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    if (pos + bytesPerPixel > buffer.size()) break;
                    int idx = currentPixel * 4;
                    pixels[idx + 0] = data[pos + 2];  // R
                    pixels[idx + 1] = data[pos + 1];  // G
                    pixels[idx + 2] = data[pos + 0];  // B
                    pixels[idx + 3] = (bytesPerPixel == 4) ? data[pos + 3] : 255;
                    pos += bytesPerPixel;
                }
            }
        }
    } else {
        int pixelCount = width * height;
        for (int i = 0; i < pixelCount && pos + bytesPerPixel <= buffer.size(); i++) {
            int idx = i * 4;
            pixels[idx + 0] = data[pos + 2];  // R (TGA is BGR)
            pixels[idx + 1] = data[pos + 1];  // G
            pixels[idx + 2] = data[pos + 0];  // B
            pixels[idx + 3] = (bytesPerPixel == 4) ? data[pos + 3] : 255;
            pos += bytesPerPixel;
        }
    }

    // Flip if bottom-origin
    if (!topOrigin) {
        std::vector<uint8_t> flipped(pixels.size());
        for (int y = 0; y < height; y++) {
            int srcRow = height - 1 - y;
            std::memcpy(flipped.data() + y * width * 4,
                       pixels.data() + srcRow * width * 4,
                       width * 4);
        }
        pixels = std::move(flipped);
    }

    return true;
}

irr::video::ITexture* ItemIconLoader::extractIcon(uint32_t iconId, int sheetIndex, int localIndex) {
    auto sheetIt = sheets_.find(sheetIndex);
    if (sheetIt == sheets_.end()) {
        LOG_WARN(MOD_UI, "extractIcon({}) - sheet {} not found in sheets_ map (size={})",
                 iconId, sheetIndex, sheets_.size());
        return nullptr;
    }

    const SheetData* sheet = sheetIt->second.get();
    if (!sheet || sheet->pixels.empty()) {
        LOG_WARN(MOD_UI, "extractIcon({}) - sheet {} is null or empty", iconId, sheetIndex);
        return nullptr;
    }

    // Calculate position in sheet (6x6 grid of 40x40 icons)
    int row = localIndex / ICONS_PER_ROW;
    int col = localIndex % ICONS_PER_ROW;

    int startX, startY;
    if (sheetIndex < 0) {
        // Spell sheets (negative index) use standard row-major layout (X=col, Y=row)
        startX = col * ICON_SIZE;
        startY = row * ICON_SIZE;
    } else {
        // Item sheets (dragitem) use column-major layout (X=row, Y=col per eqsage)
        startX = row * ICON_SIZE;
        startY = col * ICON_SIZE;
    }

    // Extract 40x40 pixel region
    std::vector<uint8_t> iconPixels(ICON_SIZE * ICON_SIZE * 4);

    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int srcX = startX + x;
            int srcY = startY + y;

            // Bounds check
            if (srcX >= sheet->width || srcY >= sheet->height) {
                continue;
            }

            int srcIdx = (srcY * sheet->width + srcX) * 4;
            int dstIdx = (y * ICON_SIZE + x) * 4;

            // Copy as BGRA for Irrlicht's ECF_A8R8G8B8 format
            iconPixels[dstIdx + 0] = sheet->pixels[srcIdx + 2];  // B
            iconPixels[dstIdx + 1] = sheet->pixels[srcIdx + 1];  // G
            iconPixels[dstIdx + 2] = sheet->pixels[srcIdx + 0];  // R
            iconPixels[dstIdx + 3] = sheet->pixels[srcIdx + 3];  // A
        }
    }

    // Create texture name
    std::string texName = "icon_" + std::to_string(iconId);

#ifdef EQT_HAS_GLES2
    // Async path: submit RGBA data to GPU upload thread
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        // Convert BGRA (Irrlicht) → RGBA (GL) for glTexImage2D
        std::vector<uint8_t> rgba(ICON_SIZE * ICON_SIZE * 4);
        for (int i = 0; i < ICON_SIZE * ICON_SIZE; ++i) {
            int idx = i * 4;
            rgba[idx + 0] = iconPixels[idx + 2];  // R (was at B position in BGRA)
            rgba[idx + 1] = iconPixels[idx + 1];  // G
            rgba[idx + 2] = iconPixels[idx + 0];  // B (was at R position in BGRA)
            rgba[idx + 3] = iconPixels[idx + 3];  // A
        }

        EQT::Graphics::UploadRequest req;
        req.type = EQT::Graphics::UploadRequestType::Texture;
        req.width = ICON_SIZE;
        req.height = ICON_SIZE;
        req.pixelData = std::move(rgba);
        req.textureName = texName;
        // High byte 4 = icon, low bits = icon ID
        req.callbackKey = (uint64_t(4) << 56) | static_cast<uint64_t>(iconId);

        gpuUploadThread_->submit(std::move(req));
        pendingAsyncIcons_.insert(iconId);
        return nullptr;  // Texture will be registered when upload completes
    }
#endif

    // Create Irrlicht image from pixel data
    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(ICON_SIZE, ICON_SIZE),
        iconPixels.data(),
        false  // Don't take ownership of the data
    );

    if (!image) {
        LOG_ERROR(MOD_UI, "ItemIconLoader Failed to create image for icon {}", iconId);
        return nullptr;
    }

    // Create texture from image
    irr::video::ITexture* texture = driver_->addTexture(texName.c_str(), image);
    image->drop();

    if (!texture) {
        LOG_ERROR(MOD_UI, "ItemIconLoader Failed to create texture for icon {}", iconId);
        return nullptr;
    }

    return texture;
}

bool ItemIconLoader::loadTGA(const std::string& path, std::vector<uint8_t>& pixels, int& width, int& height) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // TGA header
    uint8_t header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file.good()) {
        return false;
    }

    // Skip ID field
    uint8_t idLength = header[0];
    if (idLength > 0) {
        file.seekg(idLength, std::ios::cur);
    }

    // Parse header
    uint8_t imageType = header[2];
    width = header[12] | (header[13] << 8);
    height = header[14] | (header[15] << 8);
    uint8_t bitsPerPixel = header[16];
    uint8_t descriptor = header[17];

    if (width <= 0 || height <= 0) {
        LOG_ERROR(MOD_UI, "ItemIconLoader Invalid TGA dimensions: {}x{}", width, height);
        return false;
    }

    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        LOG_ERROR(MOD_UI, "ItemIconLoader Unsupported TGA bit depth: {}", (int)bitsPerPixel);
        return false;
    }

    int bytesPerPixel = bitsPerPixel / 8;
    bool isRLE = (imageType == 10);
    bool topOrigin = (descriptor & 0x20) != 0;

    pixels.resize(width * height * 4);  // Always output as RGBA

    if (isRLE) {
        // RLE compressed
        int pixelCount = width * height;
        int currentPixel = 0;

        while (currentPixel < pixelCount && file.good()) {
            uint8_t packetHeader;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);

            int count = (packetHeader & 0x7F) + 1;
            bool isRunLength = (packetHeader & 0x80) != 0;

            if (isRunLength) {
                // Run-length packet
                uint8_t pixel[4] = {0, 0, 0, 255};
                file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    int idx = currentPixel * 4;
                    pixels[idx + 0] = pixel[2];  // R (TGA is BGR)
                    pixels[idx + 1] = pixel[1];  // G
                    pixels[idx + 2] = pixel[0];  // B
                    pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;  // A
                }
            } else {
                // Raw packet
                for (int i = 0; i < count && currentPixel < pixelCount; i++, currentPixel++) {
                    uint8_t pixel[4] = {0, 0, 0, 255};
                    file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

                    int idx = currentPixel * 4;
                    pixels[idx + 0] = pixel[2];  // R
                    pixels[idx + 1] = pixel[1];  // G
                    pixels[idx + 2] = pixel[0];  // B
                    pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;  // A
                }
            }
        }
    } else {
        // Uncompressed
        int pixelCount = width * height;
        for (int i = 0; i < pixelCount && file.good(); i++) {
            uint8_t pixel[4] = {0, 0, 0, 255};
            file.read(reinterpret_cast<char*>(pixel), bytesPerPixel);

            int idx = i * 4;
            pixels[idx + 0] = pixel[2];  // R (TGA is BGR)
            pixels[idx + 1] = pixel[1];  // G
            pixels[idx + 2] = pixel[0];  // B
            pixels[idx + 3] = (bytesPerPixel == 4) ? pixel[3] : 255;  // A
        }
    }

    // Flip if bottom-origin (most TGA files are bottom-origin)
    if (!topOrigin) {
        std::vector<uint8_t> flipped(pixels.size());
        for (int y = 0; y < height; y++) {
            int srcRow = height - 1 - y;
            std::memcpy(flipped.data() + y * width * 4,
                       pixels.data() + srcRow * width * 4,
                       width * 4);
        }
        pixels = std::move(flipped);
    }

    return true;
}

irr::video::ITexture* ItemIconLoader::getPlaceholderIcon() {
    if (placeholderIcon_) return placeholderIcon_;
    if (!driver_) return nullptr;

    // Create a 40x40 dark gray texture as placeholder
    const int size = ICON_SIZE;
    std::vector<uint32_t> pixels(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Dark gray fill with slightly lighter 1px border
            bool border = (x == 0 || y == 0 || x == size - 1 || y == size - 1);
            pixels[y * size + x] = border ? 0xFF404040 : 0xFF282828;  // ARGB
        }
    }

    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(size, size),
        pixels.data(), false);

    if (image) {
        placeholderIcon_ = driver_->addTexture("_icon_placeholder_", image);
        image->drop();
    }

    return placeholderIcon_;
}

int ItemIconLoader::sheetKeyForIcon(uint32_t iconId) {
    if (iconId < static_cast<uint32_t>(ICON_ID_BASE)) {
        // Spell icon: negative sheet key
        return -(static_cast<int>(iconId / ICONS_PER_SHEET) + 1);
    }
    // Item icon: positive sheet key
    return static_cast<int>((iconId - ICON_ID_BASE) / ICONS_PER_SHEET) + 1;
}

void ItemIconLoader::sortPendingBySheet() {
    if (lazyIconQueue_.size() <= 1) return;

    std::stable_sort(lazyIconQueue_.begin(), lazyIconQueue_.end(),
        [](uint32_t a, uint32_t b) {
            return sheetKeyForIcon(a) < sheetKeyForIcon(b);
        });
}

bool ItemIconLoader::processOneLazyIcon() {
    if (lazyIconQueue_.empty()) return false;

    // Step 1: Poll one completed sheet from worker (if any ready)
    bool polledSheet = pollCompletedSheet();
    if (polledSheet) return true;

    // Step 2: Try to extract one pending icon from an already-loaded sheet
    for (auto it = lazyIconQueue_.begin(); it != lazyIconQueue_.end(); ++it) {
        uint32_t iconId = *it;
        int sk = sheetKeyForIcon(iconId);

        // Check if the required sheet is loaded
        int sheetKey;
        int localIndex;
        if (iconId < static_cast<uint32_t>(ICON_ID_BASE)) {
            int sheetNumber = (iconId / ICONS_PER_SHEET) + 1;
            sheetKey = -sheetNumber;
            localIndex = iconId % ICONS_PER_SHEET;
        } else {
            int adjustedId = iconId - ICON_ID_BASE;
            sheetKey = (adjustedId / ICONS_PER_SHEET) + 1;
            localIndex = adjustedId % ICONS_PER_SHEET;
        }
        (void)sk;  // sk == sheetKey (already computed by sheetKeyForIcon)

        if (sheets_.find(sheetKey) == sheets_.end()) {
            continue;  // Sheet not ready yet
        }

        // Extract the icon (may return nullptr if async upload submitted)
        irr::video::ITexture* tex = extractIcon(iconId, sheetKey, localIndex);

        // Register with constrained cache (sync path only — async registers in processCompletedUploads)
        if (tex && constrainedCache_) {
            std::string texName = "icon_" + std::to_string(iconId);
            // 40x40 ARGB = 6400 bytes
            size_t iconBytes = static_cast<size_t>(ICON_SIZE) * ICON_SIZE * 4;
            constrainedCache_->registerTexture(texName, tex, iconBytes, true);
        }

        // Remove from queue (async icons track via pendingAsyncIcons_ instead)
        lazyIconQueue_.erase(it);
        lazyIconQueued_.erase(iconId);
        return true;
    }

    // Step 3: Pending icons but no sheet ready — can't progress
    return false;
}

void ItemIconLoader::registerAsyncIcon(uint32_t iconId, irr::video::ITexture* tex) {
    if (tex) {
        iconCache_[iconId] = tex;
    }
}

void ItemIconLoader::clear() {
    stopWorker();
    // Textures are managed by Irrlicht's texture cache
    // Don't need to manually drop them
    iconCache_.clear();
    sheets_.clear();
    requestedSheetKeys_.clear();
    lazyIconQueue_.clear();
    lazyIconQueued_.clear();
    pendingAsyncIcons_.clear();
}

} // namespace ui
} // namespace eqt
