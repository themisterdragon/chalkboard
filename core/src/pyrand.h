// Python's random.Random, exactly: seeded from a string the way Python 3 does it (the text plus its
// SHA-512), Mersenne Twister underneath, and the same shuffle. Version A/B/C of a quiz must come out
// in the same order the Python Chalkboard printed it, or an old answer key stops matching.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class PyRandom {
public:
    explicit PyRandom(const std::string &seed);
    uint32_t next32();
    uint64_t getrandbits(int k);         // k <= 64
    uint64_t randbelow(uint64_t n);      // 0 <= r < n (n > 0)
    template <class T> void shuffle(std::vector<T> &x) {
        for (size_t i = x.size(); i-- > 1;) {
            size_t j = (size_t)randbelow(i + 1);
            std::swap(x[i], x[j]);
        }
    }

private:
    uint32_t mt[624];
    int idx = 625;
    void init_genrand(uint32_t s);
    void init_by_array(const std::vector<uint32_t> &key);
};

std::string sha512(const std::string &data);   // 64 raw bytes
// Random bytes from the operating system (new ids)
std::string os_random(size_t n);
