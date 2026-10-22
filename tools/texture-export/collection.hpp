#pragma once
#include "textureproject.hpp"
namespace texturecollection {
albion::textureproject::Result exportInstallation(const std::filesystem::path& root,const std::filesystem::path& destination,albion::textureproject::Progress* progress,int compression=3);
}
