#pragma once

#include "types.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mini_lsm {

class WAL {
public:
    Status open(std::filesystem::path path);
    Status append(KeyView key, ValueView value);
    Status recover(std::vector<std::pair<Key, Value>>& out) const;
    void   close();

private:
    std::filesystem::path path_;
    std::ofstream         out_;
};

} // namespace mini_lsm
