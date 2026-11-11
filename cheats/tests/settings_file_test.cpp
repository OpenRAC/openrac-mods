// SPDX-License-Identifier: GPL-3.0-or-later
// The cheats kept in step with the launcher's list of mods (src/common/settings_file.h), alone:
// the cheats read from a list (the others' values kept, a wrong type ignored), written into one
// (every other mod and setting kept), the file followed on disk (a change of the mod's own not read
// back, the launcher's read), and the list found beside the mod's folder.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "settings_file.h"

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

namespace fs = std::filesystem;

namespace {

// A list as the launcher writes it: another mod first, then the cheats with two settings.
const char* const kList = R"({
  "format": 1,
  "mods": [
    {
      "id": "lock-strafe",
      "enabled": true,
      "settings": {
        "mode": "hold"
      }
    },
    {
      "id": "cheats",
      "enabled": true,
      "settings": {
        "bolt_multiplier": "10",
        "trace": true
      }
    }
  ]
}
)";

void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Moves a file's time on, as another program's write a moment later does.
void touch_later(const fs::path& path) {
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
}

}  // namespace

int main() {
    // Read: the list's values over the base, the missing ones the base's.
    cheats::State base;
    base.infinite_air = true;
    auto read_back = cheats::read_settings(kList, "cheats", base);
    CHECK(read_back && read_back->multiplier == 10 && read_back->infinite_air);
    CHECK(!read_back->invincible && !read_back->infinite_ammo);

    // A value of another type is ignored; an entry without settings is the base.
    read_back = cheats::read_settings(
        R"({"format":1,"mods":[{"id":"cheats","settings":{"invincible":"yes","infinite_ammo":true}}]})",
        "cheats",
        {}
    );
    CHECK(read_back && !read_back->invincible && read_back->infinite_ammo);
    read_back = cheats::read_settings(R"({"format":1,"mods":[{"id":"cheats"}]})", "cheats", base);
    CHECK(read_back && read_back->infinite_air);

    // No entry, not a list, not JSON: nothing.
    CHECK(!cheats::read_settings(kList, "other", {}));
    CHECK(!cheats::read_settings(R"({"format":1})", "cheats", {}));
    CHECK(!cheats::read_settings("{ half", "cheats", {}));

    // Write: the cheats set, every other mod and setting kept, the mods in their order.
    cheats::State on;
    on.invincible = true;
    on.infinite_air = true;
    on.multiplier = 100;
    const auto written = cheats::write_settings(kList, "cheats", on);
    CHECK(written && written->back() == '\n');
    CHECK(written->find("\"mode\": \"hold\"") != std::string::npos);
    CHECK(written->find("\"trace\": true") != std::string::npos);
    CHECK(written->find("\"bolt_multiplier\": \"100\"") != std::string::npos);
    CHECK(written->find("lock-strafe") < written->find("\"cheats\""));
    read_back = cheats::read_settings(*written, "cheats", {});
    CHECK(read_back && read_back->invincible && !read_back->infinite_ammo);
    CHECK(read_back->infinite_air && read_back->multiplier == 100);

    // An entry without settings gets them; no entry or no list writes nothing.
    const auto made =
        cheats::write_settings(R"({"format":1,"mods":[{"id":"cheats"}]})", "cheats", on);
    CHECK(made && cheats::read_settings(*made, "cheats", {})->invincible);
    CHECK(!cheats::write_settings(kList, "other", on));
    CHECK(!cheats::write_settings("[]", "cheats", on));

    // The file on disk, in a folder of the test's own.
    std::random_device random;
    const fs::path dir = fs::temp_directory_path() / ("cheats-test-" + std::to_string(random()));
    fs::create_directories(dir);
    const fs::path list = dir / "plugins.json";
    write(list, kList);

    cheats::SettingsFile file;
    CHECK(!file.ready());
    std::string error;
    CHECK(!file.save(on, error) && !error.empty());
    file.open(list, "cheats");
    CHECK(file.ready());

    // Nothing changed since it was opened.
    cheats::State state = *cheats::read_settings(kList, "cheats", {});
    CHECK(!file.poll(state));

    // The mod's own write: on disk, and not read back as a change.
    CHECK(file.save(on, error));
    CHECK(read(list).find("\"invincible\": true") != std::string::npos);
    CHECK(read(list).find("lock-strafe") != std::string::npos);
    CHECK(!fs::exists(dir / "plugins.json.cheats.tmp"));
    state = on;
    CHECK(!file.poll(state));

    // The launcher's write: read, once.
    cheats::State launcher = on;
    launcher.invincible = false;
    launcher.multiplier = 2;
    write(list, *cheats::write_settings(read(list), "cheats", launcher));
    touch_later(list);
    CHECK(file.poll(state) && !state.invincible && state.multiplier == 2 && state.infinite_air);
    CHECK(!file.poll(state));

    // A change of another mod's only: not a change of the cheats.
    std::string other = read(list);
    other.replace(other.find("\"hold\""), 6, "\"lock\"");
    write(list, other);
    touch_later(list);
    CHECK(!file.poll(state));

    // Half written by another program: not taken, and read again once whole.
    write(list, "{ \"format\": 1, \"mo");
    touch_later(list);
    CHECK(!file.poll(state));
    launcher.infinite_ammo = true;
    write(list, *cheats::write_settings(kList, "cheats", launcher));
    touch_later(list);
    CHECK(file.poll(state) && state.infinite_ammo);

    // The list beside the mod's folder: <plugins>/<id>/lib/<platform>/<file>.
    const fs::path lib = dir / "cheats" / "lib" / "windows-x86_64";
    fs::create_directories(lib);
    write(dir / "cheats" / "openrac-mod.json", "{}");
    write(lib / "cheats.dll", "");
    const auto found = cheats::list_beside(lib / "cheats.dll", "cheats");
    CHECK(found && fs::equivalent(*found, list));
    CHECK(!cheats::list_beside(lib / "cheats.dll", "other"));
    CHECK(!cheats::list_beside(dir / "cheats.dll", "cheats"));

    // The system says where this program is.
    CHECK(cheats::own_library().has_value());

    fs::remove_all(dir);
    std::puts("cheats settings file: ok");
    return 0;
}
