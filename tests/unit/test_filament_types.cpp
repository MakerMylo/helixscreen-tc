// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The material-type table: shipped rows come from assets/filaments.json's
// `types`, and the user overlay's `types` patches or extends them.

#include "filament_catalog.h"
#include "filament_database.h"
#include "helix_test_fixture.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

namespace fs = std::filesystem;
using helix::printer::FilamentCatalog;

namespace {

constexpr const char* ASSET = "assets/filaments.json";

struct TypesFixture : HelixTestFixture {
    fs::path dir;

    TypesFixture() {
        dir = fs::temp_directory_path() /
              ("helix-types-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(dir);
    }
    ~TypesFixture() override {
        filament::reload_materials();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string write(const std::string& name, const std::string& body) {
        auto p = (dir / name).string();
        std::ofstream(p) << body;
        return p;
    }
};

size_t asset_type_count() {
    std::ifstream f(ASSET);
    return nlohmann::json::parse(f)["types"].size();
}

} // namespace

TEST_CASE_METHOD(TypesFixture, "every shipped type row loads from the asset", "[filament][types]") {
    filament::load_materials_from(ASSET, "");
    const auto table = filament::materials();
    REQUIRE(table->size() == asset_type_count());
    REQUIRE(table->size() > 50);

    auto pla = filament::find_material("pla");
    REQUIRE(pla);
    CHECK(std::string(pla->name) == "PLA");
    CHECK(pla->nozzle_min == 190);
    CHECK(pla->nozzle_max == 220);
    CHECK(pla->bed_temp == 60);
    CHECK(pla->dry_temp_c == 45);
    CHECK(pla->density_g_cm3 == Catch::Approx(1.24f));
    CHECK(std::string(pla->compat_group) == "PLA");
    CHECK_FALSE(pla->user_defined);

    auto abs = filament::find_material("ABS");
    REQUIRE(abs);
    CHECK(abs->chamber_temp_c == 50);
    CHECK(std::string(abs->category) == "Engineering");
}

TEST_CASE_METHOD(TypesFixture, "a missing asset leaves an empty table, not a crash",
                 "[filament][types]") {
    filament::load_materials_from((dir / "nope.json").string(), "");
    CHECK(filament::materials()->empty());
    CHECK_FALSE(filament::find_material("PLA"));
    CHECK(filament::get_compatibility_group("PLA") == nullptr);
    CHECK(filament::are_materials_compatible("PLA", "ABS"));
}

TEST_CASE_METHOD(TypesFixture, "a corrupt asset leaves an empty table", "[filament][types]") {
    filament::load_materials_from(write("bad.json", "{\"types\": [ {\"name\": "), "");
    CHECK(filament::materials()->empty());
}

TEST_CASE_METHOD(TypesFixture, "an overlay entry patches only the fields it names",
                 "[filament][types]") {
    auto overlay = write("user.json", R"({"types": [{"name": "pla", "bed": 65, "chamber": 0}]})");
    filament::load_materials_from(ASSET, overlay);

    auto pla = filament::find_material("PLA");
    REQUIRE(pla);
    CHECK(pla->bed_temp == 65);
    CHECK(pla->nozzle_min == 190);
    CHECK(pla->nozzle_max == 220);
    CHECK(std::string(pla->name) == "PLA");
    CHECK_FALSE(pla->user_defined);
    CHECK(filament::materials()->size() == asset_type_count());
}

TEST_CASE_METHOD(TypesFixture, "an overlay entry with a new name defines a type",
                 "[filament][types]") {
    auto overlay = write("user.json", R"({"types": [
        {"name": "PEKK", "nozzle_min": 330, "nozzle_max": 360, "bed": 120}
    ]})");
    filament::load_materials_from(ASSET, overlay);

    auto pekk = filament::find_material("pekk");
    REQUIRE(pekk);
    CHECK(pekk->nozzle_min == 330);
    CHECK(pekk->nozzle_max == 360);
    CHECK(pekk->bed_temp == 120);
    CHECK(std::string(pekk->category) == "Custom");
    CHECK(pekk->user_defined);
    // Its own compat group: endless spool must not swap it with anything else.
    CHECK(std::string(pekk->compat_group) == "PEKK");
    CHECK_FALSE(filament::are_materials_compatible("PEKK", "PEEK"));
    CHECK(filament::materials()->size() == asset_type_count() + 1);
}

TEST_CASE_METHOD(TypesFixture, "a new type may name an existing compat group",
                 "[filament][types]") {
    auto overlay = write("user.json", R"({"types": [
        {"name": "Tough PLA", "nozzle_min": 205, "nozzle_max": 235, "bed": 60,
         "compat_group": "PLA"}
    ]})");
    filament::load_materials_from(ASSET, overlay);
    CHECK(filament::are_materials_compatible("Tough PLA", "PLA"));
}

TEST_CASE_METHOD(TypesFixture, "overlay type entries without a name are skipped",
                 "[filament][types]") {
    auto overlay = write("user.json", R"({"types": [{"bed": 90}, 7, {"name": ""}]})");
    filament::load_materials_from(ASSET, overlay);
    CHECK(filament::materials()->size() == asset_type_count());
    CHECK(filament::find_material("PLA")->bed_temp == 60);
}

TEST_CASE_METHOD(TypesFixture, "a bare-array overlay carries products and adds no types",
                 "[filament][types]") {
    auto overlay = write("user.json", R"([{"id": "x", "brand": "B", "name": "N", "type": "PLA"}])");
    filament::load_materials_from(ASSET, overlay);
    CHECK(filament::materials()->size() == asset_type_count());
}

TEST_CASE_METHOD(TypesFixture, "a name handed out survives a reload", "[filament][types]") {
    auto overlay = write("user.json", R"({"types": [{"name": "PEKK", "compat_group": "PAEK"}]})");
    filament::load_materials_from(ASSET, overlay);
    const char* group = filament::get_compatibility_group("PEKK");
    REQUIRE(group != nullptr);

    filament::load_materials_from(ASSET, "");
    CHECK(std::string(group) == "PAEK");
    CHECK_FALSE(filament::find_material("PEKK"));
}

TEST_CASE_METHOD(TypesFixture, "catalog products inherit from a user-defined type",
                 "[filament][types][filament_catalog]") {
    auto overlay = write("user.json", R"({
        "types": [{"name": "PEKK", "nozzle_min": 330, "nozzle_max": 360, "bed": 120}],
        "filaments": [{"id": "acme-pekk", "brand": "Acme", "name": "PEKK", "type": "PEKK"}]
    })");
    filament::load_materials_from(ASSET, overlay);

    auto cat = FilamentCatalog::load_with_overlay(ASSET, overlay);
    const auto* p = cat.resolve_id("acme-pekk");
    REQUIRE(p != nullptr);
    CHECK(p->nozzle_min == 330);
    CHECK(p->nozzle_max == 360);
    CHECK(p->bed_temp == 120);
    CHECK(p->compat_group == "PEKK");
}

TEST_CASE_METHOD(TypesFixture, "catalog products inherit a patched shipped type",
                 "[filament][types][filament_catalog]") {
    auto overlay = write("user.json", R"({"types": [{"name": "ABS", "chamber": 60}]})");
    filament::load_materials_from(ASSET, overlay);

    auto cat = FilamentCatalog::load_with_overlay(ASSET, overlay);
    const auto* p = cat.resolve_id("creality-cr-abs");
    REQUIRE(p != nullptr);
    CHECK(p->chamber_temp_c == 60);
}
