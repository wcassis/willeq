#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/eq/dds_decoder.h"
#include "common/logging.h"
#include <algorithm>
#include <chrono>

namespace EQT {
namespace Graphics {

EntityPrepWorker::EntityPrepWorker(RaceModelLoader* modelLoader, EquipmentModelLoader* equipLoader)
    : modelLoader_(modelLoader), equipLoader_(equipLoader) {
}

EntityPrepWorker::~EntityPrepWorker() {
    stop();
}

void EntityPrepWorker::start() {
    if (queue_) return;  // already started
    queue_ = std::make_unique<BackgroundWorkQueue<PrepRequest, PrepResult>>(
        [this](PrepRequest&& req) -> PrepResult { return processRequest(std::move(req)); });
    queue_->start();
    LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: background thread started (equipLoader={})",
             equipLoader_ ? "yes" : "no");
}

void EntityPrepWorker::stop() {
    if (!queue_) return;
    queue_->stop();
    queue_.reset();
    dispatchedSpawnId_ = 0;
    dispatchedKey_ = 0;
    LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: background thread stopped");
}

void EntityPrepWorker::requestPrep(const PrepRequest& req) {
    // Main-thread-only: no mutex needed for pendingQueue_
    for (const auto& existing : pendingQueue_) {
        if (existing.spawnId == req.spawnId) {
            return;  // Already queued
        }
    }
    pendingQueue_.push_back(req);
    pendingSpawnIds_.insert(req.spawnId);
    LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: queued prep for spawn={} race={} gender={}",
              req.spawnId, req.raceId, req.gender);
}

void EntityPrepWorker::dispatchOne() {
    // Main-thread-only: called when governor is GREEN
    if (pendingQueue_.empty()) return;
    if (queue_ && !queue_->isIdle()) return;

    // Sort pending queue by priority (PVS depth, then model key) before dispatching
    sortPendingByPriority();

    PrepRequest req = pendingQueue_.front();
    pendingQueue_.pop_front();
    pendingSpawnIds_.erase(req.spawnId);

    dispatchedSpawnId_ = req.spawnId;
    dispatchedKey_ = (static_cast<uint32_t>(req.raceId) << 8) | req.gender;

    if (queue_) queue_->submit(std::move(req));

    LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: dispatched spawn={} race={} gender={} ({} pending)",
              dispatchedSpawnId_, dispatchedKey_ >> 8, dispatchedKey_ & 0xFF, pendingQueue_.size());
}

bool EntityPrepWorker::pollResult(PrepResult& out) {
    if (!queue_) return false;
    if (!queue_->pollOne(out)) return false;
    dispatchedSpawnId_ = 0;
    dispatchedKey_ = 0;
    return true;
}

bool EntityPrepWorker::isIdle() const {
    return !queue_ || queue_->isIdle();
}

void EntityPrepWorker::sortPendingByPriority() {
    // Main-thread-only: stable sort by PVS depth first, then model key for cache locality
    std::stable_sort(pendingQueue_.begin(), pendingQueue_.end(),
        [](const PrepRequest& a, const PrepRequest& b) {
            if (a.pvsDepth != b.pvsDepth) return a.pvsDepth < b.pvsDepth;
            uint32_t keyA = (static_cast<uint32_t>(a.raceId) << 8) | a.gender;
            uint32_t keyB = (static_cast<uint32_t>(b.raceId) << 8) | b.gender;
            return keyA < keyB;
        });
}

void EntityPrepWorker::updateDepths(std::function<uint8_t(size_t)> depthLookup) {
    for (auto& req : pendingQueue_) {
        req.pvsDepth = (req.bspRegion != SIZE_MAX) ? depthLookup(req.bspRegion) : 255;
    }
    sortPendingByPriority();
}

void EntityPrepWorker::cancelPrep(uint16_t spawnId) {
    // Main-thread-only: remove from pending queue
    auto it = std::remove_if(pendingQueue_.begin(), pendingQueue_.end(),
        [spawnId](const PrepRequest& req) { return req.spawnId == spawnId; });
    if (it != pendingQueue_.end()) {
        pendingQueue_.erase(it, pendingQueue_.end());
        pendingSpawnIds_.erase(spawnId);
    }
}

bool EntityPrepWorker::isPendingForEntity(uint16_t spawnId) const {
    // Check in-flight work (main-thread-only tracking)
    if (dispatchedSpawnId_ == spawnId) return true;
    // Check pending queue (main-thread-only, no lock needed)
    return pendingSpawnIds_.count(spawnId) > 0;
}

