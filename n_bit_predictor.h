#pragma once

#include <cstdint>

// N-bit predictor
template<int8_t N>
class n_bit_predictor {
public:
    explicit n_bit_predictor() {
        static_assert(N > 0);
        static_assert(N <= sizeof(int16_t) * CHAR_BIT / 2);
        val = static_cast<int16_t>(MAX / 2 + 1);
    }

    [[nodiscard]] bool predict() const {
        return val > N / 2;
    }

    void update(const bool taken) {
        val = std::clamp(val + (taken ? 1 : -1), 0, {MAX});
    }

private:
    static constexpr int16_t MAX = ~static_cast<int16_t>(std::numeric_limits<uint16_t>::max() << N);
    int16_t val;
};
