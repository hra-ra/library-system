#pragma once
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <openssl/evp.h>

// Small helpers: ISO date maths and password hashing.
namespace util {

inline std::tm parseDate(const std::string& iso) {
    std::tm tm{};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    tm.tm_hour = 12;  // noon avoids any DST edge cases
    return tm;
}

inline long dayNumber(const std::string& iso) {
    std::tm tm = parseDate(iso);
    return static_cast<long>(timegm(&tm) / 86400);
}

inline int daysBetween(const std::string& from, const std::string& to) {
    return static_cast<int>(dayNumber(to) - dayNumber(from));
}

inline std::string formatDate(std::time_t t) {
    std::tm out{};
    gmtime_r(&t, &out);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &out);
    return buf;
}

// LIBRARY_TODAY lets us pretend it is another day (handy for demos and tests).
inline std::string today() {
    if (const char* fake = std::getenv("LIBRARY_TODAY")) return fake;
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_r(&t, &lt);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

inline std::string addDays(const std::string& iso, int n) {
    std::tm tm = parseDate(iso);
    std::time_t t = timegm(&tm) + static_cast<std::time_t>(n) * 86400;
    return formatDate(t);
}

inline std::string toHex(const unsigned char* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0f];
    }
    return out;
}

inline std::string sha256Hex(const std::string& s) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(s.data(), s.size(), md, &len, EVP_sha256(), nullptr);
    return toHex(md, len);
}

inline std::string randomHex(size_t bytes) {
    std::random_device rd;
    std::string raw(bytes, '\0');
    for (auto& c : raw) c = static_cast<char>(rd() & 0xff);
    return toHex(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
}

}  // namespace util