bool EntityPrepWorker::isPending(uint16_t raceId, uint8_t gender) const {
    uint32_t key = (static_cast<uint32_t>(raceId) << 8) | gender;
    // Check in-flight work (main-thread-only tracking)
    if (dispatchedKey_ == key) return true;
    // Check pending queue (main-thread-only)
    for (const auto& req : pendingQueue_) {
        if (req.raceId == raceId && req.gender == gender) return true;
    }
    return false;
}

size_t EntityPrepWorker::getPendingCount() const {
    return pendingQueue_.size() + (dispatchedSpawnId_ != 0 ? 1 : 0);
}

EntityPrepWorker::PrepResult EntityPrepWorker::processRequest(PrepRequest&& req) {
    auto start = std::chrono::steady_clock::now();

    // Step 1: Base race model prep (S3D load, WLD parse, animation merge)
    uint32_t key = (static_cast<uint32_t>(req.raceId) << 8) | req.gender;
    bool alreadyCached = modelLoader_->isModelDataCached(key);

    bool success = false;
    if (alreadyCached) {
        success = true;
        LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: race={} gender={} already cached, skipping base prep",
                  req.raceId, req.gender);
    } else {
        // CPU-heavy work (300-500ms on ARM) — runs OFF main thread
        success = modelLoader_->preloadModelData(req.raceId, req.gender);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: preload race={} gender={} took {}ms success={}",
                 req.raceId, req.gender, elapsed, success);
    }

    // Step 1b: Variant model prep (S3D load + WLD parse + animation merge for zone-specific variants)
    // This moves the expensive S3D load (e.g., commons_chr.s3d for QCM) off the render thread
    if (success) {
        uint8_t headVariant = req.appearance.helm;
        uint8_t bodyVariant = 0;

        // Check for robe body variant from texture or chest equipment
        uint8_t chestMaterial = static_cast<uint8_t>(
            req.appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
        if (isRobeTexture(req.appearance.texture) || isRobeTexture(chestMaterial)) {
            bodyVariant = 1;
        }

        if (headVariant != 0 || bodyVariant != 0) {
            auto variantStart = std::chrono::steady_clock::now();
            bool variantOk = modelLoader_->preloadVariantModel(
                req.raceId, req.gender, headVariant, bodyVariant);
            auto variantElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - variantStart).count();
            LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: variant prep race={} head={} body={} took {}ms success={}",
                     req.raceId, (int)headVariant, (int)bodyVariant, variantElapsed, variantOk);
        }
    }

    // Build result with per-entity data
    PrepResult result;
    result.spawnId = req.spawnId;
    result.raceId = req.raceId;
    result.gender = req.gender;
    result.appearance = req.appearance;
    result.success = success;

    if (success) {
        // Step 2: Variant texture decode (body-part overrides based on appearance)
        prepVariantTextures(req, result);

        // Step 3: Equipment S3D extraction + texture decode
        prepEquipmentModels(req, result);
    }

    auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: spawn={} total prep took {}ms (variants={}, equip={})",
             req.spawnId, totalElapsed, result.variantTextures.size(), result.equipmentData.size());

    return result;
}

// Decode a BMP file (raw BMP, not DDS-disguised-as-BMP) to ARGB pixels
// Returns false if not a valid BMP or on failure
static bool decodeBMPToARGB(const std::vector<char>& data, DecodedTexture& out) {
    if (data.size() < 54) return false;  // Minimum BMP header size
    if (data[0] != 'B' || data[1] != 'M') return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    uint32_t dataOffset = d[10] | (d[11] << 8) | (d[12] << 16) | (d[13] << 24);
    int32_t width = d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24);
    int32_t height = d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24);
    uint16_t bpp = d[28] | (d[29] << 8);

    bool bottomUp = (height > 0);
    if (height < 0) height = -height;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    if (bpp != 24 && bpp != 32) return false;
    if (dataOffset >= data.size()) return false;

    uint32_t w = static_cast<uint32_t>(width);
    uint32_t h = static_cast<uint32_t>(height);
    uint32_t rowStride = ((w * (bpp / 8) + 3) & ~3);  // Rows padded to 4-byte boundary

    out.name = "";  // Caller sets name
    out.width = w;
    out.height = h;
    out.argbPixels.resize(w * h);
    out.hasAlpha = (bpp == 32);

    for (uint32_t y = 0; y < h; ++y) {
        uint32_t srcRow = bottomUp ? (h - 1 - y) : y;
        const uint8_t* row = d + dataOffset + srcRow * rowStride;
        if (dataOffset + srcRow * rowStride + w * (bpp / 8) > data.size()) return false;

        for (uint32_t x = 0; x < w; ++x) {
            uint8_t b = row[x * (bpp / 8) + 0];
            uint8_t g = row[x * (bpp / 8) + 1];
            uint8_t r = row[x * (bpp / 8) + 2];
            uint8_t a = (bpp == 32) ? row[x * 4 + 3] : 255;
            out.argbPixels[y * w + x] = (static_cast<uint32_t>(a) << 24) |
                                         (static_cast<uint32_t>(r) << 16) |
                                         (static_cast<uint32_t>(g) << 8) |
                                         static_cast<uint32_t>(b);
            if (a < 255) out.hasAlpha = true;
        }
    }
    return true;
}

