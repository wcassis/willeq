#pragma once

#include <irrlicht.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <set>

namespace EQT {
namespace Graphics {
class ConstrainedTextureCache;
class GPUUploadThread;
}
}

namespace eqt {
namespace ui {

// Loads item icons from EQ client dragitem*.tga files
// Each TGA file is 256x256 containing a 6x6 grid of 40x40 pixel icons
//
// Sheet loading (disk I/O + TGA decode) runs on a background worker thread.
// The main thread only polls completed SheetData results (<0.1ms per poll).
// Icon extraction (extractIcon → driver_->addTexture()) is lazy, triggered
// by getIcon()/getSpellIcon() on demand on the main thread.
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

    // Background worker lifecycle
    void startWorker();
    void stopWorker();

    // Poll one completed sheet result from worker thread. Moves SheetData into sheets_.
    // Returns true if a result was consumed (caller may need to re-render).
    bool pollCompletedSheet();

    // Check if there are pending sheets (queued requests or completed results to poll)
    bool hasPendingSheets() const;

    // Constrained mode: share the constrained texture cache for LRU eviction
    void setConstrainedTextureCache(EQT::Graphics::ConstrainedTextureCache* cache) { constrainedCache_ = cache; }

    // Set GPU upload thread for async icon uploads (GLES2 only)
    void setGPUUploadThread(EQT::Graphics::GPUUploadThread* thread) { gpuUploadThread_ = thread; }

    // Register a completed async icon upload into the icon cache
    void registerAsyncIcon(uint32_t iconId, irr::video::ITexture* tex);

    // Remove an icon from the pending async set (called when upload completes)
    void clearPendingAsyncIcon(uint32_t iconId) { pendingAsyncIcons_.erase(iconId); }

    // Enable/disable icon loading (disabled = always return placeholder)
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // Get placeholder icon (created on first call, reused thereafter)
    irr::video::ITexture* getPlaceholderIcon();

    // Process one lazy icon extraction per frame (GREEN-gated by caller).
    // Returns true if work was done (sheet polled or icon extracted).
    bool processOneLazyIcon();

    // Sort pending lazy icon queue by sheet key for cache locality.
    // Groups icons from the same sheet together so a single sheet load
    // serves all pending icons from that sheet before the next sheet is loaded.
    void sortPendingBySheet();

    // Constants
    static constexpr int ICON_SIZE = 40;           // Each icon is 40x40 pixels
    static constexpr int ICONS_PER_ROW = 6;        // 6 icons per row in sheet
    static constexpr int ICONS_PER_SHEET = 36;     // 6x6 = 36 icons per sheet
    static constexpr int SHEET_SIZE = 256;          // Each sheet is 256x256
    static constexpr int SHEET_MARGIN = 8;          // Margin around icon grid (256-240)/2
    static constexpr int ICON_ID_BASE = 500;        // EQ item icons start at 500

private:
    // Load a specific dragitem sheet (synchronous, used by non-progressive path)
    bool loadSheet(int sheetNumber);

    // Load a specific spellbook sheet (synchronous, used by non-progressive path)
    bool loadSpellSheet(int sheetNumber);

    // Get a spell icon (gem_icon values < 500)
    irr::video::ITexture* getSpellIcon(uint32_t iconId);

    // Extract an individual icon from a sheet
    irr::video::ITexture* extractIcon(uint32_t iconId, int sheetIndex, int localIndex);

    // Read entire file into memory buffer (thread-safe, no Irrlicht calls)
    bool readFileToBuffer(const std::string& path, std::vector<uint8_t>& buffer);

    // Decode TGA from memory buffer (thread-safe, no Irrlicht calls)
    bool decodeTGA(const std::vector<uint8_t>& buffer, std::vector<uint8_t>& pixels, int& width, int& height);

    // Parse TGA file (handles both RLE and uncompressed) — combined read+decode for legacy use
    bool loadTGA(const std::string& path, std::vector<uint8_t>& pixels, int& width, int& height);

    // Queue a sheet for background loading (called from getIcon/getSpellIcon on main thread)
    void queueSheetRequest(int sheetNumber, bool isSpellSheet);

    // Background worker thread loop
    void workerLoop();

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

    // Background worker thread
    std::thread worker_;
    std::atomic<bool> workerRunning_{false};

    // Request queue (main thread → worker)
    struct SheetRequest {
        int sheetNumber = 0;
        bool isSpellSheet = false;
        std::vector<std::string> paths;  // File paths to try
    };
    mutable std::mutex requestMutex_;
    std::condition_variable requestCV_;
    std::deque<SheetRequest> requestQueue_;

    // Result queue (worker → main thread)
    struct SheetResult {
        int sheetKey = 0;  // positive=item, negative=spell
        std::unique_ptr<SheetData> data;  // null if load failed
    };
    mutable std::mutex resultMutex_;
    std::deque<SheetResult> resultQueue_;

    // Track requested sheet keys to prevent duplicate queue entries (main-thread only)
    std::set<int> requestedSheetKeys_;

    // Constrained mode state
    EQT::Graphics::ConstrainedTextureCache* constrainedCache_ = nullptr;
    irr::video::ITexture* placeholderIcon_ = nullptr;
    bool enabled_ = true;

    // Lazy icon extraction queue (main-thread only)
    std::deque<uint32_t> lazyIconQueue_;
    std::set<uint32_t> lazyIconQueued_;

    // GPU upload thread for async icon uploads (non-owning)
    EQT::Graphics::GPUUploadThread* gpuUploadThread_ = nullptr;
    // Icon IDs currently being uploaded asynchronously (prevents duplicate submissions)
    std::set<uint32_t> pendingAsyncIcons_;

    // Compute sheet key for an icon ID (for sorting by sheet locality)
    static int sheetKeyForIcon(uint32_t iconId);
};

} // namespace ui
} // namespace eqt
