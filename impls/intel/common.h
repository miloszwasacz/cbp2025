#pragma once

#include <climits>
#include <functional>
#include <iostream>
#include <limits>

#include "../tage_common.h"

namespace intel {
    inline constexpr size_t PHT_COUNT = 3;
    inline constexpr size_t PHT_ASSOC = 4;

    // Based on the observation that only 13 LSBs of the PC affect the tags.
    static constexpr size_t TAG_WIDTH = 13;
    static constexpr size_t IDX_WIDTH = 9;
}

namespace intel::common {
    class phr_t {
    public:
        class phist_t {
            // The number of integers used to store the value.
            static constexpr size_t COUNT = 3;
            static constexpr size_t CHUNK_SIZE = sizeof(uint64_t) * CHAR_BIT;

        public:
            static constexpr size_t SIZE = 93 /* (branch count) */ * 2 /* (bits per branch) */;
            static_assert(SIZE <= COUNT * CHUNK_SIZE && SIZE > (COUNT - 1) * CHUNK_SIZE);

            bool operator==(const phist_t &other) const {
                for (uint8_t i = 0; i < COUNT; ++i) {
                    if (val[i] != other.val[i]) {
                        return false;
                    }
                }
                return true;
            }

            void shift(const uint8_t shift) {
                constexpr uint64_t last_chunk_mask = ~(std::numeric_limits<uint64_t>::max() << (SIZE % CHUNK_SIZE));

                uint64_t carry = 0;
                for (unsigned long &v: val) {
                    const uint64_t shift_mask = std::numeric_limits<uint64_t>::max() << shift;

                    v = std::rotl(v, shift);
                    const uint64_t carry_out = v & ~shift_mask;
                    v = (v & shift_mask) | (carry & ~shift_mask);
                    carry = carry_out;
                }
                val[COUNT - 1] &= last_chunk_mask;
            }

            void xor_lo(const uint64_t other) {
                val[0] ^= other;
            }

            [[nodiscard]] size_t bit_at(const size_t idx) const {
                assert(idx < SIZE && "index out of bounds");
                const uint64_t chunk = val[idx / CHUNK_SIZE];
                return (chunk >> (idx % CHUNK_SIZE)) & 0b1;
            }

        private:
            // The first element of the array is least-significant bitwise.
            uint64_t val[COUNT] = {};
        };

        void update(const uint64_t addr, const uint64_t target) {
            // Step 1: Shift
            reg.shift(2);

            // Step 2: XOR with footprint
            //TODO Different footprints on different uarch
            const uint64_t footprint = (
                                           ((addr >> 3) & 0b11)
                                           | ((addr >> 5) & (0b11 << 2)) // >> 7 - 2
                                           | ((addr >> 7) & (0b11 << 4)) // >> 11 - 4
                                           | ((addr << 1) & (0b11 << 6)) // >> 5 - 6
                                           | ((addr >> 1) & (0b11 << 8)) // >> 9 - 8
                                           | ((addr >> 3) & (0b111111 << 10)) // >> 13 - 10
                                       ) ^ (target & 0b111111);
            reg.xor_lo(footprint);
        }

        [[nodiscard]] const phist_t &value() const {
            return reg;
        }

    private:
        // A shift register that holds the current history. The first element
        // of the tuple is least-significant bitwise.
        phist_t reg{};
    };

    using hist_t = phr_t::phist_t;
    using base_pred_t = tage::common::base_pred_t;
    using pred_info_t = tage::common::pred_info_t<hist_t>;

    // A 4-way set associative table of predictions.
    class pht_t : public tage::common::pht_t<hist_t, IDX_WIDTH, PHT_ASSOC, PHT_COUNT, TAG_WIDTH> {
    public:
        explicit pht_t(const size_t level)
            : tage::common::pht_t<phr_t::phist_t, IDX_WIDTH, PHT_ASSOC, PHT_COUNT, TAG_WIDTH>(level) {
        }

    protected:
        [[nodiscard]] size_t fold_hist(const hist_t &hist) const {
            std::function<ssize_t(ssize_t i)> even_start, odd_start;
            ssize_t i_hi, i_lo, j_hi, j_lo;
            switch (level) {
                case 1:
                    even_start = [](ssize_t) {
                        return 20;
                    };
                    odd_start = [](ssize_t) {
                        return 15;
                    };
                    i_hi = i_lo = j_hi = j_lo = 0;
                    break;
                case 2:
                    even_start = [](const ssize_t i) {
                        return 16 * i + 8;
                    };
                    odd_start = [](const ssize_t j) {
                        return 16 * j + 1;
                    };
                    i_hi = 3;
                    i_lo = 1;
                    j_hi = 3;
                    j_lo = 0;
                    break;
                case 3:
                    even_start = [](const ssize_t i) {
                        return 16 * i + 8;
                    };
                    odd_start = [](const ssize_t j) {
                        return 16 * j + 1;
                    };
                    i_hi = 11;
                    i_lo = 1;
                    j_hi = 11;
                    j_lo = 0;
                    break;
                default:
                    std::cerr << "unimplemented index hash function" << std::endl;
                    std::abort();
            }

            constexpr uint8_t FOLD_LEN = 8;
            size_t folded = 0;
            for (uint8_t offset = 0; offset < FOLD_LEN; offset++) {
                folded <<= 1;
                const auto even_bit = [offset, even_start](const ssize_t i) {
                    return even_start(i) - static_cast<ssize_t>(2 * offset);
                };
                const auto odd_bit = [offset, odd_start](const ssize_t j) {
                    return odd_start(j) - static_cast<ssize_t>(2 * offset);
                };
                size_t f = 0;
                for (ssize_t i = i_hi; i >= i_lo; --i) {
                    f ^= hist.bit_at(even_bit(i));
                }
                for (ssize_t j = j_hi; j >= j_lo && odd_bit(j) >= 0; --j) {
                    f ^= hist.bit_at(odd_bit(j));
                }
                folded |= f & 0b1;
            }
            return folded;
        }

        [[nodiscard]] size_t index(const hist_t &hist, const uint64_t pc) const override {
            size_t idx = (pc >> 5) & 0b1;
            idx <<= IDX_WIDTH - 1;
            idx |= fold_hist(hist);
            return idx;
        }

        [[nodiscard]] size_t tag(const phr_t::phist_t &hist, const uint64_t pc) const override {
            size_t tag = pc & ((1 << TAG_WIDTH) - 1);
            tag ^= fold_hist(hist);
            return tag;
        }
    };

    class IntelBase : public tage::common::TageBase<hist_t, pht_t, PHT_COUNT> {
    public:
        //TODO Different threshold?
        explicit IntelBase() : TageBase(tage::common::U_RESET_THRESHOLD) {
        }

    protected:
        [[nodiscard]] size_t size() const override {
            return phr_t::phist_t::SIZE + TageBase::size();
        }
    };
}