void EntityPrepWorker::prepVariantTextures(const PrepRequest& req, PrepResult& result) {
    // Get model data to find which textures are available
    auto modelData = modelLoader_->getRaceModelData(req.raceId, req.gender);

    // Check if the appearance requires any variant textures (body texture variant)
    const auto& appearance = req.appearance;
    if (appearance.texture == 0 && appearance.helm == 0) return;

    // Get race code for texture name transformation (e.g., "HUM", "ELF")
    std::string raceCode = RaceModelLoader::getRaceCode(req.raceId);
    if (raceCode.empty()) return;

    // Lowercase race code for texture matching
    std::string lowerRaceCode = raceCode;
    std::transform(lowerRaceCode.begin(), lowerRaceCode.end(), lowerRaceCode.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // For body texture variants, EQ generates texture names like:
    // clk<NN> for robes (texture >= 10), or <race>ch<NN> for chest armor, etc.
    // These textures may already be in the race model's texture map or in armor archives.
    // We decode DDS or BMP textures found that match variant patterns.

    // Track which textures we've already decoded (avoid duplicates)
    std::set<std::string> decodedNames;

    auto decodeAndAdd = [&](const std::string& texName, const std::shared_ptr<TextureInfo>& texInfo) {
        if (!texInfo || texInfo->data.empty()) return;

        std::string lowerName = texName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (decodedNames.count(lowerName) > 0) return;

        if (DDSDecoder::isDDS(texInfo->data)) {
            DecodedTexture decoded;
            decoded.name = texName;
            DecodedImage img = DDSDecoder::decode(texInfo->data);
            if (!img.isValid()) return;

            decoded.width = img.width;
            decoded.height = img.height;
            decoded.argbPixels.resize(img.width * img.height);
            for (uint32_t i = 0; i < img.width * img.height; ++i) {
                uint8_t r = img.pixels[i * 4 + 0];
                uint8_t g = img.pixels[i * 4 + 1];
                uint8_t b = img.pixels[i * 4 + 2];
                uint8_t a = img.pixels[i * 4 + 3];
                decoded.argbPixels[i] = (static_cast<uint32_t>(a) << 24) |
                                        (static_cast<uint32_t>(r) << 16) |
                                        (static_cast<uint32_t>(g) << 8) |
                                        static_cast<uint32_t>(b);
                if (a < 255) decoded.hasAlpha = true;
            }
            decodedNames.insert(lowerName);
            result.variantTextures.push_back(std::move(decoded));
        } else if (texInfo->data.size() >= 2 && texInfo->data[0] == 'B' && texInfo->data[1] == 'M') {
            // Real BMP file
            DecodedTexture decoded;
            if (decodeBMPToARGB(texInfo->data, decoded)) {
                decoded.name = texName;
                decodedNames.insert(lowerName);
                result.variantTextures.push_back(std::move(decoded));
            }
        }
    };

    // Build variant prefixes to search for
    // Variant texture naming: <race><slot><NN> where NN is the variant number
    // Slots: he (head/helm), ch (chest), ua (upper arm), fa (forearm),
    //        hn (hands), lg (legs), ft (feet)
    // Also: clk<NN> for cloaks/robes
    std::vector<std::string> variantPrefixes;
    if (appearance.texture >= 10) {
        // Robe textures use "clk" prefix
        char buf[32];
        snprintf(buf, sizeof(buf), "clk%02d", appearance.texture - 10);
        variantPrefixes.push_back(buf);
    } else if (appearance.texture > 0) {
        // Armor textures use race-specific prefixes
        char buf[32];
        const char* slots[] = {"ch", "ua", "fa", "hn", "lg", "ft"};
        for (const char* slot : slots) {
            snprintf(buf, sizeof(buf), "%s%s%02d", lowerRaceCode.c_str(), slot, appearance.texture);
            variantPrefixes.push_back(buf);
        }
    }
    if (appearance.helm > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%she%02d", lowerRaceCode.c_str(), appearance.helm);
        variantPrefixes.push_back(buf);
    }

    auto searchTextures = [&](const std::map<std::string, std::shared_ptr<TextureInfo>>& textures) {
        for (const auto& [texName, texInfo] : textures) {
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            for (const auto& prefix : variantPrefixes) {
                if (lowerName.find(prefix) != std::string::npos) {
                    decodeAndAdd(texName, texInfo);
                    break;
                }
            }
        }
    };

    // Search the base model's texture map first
    if (modelData && !modelData->textures.empty()) {
        searchTextures(modelData->textures);
    }

    // Search the variant model's texture map (from otherChrCaches_ via preloadVariantModel)
    // This is where zone-specific variant textures live (e.g., qcmch0101.bmp from commons_chr.s3d)
    uint8_t headVariant = appearance.helm;
    uint8_t bodyVariant = 0;
    uint8_t chestMaterial = static_cast<uint8_t>(
        appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
    if (isRobeTexture(appearance.texture) || isRobeTexture(chestMaterial)) {
        bodyVariant = 1;
    }

    if (headVariant != 0 || bodyVariant != 0) {
        auto variantModelData = modelLoader_->getVariantModelData(
            req.raceId, req.gender, headVariant, bodyVariant);
        if (variantModelData && !variantModelData->textures.empty()) {
            searchTextures(variantModelData->textures);
        }
    }

    if (!result.variantTextures.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: spawn={} decoded {} variant textures",
                  req.spawnId, result.variantTextures.size());
    }
}

void EntityPrepWorker::prepEquipmentModels(const PrepRequest& req, PrepResult& result) {
    if (!equipLoader_) return;

    const auto& appearance = req.appearance;
    uint32_t primaryId = appearance.equipment[7];    // Primary slot
    uint32_t secondaryId = appearance.equipment[8];  // Secondary slot

    if (primaryId == 0 && secondaryId == 0) return;

    auto prepOneEquipment = [&](uint32_t equipmentId, bool isPrimary) {
        if (equipmentId == 0) return;

        auto extractStart = std::chrono::steady_clock::now();

        // Resolve model ID
        int modelId = equipLoader_->getModelIdForItem(equipmentId);
        if (modelId < 0) modelId = static_cast<int>(equipmentId);

        // Look up model reference in the index
        const EquipmentModelLoader::EquipmentModelRef* modelRef = equipLoader_->getModelRef(modelId);
        if (!modelRef) {
            LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: no model ref for equip {} (modelId={})",
                      equipmentId, modelId);
            return;
        }

        // Extract equipment model off-thread (opens S3D, parses WLD, extracts geometry)
        auto equipData = EquipmentModelLoader::extractEquipmentModelOffThread(*modelRef, modelId);
        if (!equipData) {
            LOG_DEBUG(MOD_GRAPHICS, "EntityPrepWorker: failed to extract equip model IT{}",
                      modelId);
            return;
        }

        // Decode equipment textures (DDS → ARGB)
        PrepResult::EquipmentPrepData prepData;
        prepData.modelId = modelId;
        prepData.equipmentId = equipmentId;
        prepData.isPrimary = isPrimary;
        prepData.geometry = equipData->geometry;
        prepData.rawTextures = equipData->textures;

        for (const auto& texName : equipData->textureNames) {
            std::string lowerName = texName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            auto texIt = equipData->textures.find(lowerName);
            if (texIt == equipData->textures.end() || !texIt->second) continue;
            const auto& texInfo = texIt->second;
            if (texInfo->data.empty()) continue;

            if (DDSDecoder::isDDS(texInfo->data)) {
                DecodedTexture decoded;
                decoded.name = texName;
                DecodedImage img = DDSDecoder::decode(texInfo->data);
                if (img.isValid()) {
                    decoded.width = img.width;
                    decoded.height = img.height;
                    decoded.argbPixels.resize(img.width * img.height);
                    for (uint32_t i = 0; i < img.width * img.height; ++i) {
                        uint8_t r = img.pixels[i * 4 + 0];
                        uint8_t g = img.pixels[i * 4 + 1];
                        uint8_t b = img.pixels[i * 4 + 2];
                        uint8_t a = img.pixels[i * 4 + 3];
                        decoded.argbPixels[i] = (static_cast<uint32_t>(a) << 24) |
                                                (static_cast<uint32_t>(r) << 16) |
                                                (static_cast<uint32_t>(g) << 8) |
                                                static_cast<uint32_t>(b);
                        if (a < 255) decoded.hasAlpha = true;
                    }
                    prepData.decodedTextures.push_back(std::move(decoded));
                }
            }
        }

        auto extractElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - extractStart).count();
        LOG_INFO(MOD_GRAPHICS, "EntityPrepWorker: spawn={} equip IT{} extract took {}ms ({} textures decoded)",
                 req.spawnId, modelId, extractElapsed, prepData.decodedTextures.size());

        result.equipmentData.push_back(std::move(prepData));
    };

    prepOneEquipment(primaryId, true);
    prepOneEquipment(secondaryId, false);
}

} // namespace Graphics
} // namespace EQT
