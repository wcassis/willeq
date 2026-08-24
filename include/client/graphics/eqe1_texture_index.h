#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace EQT { namespace Graphics {

// T207: name -> owning-archive index for an EQE1 tree, produced by
// tools/eqe1_texture_index_builder (data/eqe1_texture_index.json).
//
// Exists to replace the linear probe that ConstrainedTextureCache used to run
// over the configured global archive list: on a lookup miss it opened the next
// archive and retried, so a name present in no archive opened every one of them.
// A cold befallen zone-in opened 37 archives of which 24 served zero lookups.
//
// A name may live in many archives (21356 of 50447 names do; one lives in 691),
// so owners() returns the full list. Choosing among them is the caller's job --
// it needs the zone's candidate set, which this class deliberately knows nothing
// about.
class EQE1TextureIndex {
public:
    bool load(const std::string& path);
    bool isLoaded() const { return loaded_; }

    // Archives holding `name` (already lowercased, extension stripped -- the
    // same key the cache builds), or nullptr if no archive in the tree has it.
    // A nullptr here is the answer "absent", reached without opening anything.
    const std::vector<uint16_t>* owners(const std::string& name) const;

    const std::string& archiveName(uint16_t idx) const { return archives_[idx]; }
    size_t archiveCount() const { return archives_.size(); }
    size_t nameCount() const { return entries_.size(); }

private:
    std::vector<std::string> archives_;
    std::unordered_map<std::string, std::vector<uint16_t>> entries_;
    bool loaded_ = false;
};

}} // namespace EQT::Graphics
