// eb::xbox::cout / cerr and the log sink behind them.
#include <hal/debug.h>
#include <xboxkrnl/xboxkrnl.h>

#include <cstring>
#include <ostream>
#include <streambuf>
#include <string>

#include "xbox_platform.hpp"

namespace eb::xbox {
namespace {

bool g_screen_console = false;
std::string g_line;

void emit_line(const std::string &line) {
    DbgPrint("%s\n", line.c_str());
    if (g_screen_console) debugPrint("%s\n", line.c_str());
}

class LogBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            const char c = static_cast<char>(ch);
            log_write(&c, 1);
        }
        return traits_type::not_eof(ch);
    }
    std::streamsize xsputn(const char *text, std::streamsize count) override {
        log_write(text, static_cast<std::size_t>(count));
        return count;
    }
};

LogBuffer g_buffer;

} // namespace

void log_write(const char *text, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            emit_line(g_line);
            g_line.clear();
        } else if (text[i] != '\r') {
            g_line += text[i];
            if (g_line.size() >= 240) { // keep debugPrint lines on screen
                emit_line(g_line);
                g_line.clear();
            }
        }
    }
}

void set_screen_console_enabled(bool enabled) { g_screen_console = enabled; }

std::ostream cout(&g_buffer);
std::ostream cerr(&g_buffer);

} // namespace eb::xbox
