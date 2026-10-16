#include "zipfile.h"

#include <ctime>

#include "zlib.h"

namespace zipfile {

static void u16(std::string &s, uint32_t v) {
    s += (char)(v & 0xFF);
    s += (char)((v >> 8) & 0xFF);
}

static void u32(std::string &s, uint32_t v) {
    u16(s, v & 0xFFFF);
    u16(s, v >> 16);
}

static void dos_now(uint16_t &time_, uint16_t &date) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    time_ = (uint16_t)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    date = (uint16_t)(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}

void Writer::add(const std::string &name, const std::string &data, bool compress) {
    std::string body = compress ? zlib::deflate_raw(data) : data;
    uint16_t method = compress ? 8 : 0;
    if (compress && body.size() >= data.size()) {   // stored is smaller: keep it plain
        body = data;
        method = 0;
    }
    uint16_t t, d;
    dos_now(t, d);
    Entry e{name, zlib::crc32(data), (uint32_t)body.size(), (uint32_t)data.size(), (uint32_t)out_.size(), method};
    u32(out_, 0x04034b50);
    u16(out_, 20);
    u16(out_, 0x0800);   // names are UTF-8
    u16(out_, method);
    u16(out_, t);
    u16(out_, d);
    u32(out_, e.crc);
    u32(out_, e.csize);
    u32(out_, e.usize);
    u16(out_, (uint32_t)name.size());
    u16(out_, 0);
    out_ += name;
    out_ += body;
    entries_.push_back(e);
}

std::string Writer::finish() {
    uint32_t start = (uint32_t)out_.size();
    uint16_t t, d;
    dos_now(t, d);
    for (const Entry &e : entries_) {
        u32(out_, 0x02014b50);
        u16(out_, 0x0314);   // made by: Unix, zip 2.0
        u16(out_, 20);
        u16(out_, 0x0800);
        u16(out_, e.method);
        u16(out_, t);
        u16(out_, d);
        u32(out_, e.crc);
        u32(out_, e.csize);
        u32(out_, e.usize);
        u16(out_, (uint32_t)e.name.size());
        u16(out_, 0);
        u16(out_, 0);
        u16(out_, 0);
        u16(out_, 0);
        u32(out_, 0600u << 16);   // -rw-------
        u32(out_, e.offset);
        out_ += e.name;
    }
    uint32_t size = (uint32_t)out_.size() - start;
    u32(out_, 0x06054b50);
    u16(out_, 0);
    u16(out_, 0);
    u16(out_, (uint32_t)entries_.size());
    u16(out_, (uint32_t)entries_.size());
    u32(out_, size);
    u32(out_, start);
    u16(out_, 0);
    return out_;
}

// bounds-checked: a damaged file can claim any offset (0 past the end; callers check sizes first)
static uint32_t rd16(const std::string &s, uint64_t o) { return o + 2 <= s.size() ? ((unsigned char)s[o] | ((unsigned char)s[o + 1] << 8)) : 0; }
static uint32_t rd32(const std::string &s, uint64_t o) { return rd16(s, o) | (rd16(s, o + 2) << 16); }

bool read(const std::string &z, std::vector<std::pair<std::string, std::string>> &out, std::string *err) {
    auto fail = [&](const char *m) {
        if (err) *err = m;
        return false;
    };
    if (z.size() < 22) return fail("not a zip file");
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i <= 22 + 65535;)
        if (rd32(z, i) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) return fail("not a zip file");
    uint32_t count = rd16(z, eocd + 10);
    uint64_t p = rd32(z, eocd + 16);   // 64-bit sums from here on: no offset in the file can wrap around
    for (uint32_t k = 0; k < count; k++) {
        if (p + 46 > z.size() || rd32(z, p) != 0x02014b50) return fail("damaged zip directory");
        uint32_t method = rd16(z, p + 10), csize = rd32(z, p + 20), usize = rd32(z, p + 24);
        uint64_t nlen = rd16(z, p + 28), xlen = rd16(z, p + 30), clen = rd16(z, p + 32), off = rd32(z, p + 42);
        if (p + 46 + nlen > z.size()) return fail("damaged zip directory");
        std::string name = z.substr((size_t)p + 46, (size_t)nlen);
        p += 46 + nlen + xlen + clen;
        if (off + 30 > z.size() || rd32(z, off) != 0x04034b50) return fail("damaged zip entry");
        uint64_t data = off + 30 + rd16(z, off + 26) + rd16(z, off + 28);
        if (data + csize > z.size()) return fail("damaged zip entry");
        std::string body = z.substr((size_t)data, csize);
        if (method == 8) body = zlib::inflate_raw(body, (size_t)usize + 1);
        else if (method != 0) return fail("unsupported compression in zip");
        out.push_back({name, body});
    }
    return true;
}

}  // namespace zipfile
