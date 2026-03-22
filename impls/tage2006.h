#pragma once

#include "tage_common.h"
#include "intel/common.h"

//TODO: Different uarch apart from Skylake (e.g. Alder Lake, Haswell, etc.)
namespace tage {
    using common::PHT_COUNT;

    class ghr_t {
    public:
        using phist_t = uint16_t;
        static constexpr size_t PHIST_SIZE = sizeof(phist_t) * CHAR_BIT;

        class ghist_t {
            // The number of integers used to store the value.
            static constexpr size_t COUNT = 3;
            static constexpr size_t CHUNK_SIZE = sizeof(uint64_t) * CHAR_BIT;

        public:
            // To make a fair comparison with the Intel CBP in terms of memory usage,
            // the size of GHIST + PHIST is the same as PHIST in the Intel counterpart.
            static constexpr size_t SIZE = intel::common::phr_t::phist_t::SIZE - PHIST_SIZE;
            static_assert(SIZE <= COUNT * CHUNK_SIZE && SIZE > (COUNT - 1) * CHUNK_SIZE);

            bool operator==(const ghist_t &other) const {
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

            void or_lsb(const uint64_t bit) {
                val[0] |= bit & 0b1;
            }

            [[nodiscard]] size_t bits_at(const size_t idx, size_t len) const {
                assert(idx < SIZE && "index out of bounds");
                assert(len <= CHUNK_SIZE);
                if (idx + len >= SIZE) {
                    len = SIZE - idx;
                }
                const size_t start = idx / CHUNK_SIZE;
                const size_t end = (idx + len - 1) / CHUNK_SIZE;

                if (start == end) {
                    const uint64_t chunk = val[start];
                    const uint64_t mask = ~(std::numeric_limits<uint64_t>::max() << len);
                    return (chunk >> (idx % CHUNK_SIZE)) & mask;
                } else {
                    const uint64_t chunk_start = val[start];
                    const uint64_t chunk_end = val[end];

                    const size_t len_bot = CHUNK_SIZE - idx;
                    const size_t bot = chunk_start >> (idx % CHUNK_SIZE);
                    const size_t top = chunk_end;

                    const uint64_t mask = ~(std::numeric_limits<uint64_t>::max() << len);
                    return ((top << len_bot) | bot) & mask;
                }
            }

        private:
            // The first element of the array is least-significant bitwise.
            uint64_t val[COUNT] = {};
        };

        void update(const bool taken, const uint64_t addr) {
            // Update GHIST
            reg.first.shift(1);
            reg.first.or_lsb(taken);

            // Update PHIST
            reg.second <<= 1;
            reg.second |= addr & 0b1;
        }

        [[nodiscard]] const std::pair<ghist_t, phist_t> &value() const {
            return reg;
        }

    private:
        // Shift registers that hold the current global history and path history.
        // The first element of GHIST is least-significant bitwise.
        std::pair<ghist_t, phist_t> reg{{}, 0};
    };

    using hist_t = std::pair<ghr_t::ghist_t, ghr_t::phist_t>;

    class pht_t : public common::pht_t<hist_t> {
    public:
        explicit pht_t(const size_t level) : common::pht_t<hist_t>(level) {
        }

    protected:
        [[nodiscard]] size_t index(const hist_t &hist, const uint64_t pc) const override {
            // We use an index with the same length as the Intel CBP.
            constexpr size_t CHUNK_SIZE = common::PHT_SIZE_POW;
            constexpr u_int64_t CHUNK_MASK = (1 << CHUNK_SIZE) - 1;

            size_t idx = 0;

            // PC folding
            {
                constexpr size_t chunk_count = 2;
                uint64_t folding_pc = pc;
                for (size_t i = 0; i < chunk_count; ++i) {
                    idx ^= folding_pc & CHUNK_MASK;
                    folding_pc >>= CHUNK_SIZE;
                }
            }

            // Simple PHIST folding
            {
                ghr_t::phist_t phist = hist.second;
                for (size_t i = 0; i < ghr_t::PHIST_SIZE; i += CHUNK_SIZE) {
                    idx ^= phist & CHUNK_MASK;
                    phist >>= CHUNK_SIZE;
                }
            }

            // Simple GHIST folding
            {
                size_t chunk_count;
                switch (level) {
                    case 1: {
                        chunk_count = 2;
                        break;
                    }
                    case 2: {
                        chunk_count = 6;
                        break;
                    }
                    case 3: {
                        constexpr size_t c = ghr_t::ghist_t::SIZE / CHUNK_SIZE;
                        if constexpr (ghr_t::ghist_t::SIZE % CHUNK_SIZE != 0) {
                            // ReSharper disable once CppDFAUnreachableCode
                            chunk_count = c + 1;
                        } else {
                            // ReSharper disable once CppDFAUnreachableCode
                            chunk_count = c;
                        }
                        break;
                    }
                    default: {
                        assert(false);
                    }
                }
                for (size_t i = 0; i < chunk_count; ++i) {
                    idx ^= hist.first.bits_at(i * CHUNK_SIZE, CHUNK_SIZE);
                }
            }

            return idx;
        }
    };

    // Academic TAGE Conditional Branch Predictor from the 2006 paper.
    class TAGE2006CBP : public common::TageBase<hist_t, pht_t> {
    public:
        explicit TAGE2006CBP() : TageBase(common::U_RESET_THRESHOLD) {
            std::cout << "Testing TAGE 2006 CBP" << std::endl;
        }

        void history_update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool taken,
                            const uint64_t nextPC) override {
            ghr.update(taken, PC);
        }

        void track_other_inst(const uint64_t PC, const InstClass instClass, const bool predDir, const bool resolveDir,
                              const uint64_t nextPC) override {
            switch (instClass) {
                case InstClass::uncondDirectBranchInstClass:
                case InstClass::uncondIndirectBranchInstClass:
                case InstClass::callDirectInstClass:
                case InstClass::callIndirectInstClass:
                case InstClass::ReturnInstClass:
                    assert(resolveDir && "unconditional branches and calls should always be taken");
                    ghr.update(true, PC);
                    break;
                case InstClass::condBranchInstClass:
                    assert(false && "should be handled by history_update");
                default:
                    assert(false);
            }
        }

    protected:
        const hist_t &get_hist() override {
            return ghr.value();
        }

    private:
        ghr_t ghr;
    };
}
