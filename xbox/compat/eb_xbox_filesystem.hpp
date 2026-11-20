// Minimal std::filesystem replacement (namespace eb::xbox::fs) for nxdk, backed by its WinAPI subset.
//
// nxdk's libc++ ships <filesystem> headers but no library implementation, and
// no MoveFileEx. This header provides exactly the surface the Phase Distorter
// sources use (see xbox/README.md), with these Xbox-specific behaviours:
//   * paths are plain byte strings; both '/' and '\\' separate components
//   * FATX is case-insensitive, so equivalent() compares case-insensitively
//   * there are no symlinks: symlink_status() == status()
//   * rename() onto an existing file is emulated with a two-step swap that
//     restores the old file if the final move fails (MoveFileA cannot replace)
// `xbox/tools/prepare_sources.py` rewrites `#include <filesystem>` to include
// this header in the prepared copy of the sources.
#pragma once

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <windows.h>

namespace eb::xbox::fs {

class path {
public:
    using value_type = char;
    using string_type = std::string;
    static constexpr value_type preferred_separator = '\\';

    path() = default;
    path(string_type s) : text_(std::move(s)) {}
    path(const char *s) : text_(s ? s : "") {}
    path(const std::u8string &s) : text_(s.begin(), s.end()) {}

    const string_type &native() const noexcept { return text_; }
    const value_type *c_str() const noexcept { return text_.c_str(); }
    string_type string() const { return text_; }
    string_type generic_string() const {
        string_type out = text_;
        for (char &c : out)
            if (c == '\\') c = '/';
        return out;
    }
    std::u8string u8string() const { return std::u8string(text_.begin(), text_.end()); }
    bool empty() const noexcept { return text_.empty(); }
    operator string_type() const { return text_; }

    bool is_absolute() const noexcept {
        return (text_.size() >= 2 && text_[1] == ':') || (!text_.empty() && is_sep(text_[0]));
    }
    bool has_filename() const { return !filename().empty(); }
    bool has_parent_path() const { return !parent_path().empty(); }

    path parent_path() const {
        const auto i = last_sep();
        if (i == npos) return {};
        if (i > 0 && text_[i - 1] == ':') return path(text_.substr(0, i + 1)); // "E:\x" -> "E:\"
        if (i == 0) return path(text_.substr(0, 1));
        return path(text_.substr(0, i));
    }
    path filename() const {
        const auto i = last_sep();
        if (i == npos) return text_.find(':') == 1 && text_.size() == 2 ? path() : *this;
        return path(text_.substr(i + 1));
    }
    path extension() const {
        const auto name = filename().text_;
        const auto dot = name.rfind('.');
        if (dot == npos || dot == 0 || name == "..") return {};
        return path(name.substr(dot));
    }
    path stem() const {
        const auto name = filename().text_;
        const auto dot = name.rfind('.');
        if (dot == npos || dot == 0 || name == "..") return path(name);
        return path(name.substr(0, dot));
    }
    path &replace_extension(const path &replacement = path()) {
        const auto ext = extension().text_;
        text_.erase(text_.size() - ext.size());
        if (!replacement.text_.empty()) {
            if (replacement.text_[0] != '.') text_ += '.';
            text_ += replacement.text_;
        }
        return *this;
    }
    path &replace_filename(const path &replacement) {
        const auto i = last_sep();
        text_.erase(i == npos ? 0 : i + 1);
        text_ += replacement.text_;
        return *this;
    }

    path &operator/=(const path &rhs) {
        if (rhs.is_absolute() || text_.empty()) {
            text_ = rhs.text_;
        } else {
            if (!is_sep(text_.back())) text_ += preferred_separator;
            text_ += rhs.text_;
        }
        return *this;
    }
    path &operator+=(const path &rhs) {
        text_ += rhs.text_;
        return *this;
    }
    path &operator+=(const string_type &rhs) {
        text_ += rhs;
        return *this;
    }
    path &operator+=(const char *rhs) {
        text_ += rhs;
        return *this;
    }

