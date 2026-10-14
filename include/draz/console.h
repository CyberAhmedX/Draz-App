#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace draz::console {

// Thrown when input ends (Ctrl+D, Ctrl+Z or a closed pipe), so the program
// can exit cleanly instead of looping forever on a dead input stream.
class InputClosed : public std::runtime_error {
public:
    InputClosed() : std::runtime_error("input closed") {}
};

constexpr std::size_t kWidth = 72;

void init();  // UTF-8 output and colors on Windows
void clear();

// Full-width store bar: "DRAZ > Customer > Cart" on the left, `right` on the right.
void header(const std::vector<std::string>& path, const std::string& right = "");
void section(const std::string& title);
void hint(const std::string& text);

void ok(const std::string& text);
void fail(const std::string& text);
void note(const std::string& text);

struct MenuItem {
    int key;
    std::string label;
    std::string detail;  // shown dimmed after the label, may be empty
};
// Prints the items and returns the chosen key.
int menu(const std::vector<MenuItem>& items);

struct Column {
    std::string title;
    std::size_t width;
    bool right = false;  // right-align (numbers and money)
};
void table(const std::vector<Column>& columns, const std::vector<std::vector<std::string>>& rows);

std::string money(std::int64_t rupees);  // "Rs 12,499"
std::string fit(const std::string& text, std::size_t width);

std::string readLine(const std::string& label);
std::string readPassword(const std::string& label);  // not shown while typing
int readInt(const std::string& label, int min, int max);
bool confirm(const std::string& question);
void pause();

}  // namespace draz::console
