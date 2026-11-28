#pragma once

#include <string>
#include <string_view>

std::string musicIdentifier(std::string_view value);
std::string musicNamespace(std::string_view value);
std::string musicLibraryId(std::string_view canonicalRoot);
std::string musicSongId(std::string_view nameSpace, std::string_view relativePath);
std::string musicArtistId(std::string_view name);
std::string musicAlbumId(std::string_view artistId, std::string_view title);
