#include <filesystem>
#include <fstream>

#include "check.h"
#include "draz/storage.h"

using namespace draz::storage;
namespace fs = std::filesystem;

TEST("fields with tabs, newlines and backslashes survive a round trip") {
    Row row = {"plain", "with space", "tab\there", "line\nbreak", "back\\slash", "", "\\t literal"};
    CHECK(decodeRow(encodeRow(row)) == row);
    CHECK(encodeRow(row).find('\n') == std::string::npos);
}

TEST("tables are written and read back, and a missing file is empty") {
    fs::path dir = fs::temp_directory_path() / "draz_storage_test";
    fs::remove_all(dir);
    CHECK(readTable(dir / "none.tsv").empty());

    std::vector<Row> rows = {{"a", "Data Structures", "3"}, {"b", "x\ty", ""}};
    writeTable(dir / "t.tsv", rows);
    CHECK(readTable(dir / "t.tsv") == rows);
    CHECK(!fs::exists(dir / "t.tsv.tmp"));

    writeTable(dir / "t.tsv", {{"only"}});
    CHECK((readTable(dir / "t.tsv") == std::vector<Row>{{"only"}}));
    fs::remove_all(dir);
}

TEST("windows line endings and comments are ignored") {
    fs::path dir = fs::temp_directory_path() / "draz_storage_crlf";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "t.tsv", std::ios::binary);
        out << "# comment\r\na\tb\r\n\r\n";
    }
    CHECK((readTable(dir / "t.tsv") == std::vector<Row>{{"a", "b"}}));
    fs::remove_all(dir);
}