    friend path operator/(const path &lhs, const path &rhs) {
        path out(lhs);
        out /= rhs;
        return out;
    }
    friend bool operator==(const path &a, const path &b) noexcept { return a.text_ == b.text_; }
    friend bool operator!=(const path &a, const path &b) noexcept { return a.text_ != b.text_; }
    friend bool operator<(const path &a, const path &b) noexcept { return a.text_ < b.text_; }

private:
    static constexpr size_t npos = string_type::npos;
    static bool is_sep(char c) noexcept { return c == '/' || c == '\\'; }
    size_t last_sep() const noexcept {
        for (size_t i = text_.size(); i-- > 0;)
            if (is_sep(text_[i])) return i;
        return npos;
    }
    string_type text_;
};

class filesystem_error : public std::system_error {
public:
    filesystem_error(const std::string &what, std::error_code ec) : system_error(ec, what) {}
    filesystem_error(const std::string &what, const path &p1, std::error_code ec)
        : system_error(ec, what), p1_(p1) {}
    const path &path1() const noexcept { return p1_; }

private:
    path p1_;
};

enum class file_type { none, not_found, regular, directory, symlink, block, character, fifo, socket, unknown };

class file_status {
public:
    file_status() noexcept = default;
    explicit file_status(file_type t) noexcept : type_(t) {}
    file_type type() const noexcept { return type_; }

private:
    file_type type_ = file_type::none;
};

namespace detail {

inline std::error_code make_ec(DWORD win32) {
    if (win32 == ERROR_FILE_NOT_FOUND || win32 == ERROR_PATH_NOT_FOUND)
        return std::make_error_code(std::errc::no_such_file_or_directory);
    if (win32 == ERROR_ALREADY_EXISTS) return std::make_error_code(std::errc::file_exists);
    return std::make_error_code(std::errc::io_error);
}

inline file_status query(const path &p, std::error_code &ec) noexcept {
    const DWORD attributes = GetFileAttributesA(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            ec.clear();
            return file_status(file_type::not_found);
        }
        ec = make_ec(error);
        return file_status(file_type::none);
    }
    ec.clear();
    return file_status((attributes & FILE_ATTRIBUTE_DIRECTORY) ? file_type::directory : file_type::regular);
}

inline char fold(char c) noexcept { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : (c == '/' ? '\\' : c); }

} // namespace detail

// ---- status ---------------------------------------------------------------
inline file_status status(const path &p, std::error_code &ec) noexcept { return detail::query(p, ec); }
inline file_status status(const path &p) {
    std::error_code ec;
    auto s = detail::query(p, ec);
    if (ec) throw filesystem_error("status", p, ec);
    return s;
}
inline file_status symlink_status(const path &p, std::error_code &ec) noexcept { return detail::query(p, ec); }
inline file_status symlink_status(const path &p) { return status(p); }

inline bool exists(file_status s) noexcept { return s.type() != file_type::none && s.type() != file_type::not_found; }
inline bool is_regular_file(file_status s) noexcept { return s.type() == file_type::regular; }
inline bool is_directory(file_status s) noexcept { return s.type() == file_type::directory; }
inline bool is_symlink(file_status) noexcept { return false; }

inline bool exists(const path &p, std::error_code &ec) noexcept { return exists(status(p, ec)); }
inline bool exists(const path &p) { return exists(status(p)); }
inline bool is_regular_file(const path &p, std::error_code &ec) noexcept { return is_regular_file(status(p, ec)); }
inline bool is_regular_file(const path &p) { return is_regular_file(status(p)); }
inline bool is_directory(const path &p, std::error_code &ec) noexcept { return is_directory(status(p, ec)); }
inline bool is_directory(const path &p) { return is_directory(status(p)); }
inline bool is_symlink(const path &, std::error_code &ec) noexcept {
    ec.clear();
    return false;
}

// ---- size / remove / rename / create --------------------------------------
inline std::uintmax_t file_size(const path &p, std::error_code &ec) noexcept {
    HANDLE h = CreateFileA(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ec = detail::make_ec(GetLastError());
        return static_cast<std::uintmax_t>(-1);
    }
    LARGE_INTEGER size{};
    const BOOL ok = GetFileSizeEx(h, &size);
    CloseHandle(h);
    if (!ok) {
        ec = detail::make_ec(GetLastError());
        return static_cast<std::uintmax_t>(-1);
    }
    ec.clear();
    return static_cast<std::uintmax_t>(size.QuadPart);
}
inline std::uintmax_t file_size(const path &p) {
    std::error_code ec;
    const auto size = file_size(p, ec);
    if (ec) throw filesystem_error("file_size", p, ec);
    return size;
}

inline bool remove(const path &p, std::error_code &ec) noexcept {
    const auto s = detail::query(p, ec);
    if (ec) return false;
    if (s.type() == file_type::not_found) return false;
    const BOOL ok = s.type() == file_type::directory ? RemoveDirectoryA(p.c_str()) : DeleteFileA(p.c_str());
    if (!ok) {
        ec = detail::make_ec(GetLastError());
        return false;
    }
    ec.clear();
    return true;
}
inline bool remove(const path &p) {
    std::error_code ec;
    const bool removed = remove(p, ec);
    if (ec) throw filesystem_error("remove", p, ec);
    return removed;
}

