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
    // Returns nullptr if icon not found or sheet not yet loaded (progressive mode)
    irr::video::ITexture* getIcon(uint32_t iconId);

    // Get the number of loaded sheets
    size_t getSheetCount() const { return sheets_.size(); }

    // Clear all loaded icons/sheets
    void clear();

    // Progressive loading: perform one step of pending sheet work per call.
    // Two-phase pipeline: phase 1 reads raw file from disk, phase 2 decodes to pixels.
    // Returns true if work was done (caller may need to re-render after phase 2).
    bool loadOnePendingSheet();

    // Check if there are pending sheets waiting to load (or mid-decode)
    bool hasPendingSheets() const {
        return pendingSheetWork_ != nullptr || !pendingSpellSheets_.empty() || !pendingItemSheets_.empty();
    }

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

    // Read entire file into memory buffer (phase 1 — disk I/O only)
    bool readFileToBuffer(const std::string& path, std::vector<uint8_t>& buffer);

    // Decode TGA from memory buffer (phase 2 — CPU decode, no disk I/O)
    bool decodeTGA(const std::vector<uint8_t>& buffer, std::vector<uint8_t>& pixels, int& width, int& height);

    // Parse TGA file (handles both RLE and uncompressed) — combined read+decode for legacy use
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

    // Pending sheets to load progressively (one per frame)
    std::vector<int> pendingSpellSheets_;
    std::vector<int> pendingItemSheets_;

    // Two-phase pending sheet work: phase 1 reads file, phase 2 decodes
    struct PendingSheetWork {
        int sheetNumber = 0;
        bool isSpellSheet = false;
        std::vector<uint8_t> rawFileData;  // Raw TGA bytes from phase 1
    };
    std::unique_ptr<PendingSheetWork> pendingSheetWork_;
};

} // namespace ui
} // namespace eqt
