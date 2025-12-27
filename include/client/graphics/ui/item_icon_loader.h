#pragma once

#include <irrlicht.h>
#include <string>
#include <map>
#include <vector>
#include <memory>

namespace eqt {
namespace ui {

// Loads item icons from EQ client dragitem*.tga files
// Each TGA file is 256x256 containing a 6x6 grid of 40x40 pixel icons
class ItemIconLoader {
public:
    ItemIconLoader();
    ~ItemIconLoader();

    // Initialize with Irrlicht driver and path to EQ client
    bool init(irr::video::IVideoDriver* driver, const std::string& eqClientPath);

    // Get texture for a specific icon ID
    // Returns nullptr if icon not found
    irr::video::ITexture* getIcon(uint32_t iconId);

    // Get the number of loaded sheets
    size_t getSheetCount() const { return sheets_.size(); }

    // Clear all loaded icons/sheets
    void clear();

    // Constants
    static constexpr int ICON_SIZE = 40;           // Each icon is 40x40 pixels
    static constexpr int ICONS_PER_ROW = 6;        // 6 icons per row in sheet
    static constexpr int ICONS_PER_SHEET = 36;     // 6x6 = 36 icons per sheet
    static constexpr int SHEET_SIZE = 256;         // Each sheet is 256x256
    static constexpr int SHEET_MARGIN = 8;         // Margin around icon grid (256-240)/2
    static constexpr int ICON_ID_BASE = 500;       // EQ item icons start at 500

private:
    // Load a specific dragitem sheet
    bool loadSheet(int sheetNumber);

    // Load a specific spellbook sheet (for spell icons)
    bool loadSpellSheet(int sheetNumber);

    // Get a spell icon (gem_icon values < 500)
    irr::video::ITexture* getSpellIcon(uint32_t iconId);

    // Extract an individual icon from a sheet
    irr::video::ITexture* extractIcon(uint32_t iconId, int sheetIndex, int localIndex);

    // Parse TGA file (handles both RLE and uncompressed)
    bool loadTGA(const std::string& path, std::vector<uint8_t>& pixels, int& width, int& height);

    irr::video::IVideoDriver* driver_ = nullptr;
    std::string eqClientPath_;

    // Cached sheet images (raw pixel data)
    struct SheetData {
        std::vector<uint8_t> pixels;  // RGBA pixel data
        int width = 0;
        int height = 0;
    };
    std::map<int, std::unique_ptr<SheetData>> sheets_;

    // Cached individual icon textures
    std::map<uint32_t, irr::video::ITexture*> iconCache_;
};

} // namespace ui
} // namespace eqt
