// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_image_regions.h"

#include "data_root_resolver.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "hv/json.hpp"

namespace helix {

namespace {

std::optional<NormPoint> read_point(const nlohmann::json& v) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number())
        return std::nullopt;
    return NormPoint{v[0].get<float>(), v[1].get<float>()};
}

// Main-thread only; loaded flag gates the one-time file read.
std::unordered_map<std::string, ImageRegions>& table() {
    static std::unordered_map<std::string, ImageRegions> t;
    return t;
}

bool& loaded() {
    static bool l = false;
    return l;
}

} // namespace

std::unordered_map<std::string, ImageRegions> parse_image_regions(const std::string& json_text) {
    std::unordered_map<std::string, ImageRegions> out;
    const auto doc = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        spdlog::warn("[PrinterImageRegions] regions document is not a JSON object");
        return out;
    }
    for (const auto& [name, e] : doc.items()) {
        const auto size =
            e.is_object() && e.contains("size") ? read_point(e["size"]) : std::nullopt;
        const auto nozzle =
            e.is_object() && e.contains("nozzle") ? read_point(e["nozzle"]) : std::nullopt;
        const auto& bed = e.is_object() && e.contains("bed") ? e["bed"] : nlohmann::json();
        const auto bl = bed.is_array() && bed.size() == 2 ? read_point(bed[0]) : std::nullopt;
        const auto br = bed.is_array() && bed.size() == 2 ? read_point(bed[1]) : std::nullopt;
        if (!size || !nozzle || !bl || !br) {
            spdlog::warn("[PrinterImageRegions] skipping '{}': needs size, nozzle and bed", name);
            continue;
        }
        ImageRegions r;
        r.src_w = static_cast<int>(size->x);
        r.src_h = static_cast<int>(size->y);
        r.nozzle = *nozzle;
        r.bed_left = *bl;
        r.bed_right = *br;
        if (e.contains("part_fan"))
            r.part_fan = read_point(e["part_fan"]);
        if (e.contains("chamber"))
            r.chamber = read_point(e["chamber"]);
        if (e.contains("light"))
            r.light = read_point(e["light"]);
        out.emplace(name, r);
    }
    return out;
}

const ImageRegions* lookup_image_regions(std::string_view basename) {
    if (!loaded()) {
        loaded() = true;
        const std::string path = asset_path("assets/images/printers/regions.json");
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::fseek(f, 0, SEEK_END);
            long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            std::string text(size, '\0');
            std::size_t bytes_read = std::fread(text.data(), 1, size, f);
            std::fclose(f);
            if (bytes_read == static_cast<std::size_t>(size)) {
                table() = parse_image_regions(text);
                spdlog::debug("[PrinterImageRegions] {} tagged images", table().size());
            } else {
                spdlog::debug("[PrinterImageRegions] no regions.json; every image is untagged");
            }
        } else {
            spdlog::debug("[PrinterImageRegions] no regions.json; every image is untagged");
        }
    }
    const auto it = table().find(std::string(basename));
    return it == table().end() ? nullptr : &it->second;
}

void set_image_regions_for_testing(std::unordered_map<std::string, ImageRegions> regions) {
    table() = std::move(regions);
    loaded() = true;
}

std::string printer_image_basename(std::string_view path) {
    // Custom images live under the config dir's custom_images/, never here.
    if (path.find("/images/printers/") == std::string_view::npos)
        return {};
    const auto slash = path.find_last_of('/');
    std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    std::string stem(file.substr(0, dot));
    if (path.find("/prerendered/") != std::string_view::npos) {
        const auto dash = stem.find_last_of('-');
        if (dash != std::string::npos && dash + 1 < stem.size() &&
            std::all_of(stem.begin() + dash + 1, stem.end(),
                        [](unsigned char c) { return std::isdigit(c); }))
            stem.resize(dash);
    }
    return stem;
}

} // namespace helix
