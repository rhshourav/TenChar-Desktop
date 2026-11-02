// The English and Bangla string tables must have the same keys and the same %n placeholders, and every key the
// interface uses must exist. (A native Bangla speaker still has to review the wording.)
#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>

#include "tc/json.hpp"
#include "testing.hpp"

using namespace tc;
namespace fs = std::filesystem;

static std::set<std::string> placeholders(const std::string& s) {
    std::set<std::string> out;
    for (std::size_t i = 0; i + 1 < s.size(); ++i)
        if (s[i] == '%' && s[i + 1] >= '0' && s[i + 1] <= '9') out.insert(s.substr(i, 2));
    return out;
}

int main() {
    const fs::path app = fs::path(TC_APP_DIR);
    auto en = parseJson(readFile((app / "resources/strings/en.json").string()));
    auto bn = parseJson(readFile((app / "resources/strings/bn.json").string()));
    CHECK(en && bn);
    std::set<std::string> enKeys, bnKeys;
    for (auto& [k, v] : en->object) {
        enKeys.insert(k);
        CHECK(v.type == Json::Type::String && !v.string.empty());
    }
    for (auto& [k, v] : bn->object) {
        bnKeys.insert(k);
        CHECK(v.type == Json::Type::String && !v.string.empty());
        if (const Json* e = en->find(k)) CHECK(placeholders(e->string) == placeholders(v.string));
    }
    for (auto& k : enKeys) if (!bnKeys.count(k)) { ++g_fail; std::fprintf(stderr, "missing in Bangla: %s\n", k.c_str()); }
    for (auto& k : bnKeys) if (!enKeys.count(k)) { ++g_fail; std::fprintf(stderr, "only in Bangla: %s\n", k.c_str()); }

    // every ui.strings.<key> used in QML exists
    std::regex use(R"(ui\.strings\.([a-z0-9_]+))");
    std::set<std::string> used;
    for (auto& e : fs::directory_iterator(app / "qml")) {
        if (e.path().extension() != ".qml") continue;
        std::string text = readFile(e.path().string());
        for (std::sregex_iterator it(text.begin(), text.end(), use), end; it != end; ++it) used.insert((*it)[1]);
    }
    for (auto& k : used) {
        ++g_checks;
        if (!enKeys.count(k)) { ++g_fail; std::fprintf(stderr, "QML uses a missing string key: %s\n", k.c_str()); }
    }
    // and no key is dead weight
    for (auto& k : enKeys)
        if (!used.count(k) && k.rfind("tray_", 0) != 0) std::fprintf(stderr, "note: unused string key %s\n", k.c_str());
    return FINISH();
}