inline void rename(const path &from, const path &to, std::error_code &ec) noexcept {
    ec.clear();
    // MoveFileExW (eb_xbox_win32.hpp) emulates replace-existing with a safe two-step swap.
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) ec = detail::make_ec(GetLastError());
}
inline void rename(const path &from, const path &to) {
    std::error_code ec;
    rename(from, to, ec);
    if (ec) throw filesystem_error("rename", from, ec);
}

inline bool create_directory(const path &p, std::error_code &ec) noexcept {
    if (CreateDirectoryA(p.c_str(), nullptr)) {
        ec.clear();
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS && is_directory(detail::query(p, ec))) {
        ec.clear();
        return false;
    }
    ec = detail::make_ec(error);
    return false;
}
inline bool create_directories(const path &p, std::error_code &ec) noexcept {
    ec.clear();
    if (p.empty()) return false;
    if (is_directory(detail::query(p, ec))) return false;
    const auto parent = p.parent_path();
    if (!parent.empty() && parent != p) {
        create_directories(parent, ec);
        if (ec) return false;
    }
    return create_directory(p, ec);
}
inline bool create_directories(const path &p) {
    std::error_code ec;
    const bool created = create_directories(p, ec);
    if (ec) throw filesystem_error("create_directories", p, ec);
    return created;
}

// ---- path queries ---------------------------------------------------------
inline path current_path(std::error_code &ec) {
    ec.clear();
    return path("D:\\"); // the directory the XBE was launched from
}
inline path current_path() { return path("D:\\"); }
inline path weakly_canonical(const path &p) { return p; }
inline path weakly_canonical(const path &p, std::error_code &ec) {
    ec.clear();
    return p;
}
inline bool equivalent(const path &a, const path &b, std::error_code &ec) noexcept {
    ec.clear();
    const auto &x = a.native();
    const auto &y = b.native();
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (detail::fold(x[i]) != detail::fold(y[i])) return false;
    return true;
}
inline bool equivalent(const path &a, const path &b) {
    std::error_code ec;
    return equivalent(a, b, ec);
}

// ---- directory iteration --------------------------------------------------
class directory_entry {
public:
    directory_entry() = default;
    directory_entry(path p, file_status s) : path_(std::move(p)), status_(s) {}
    const path &path() const noexcept { return path_; }
    operator const eb::xbox::fs::path &() const noexcept { return path_; }
    file_status status() const noexcept { return status_; }
    file_status symlink_status() const noexcept { return status_; }
    bool is_regular_file() const noexcept { return status_.type() == file_type::regular; }
    bool is_directory() const noexcept { return status_.type() == file_type::directory; }

private:
    eb::xbox::fs::path path_;
    file_status status_;
};

// Snapshots the directory when constructed (FATX directories are small).
class directory_iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = directory_entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const directory_entry *;
    using reference = const directory_entry &;

    directory_iterator() noexcept = default;
    explicit directory_iterator(const path &directory) {
        std::error_code ec;
        load(directory, ec);
        if (ec) throw filesystem_error("directory_iterator", directory, ec);
    }
    directory_iterator(const path &directory, std::error_code &ec) { load(directory, ec); }

    reference operator*() const { return (*entries_)[index_]; }
    pointer operator->() const { return &(*entries_)[index_]; }
    directory_iterator &operator++() {
        if (entries_ && ++index_ >= entries_->size()) entries_.reset();
        return *this;
    }
    bool operator==(const directory_iterator &o) const noexcept {
        return entries_ == o.entries_ && (!entries_ || index_ == o.index_);
    }
    bool operator!=(const directory_iterator &o) const noexcept { return !(*this == o); }

private:
    void load(const path &directory, std::error_code &ec) {
        ec.clear();
        const std::string pattern = (directory / "*").string();
        WIN32_FIND_DATAA data{};
        HANDLE h = FindFirstFileA(pattern.c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) ec = detail::make_ec(error);
            return;
        }
        auto list = std::make_shared<std::vector<directory_entry>>();
        do {
            const std::string name = data.cFileName;
            if (name == "." || name == "..") continue;
            const bool is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            list->emplace_back(directory / name, file_status(is_dir ? file_type::directory : file_type::regular));
        } while (FindNextFileA(h, &data));
        FindClose(h);
        if (!list->empty()) entries_ = std::move(list);
    }
    std::shared_ptr<std::vector<directory_entry>> entries_;
    std::size_t index_ = 0;
};

inline directory_iterator begin(directory_iterator it) noexcept { return it; }
inline directory_iterator end(const directory_iterator &) noexcept { return directory_iterator(); }

} // namespace eb::xbox::fs
