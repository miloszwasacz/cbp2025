#pragma once

#include <cstdint>

// N-bit usefulness counter
template<int8_t N>
class u_ctr {
public:
    u_ctr() {
        static_assert(N > 0);
        static_assert(N <= sizeof(int16_t) * CHAR_BIT / 2);
        val = 0;
    }

    void update(const bool taken) {
        val = std::clamp(val + (taken ? 1 : -1), 0, {MAX});
    }

    [[nodiscard]] int16_t value() const {
        return val;
    }

    void age(const bool clear_msb) {
        const uint8_t clr_shift = clear_msb ? N - 1 : 0;
        val &= ~(0b1 << clr_shift);
    }

private:
    static constexpr int16_t MAX = ~static_cast<int16_t>(std::numeric_limits<uint16_t>::max() << N);
    int16_t val;
};
