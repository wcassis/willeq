#include "client/graphics/ui/item_icon_loader.h"
#include <fstream>
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <cstring>

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
    if (workerRunning_.load()) return;
    workerRunning_.store(true);
    worker_ = std::thread(&ItemIconLoader::workerLoop, this);
    LOG_INFO(MOD_UI, "Icon sheet background worker started");
}

void ItemIconLoader::stopWorker() {
    if (!workerRunning_.load()) return;
    workerRunning_.store(false);
    requestCV_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    LOG_INFO(MOD_UI, "Icon sheet background worker stopped");
}

void ItemIconLoader::workerLoop() {
    while (workerRunning_.load()) {
        SheetRequest req;
        {
            std::unique_lock<std::mutex> lock(requestMutex_);
            requestCV_.wait(lock, [this] {
                return !requestQueue_.empty() || !workerRunning_.load();
            });
            if (!workerRunning_.load()) break;
            if (requestQueue_.empty()) continue;
            req = std::move(requestQueue_.front());
            requestQueue_.pop_front();
        }

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

        // Push result (even on failure, so hasPendingSheets() stays accurate)
        int sheetKey = req.isSpellSheet ? -req.sheetNumber : req.sheetNumber;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            SheetResult result;
            result.sheetKey = sheetKey;
            result.data = found ? std::move(sheet) : nullptr;
            resultQueue_.push_back(std::move(result));
        }
    }
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

    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestQueue_.push_back(std::move(req));
    }
    requestCV_.notify_one();

    LOG_DEBUG(MOD_UI, "Queued {} sheet {} for background load",
              isSpellSheet ? "spell" : "item", sheetNumber);
}

bool ItemIconLoader::pollCompletedSheet() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    if (resultQueue_.empty()) return false;
    auto result = std::move(resultQueue_.front());
    resultQueue_.pop_front();
    if (result.data) {
        sheets_[result.sheetKey] = std::move(result.data);
    }
    return true;
}

bool ItemIconLoader::hasPendingSheets() const {
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (!resultQueue_.empty()) return true;
    }
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        return !requestQueue_.empty();
    }
}

irr::video::ITexture* ItemIconLoader::getIcon(uint32_t iconId) {
    // Check cache first
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
    std::ostringstream texName;
    texName << "icon_" << iconId;

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
    irr::video::ITexture* texture = driver_->addTexture(texName.str().c_str(), image);
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

void ItemIconLoader::clear() {
    stopWorker();
    // Textures are managed by Irrlicht's texture cache
    // Don't need to manually drop them
    iconCache_.clear();
    sheets_.clear();
    requestedSheetKeys_.clear();
}

} // namespace ui
} // namespace eqt
