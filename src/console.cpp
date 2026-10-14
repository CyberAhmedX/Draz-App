#include "draz/console.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#define DRAZ_ISATTY(fd) _isatty(fd)
#else
#include <termios.h>
#include <unistd.h>
#define DRAZ_ISATTY(fd) isatty(fd)
#endif

namespace draz::console {

namespace {

// Colors: the store's orange for the brand and prompts, plus green, red
// and amber for results. Nothing is colored when output isn't a terminal.
const char* const kOrange = "38;5;208";
const char* const kBar = "1;38;5;16;48;5;208";
const char* const kGreen = "38;5;78";
const char* const kRed = "38;5;203";
const char* const kAmber = "38;5;221";
const char* const kDim = "38;5;245";
const char* const kBold = "1";

bool colors() {
    static const bool enabled = DRAZ_ISATTY(1) != 0;
    return enabled;
}

std::string paint(const char* code, const std::string& text) {
    if (!colors()) return text;
    return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
}

// Columns a UTF-8 string takes up: every byte except continuation bytes
// (10xxxxxx) starts a new character.
std::size_t textWidth(const std::string& text) {
    std::size_t n = 0;
    for (unsigned char c : text) n += (c & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

std::string repeat(const std::string& piece, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += piece;
    return out;
}

std::string trim(const std::string& text) {
    std::size_t start = 0, end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(start, end - start);
}

std::string getLine() {
    std::string line;
    if (!std::getline(std::cin, line)) throw InputClosed();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

void prompt(const std::string& label) {
    std::cout << "  " << paint(kOrange, "►") << " " << label << std::flush;
}

bool isNumber(const std::string& text) {
    if (text.empty() || text.size() > 9) return false;
    std::size_t start = text[0] == '-' ? 1 : 0;
    if (start == text.size()) return false;
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(start), text.end(),
                       [](unsigned char c) { return std::isdigit(c); });
}

// Turns terminal echo off for as long as the object lives.
class HiddenInput {
public:
    HiddenInput() {
#ifdef _WIN32
        handle_ = GetStdHandle(STD_INPUT_HANDLE);
        active_ = GetConsoleMode(handle_, &mode_) != 0;
        if (active_) SetConsoleMode(handle_, mode_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
        active_ = isatty(0) && tcgetattr(0, &saved_) == 0;
        if (active_) {
            termios quiet = saved_;
            quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            tcsetattr(0, TCSAFLUSH, &quiet);
        }
#endif
    }
    ~HiddenInput() {
        if (!active_) return;
#ifdef _WIN32
        SetConsoleMode(handle_, mode_);
#else
        tcsetattr(0, TCSAFLUSH, &saved_);
#endif
        std::cout << "\n";
    }
    HiddenInput(const HiddenInput&) = delete;
    HiddenInput& operator=(const HiddenInput&) = delete;

private:
    bool active_ = false;
#ifdef _WIN32
    HANDLE handle_ = nullptr;
    DWORD mode_ = 0;
#else
    termios saved_{};
#endif
};

}  // namespace

void init() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

void clear() {
    if (colors()) std::cout << "\x1b[2J\x1b[H";
}

void header(const std::vector<std::string>& path, const std::string& right) {
    std::string left = " DRAZ";
    for (const std::string& part : path) left += "  >  " + part;
    std::string r = right.empty() ? "" : right + " ";
    std::size_t used = textWidth(left) + textWidth(r);
    std::size_t gap = kWidth > used ? kWidth - used : 1;
    std::string line = left + std::string(gap, ' ') + r;

    std::cout << "\n";
    if (colors()) {
        std::cout << paint(kBar, line) << "\n\n";
    } else {
        std::cout << line << "\n" << std::string(kWidth, '=') << "\n\n";
    }
}

void section(const std::string& title) {
    std::cout << "  " << paint(kOrange, "▌") << " " << paint(kBold, title) << "\n\n";
}

void hint(const std::string& text) { std::cout << "  " << paint(kDim, text) << "\n"; }

void ok(const std::string& text) { std::cout << "\n  " << paint(kGreen, "+ " + text) << "\n"; }
void fail(const std::string& text) { std::cout << "\n  " << paint(kRed, "x " + text) << "\n"; }
void note(const std::string& text) { std::cout << "  " << paint(kAmber, "! " + text) << "\n"; }

int menu(const std::vector<MenuItem>& items) {
    int lowest = 0, highest = 0;
    for (const MenuItem& item : items) {
        std::string key = "[" + std::to_string(item.key) + "]";
        std::cout << "   " << paint(kOrange, key) << std::string(item.key >= 10 ? 1 : 2, ' ') << item.label;
        if (!item.detail.empty()) {
            std::size_t used = textWidth(item.label);
            std::cout << std::string(used < 26 ? 26 - used : 1, ' ') << paint(kDim, item.detail);
        }
        std::cout << "\n";
        lowest = std::min(lowest, item.key);
        highest = std::max(highest, item.key);
    }
    std::cout << "\n";
    while (true) {
        prompt("");
        std::string text = trim(getLine());
        if (isNumber(text)) {
            int choice = std::stoi(text);
            for (const MenuItem& item : items) {
                if (item.key == choice) return choice;
            }
        }
        fail("Pick one of the numbers in brackets.");
        std::cout << "\n";
    }
}

std::string fit(const std::string& text, std::size_t width) {
    if (textWidth(text) <= width) return text;
    // Cut at a character boundary, leaving room for "...".
    std::size_t keep = width > 3 ? width - 3 : width, count = 0, i = 0;
    for (; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && count++ == keep) break;
    }
    return text.substr(0, i) + (width > 3 ? "..." : "");
}

void table(const std::vector<Column>& columns, const std::vector<std::vector<std::string>>& rows) {
    auto cell = [](const std::string& text, const Column& c) {
        std::string t = fit(text, c.width);
        std::string pad(c.width - std::min(c.width, textWidth(t)), ' ');
        return c.right ? pad + t : t + pad;
    };
    std::string head = "  ", rule = "  ";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i) {
            head += "  ";
            rule += "  ";
        }
        head += cell(columns[i].title, columns[i]);
        rule += repeat("─", columns[i].width);
    }
    std::cout << paint(kDim, head) << "\n" << paint(kDim, rule) << "\n";
    for (const auto& row : rows) {
        std::cout << "  ";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i) std::cout << "  ";
            std::cout << cell(i < row.size() ? row[i] : "", columns[i]);
        }
        std::cout << "\n";
    }
}

std::string money(std::int64_t rupees) {
    bool negative = rupees < 0;
    std::string digits = std::to_string(negative ? -rupees : rupees);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return (negative ? "-Rs " : "Rs ") + out;
}

std::string readLine(const std::string& label) {
    prompt(label);
    return trim(getLine());
}

std::string readPassword(const std::string& label) {
    prompt(label);
    HiddenInput hidden;
    return getLine();
}

int readInt(const std::string& label, int min, int max) {
    while (true) {
        std::string text = readLine(label);
        if (isNumber(text)) {
            int value = std::stoi(text);
            if (value >= min && value <= max) return value;
        }
        fail("Enter a whole number from " + std::to_string(min) + " to " + std::to_string(max) + ".");
        std::cout << "\n";
    }
}

bool confirm(const std::string& question) {
    while (true) {
        std::string text = readLine(question + " (y/n) ");
        if (text == "y" || text == "Y") return true;
        if (text == "n" || text == "N") return false;
        fail("Type y or n.");
        std::cout << "\n";
    }
}

void pause() {
    std::cout << "\n  " << paint(kDim, "Press Enter to go back") << std::flush;
    getLine();
}

}  // namespace draz::console
