#include "deck_ui.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace {
constexpr std::string_view previewVersion = "deckds-preview-1";

template <class T>
void readRange(std::istream& input, T& field, int min, int max)
{
    int value = 0;
    if (input >> value && value >= min && value <= max)
        field = static_cast<T>(value);
}
}

bool DeckUi::loadPreview(std::istream& settings)
{
    std::string key;
    if (!std::getline(settings, key) || key != previewVersion)
        return false;

    std::unordered_map<std::string_view, int> trackIndices;
    trackIndices.reserve(player.tracks.size());
    for (size_t i = 0; i < player.tracks.size(); ++i)
        trackIndices.emplace(player.tracks[i].id, static_cast<int>(i));
    const auto readTrackList = [&](std::istream& input) {
        std::vector<int> indices;
        for (std::string id; input >> std::quoted(id);) {
            if (const auto found = trackIndices.find(id); found != trackIndices.end())
                indices.push_back(found->second);
        }
        return indices;
    };

    Tab savedTab = tab;
    while (settings >> key) {
        std::string name, line;
        if (key == "playlist" && !(settings >> std::quoted(name)))
            break;
        std::getline(settings, line);
        std::istringstream input(std::move(line));
        if (key == "display") {
            readRange(input, theme, 0, static_cast<int>(Theme::Vfd));
            readRange(input, scaleMode, 0, static_cast<int>(PreviewScale::Count) - 1);
            readRange(input, brightness, 0, static_cast<int>(std::size(brightnessLevels)) - 1);
            readRange(input, stage, 0, 2);
            for (auto& accent : themeAccents)
                readRange(input, accent, 0, 3);
        } else if (key == "system") {
            readRange(input, language, 0, static_cast<int>(Language::Japanese));
            readRange(input, pocket, 0, 1);
            readRange(input, clock24h, 0, 1);
            readRange(input, clockOffsetMinutes, 0, 24 * 60 - 1);
        } else if (key == "library") {
            readRange(input, savedTab, 0, static_cast<int>(Tab::Queue));
            readRange(input, albumsInGrid, 0, 1);
            readRange(input, player.largeLibrary, 0, 1);
        } else if (key == "sort") {
            int index = -1, field = -1;
            bool direction = false;
            if (input >> index >> field >> direction && index >= 0 && index < static_cast<int>(sortFields.size())) {
                const auto specs = sortSpecsFor(static_cast<Tab>(index));
                if (std::ranges::any_of(specs, [&](const SortSpec& spec) { return static_cast<int>(spec.field) == field; })) {
                    sortFields[index] = static_cast<SortField>(field);
                    sortDirections[index] = direction;
                }
            }
        } else if (key == "audio") {
            readRange(input, player.volume, 0, 100);
            readRange(input, lastNonZeroVolume, 1, 100);
            readRange(input, player.mute, 0, 1);
            readRange(input, player.shuffle, 0, 1);
            readRange(input, player.repeat, 0, 2);
            readRange(input, output, 0, 2);
            readRange(input, player.sfx, 0, 1);
            readRange(input, player.sfxVolume, 0, 2);
            readRange(input, player.sfxSpeaker, 0, 1);
        } else if (key == "position") {
            std::string id;
            int seconds = 0;
            if (input >> std::quoted(id) >> seconds) {
                if (id.empty()) {
                    player.current = -1;
                    player.seconds = 0;
                } else if (const auto found = trackIndices.find(id); found != trackIndices.end()) {
                    player.current = found->second;
                    player.seconds = std::clamp(seconds, 0, std::max(0, player.track()->duration - 1));
                }
            }
        } else if (key == "queue") {
            player.queue = readTrackList(input);
        } else if (key == "history") {
            player.history = readTrackList(input);
        } else if (key == "stars") {
            std::fill(player.stars.begin(), player.stars.end(), false);
            for (int index : readTrackList(input))
                player.stars[index] = true;
        } else if (key == "playlists") {
            player.playlists.clear();
        } else if (key == "playlist") {
            player.playlists.push_back({std::move(name), readTrackList(input)});
        }
    }

    setTab(static_cast<int>(savedTab));
    settleTab();
    return true;
}

void DeckUi::savePreview(std::ostream& settings) const
{
    settings << previewVersion << '\n'
             << "display " << static_cast<int>(theme) << ' ' << static_cast<int>(scaleMode) << ' '
             << brightness << ' ' << stage;
    for (const int accent : themeAccents)
        settings << ' ' << accent;
    settings << "\nsystem " << static_cast<int>(language) << ' ' << pocket << ' ' << clock24h << ' ' << clockOffsetMinutes
             << "\nlibrary " << static_cast<int>(tab) << ' ' << albumsInGrid << ' ' << player.largeLibrary << '\n';
    for (const auto sortedTab : {Tab::Albums, Tab::Artists, Tab::Songs}) {
        const auto index = static_cast<int>(sortedTab);
        const auto field = sortedTab == tab ? sortField : sortFields[index];
        const bool direction = sortedTab == tab ? ascending : sortDirections[index];
        settings << "sort " << index << ' ' << static_cast<int>(field) << ' ' << direction << '\n';
    }
    settings << "audio " << player.volume << ' ' << lastNonZeroVolume << ' ' << player.mute << ' ' << player.shuffle
             << ' ' << player.repeat << ' ' << output << ' ' << player.sfx << ' ' << player.sfxVolume
             << ' ' << player.sfxSpeaker << '\n';
    const auto* track = player.track();
    settings << "position " << std::quoted(track ? track->id : std::string_view{})
             << ' ' << static_cast<int>(player.seconds) << '\n';

    const auto writeTrackList = [&](const std::vector<int>& indices) {
        for (const int index : indices)
            settings << ' ' << std::quoted(player.tracks[index].id);
        settings << '\n';
    };
    settings << "queue";
    writeTrackList(player.queue);
    settings << "history";
    writeTrackList(player.history);
    settings << "stars";
    for (size_t i = 0; i < player.stars.size(); ++i)
        if (player.stars[i])
            settings << ' ' << std::quoted(player.tracks[i].id);
    settings << "\nplaylists\n";
    for (const auto& playlist : player.playlists) {
        settings << "playlist " << std::quoted(playlist.name);
        writeTrackList(playlist.tracks);
    }
}
