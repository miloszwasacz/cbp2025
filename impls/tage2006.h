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
        static constexpr size_t UPDATE_SHIFT = 1;

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
            reg.first.shift(UPDATE_SHIFT);
            reg.first.or_lsb(taken);

            // Update PHIST
            reg.second <<= UPDATE_SHIFT;
            reg.second |= addr & ((1 << UPDATE_SHIFT) - 1);
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
        using folded_hist_t = size_t;
        static constexpr size_t TAG_WIDTH = intel::TAG_WIDTH;
        static constexpr size_t IDX_WIDTH = intel::IDX_WIDTH;
        // We use the same tag and index widths as the intel to make a fair comparison.
        static_assert(sizeof(folded_hist_t) * CHAR_BIT >= TAG_WIDTH);
        static_assert(sizeof(folded_hist_t) * CHAR_BIT >= IDX_WIDTH);

    public:
        explicit pht_t(const size_t level) : common::pht_t<hist_t>(level) {
            switch (level) {
                case 1:
                    hist_len = 10;
                    break;
                case 2:
                    hist_len = 35;
                    break;
                case 3:
                    hist_len = ghr_t::ghist_t::SIZE;
                    break;
                default:
                    assert(false && "unimplemented level");
            }
        }

        void update_folded_ghist(const hist_t &new_ghist, const bool taken) {
            auto update = [this, taken](folded_hist_t &fold, const size_t width) {
                assert(width < sizeof(folded_hist_t) * CHAR_BIT);

                const auto carry = (fold >> (width - 1)) & 0b1;
                fold = ((fold << 1) | carry) & ((1 << width) - 1); // ROTL
                fold ^= taken;
                fold ^= (next_evicted_bit << (hist_len % width));
            };
            update(idx_fold, IDX_WIDTH);
            update(tag_fold[0], TAG_WIDTH);
            update(tag_fold[1], TAG_WIDTH - 1);

            next_evicted_bit = new_ghist.first.bits_at(ghr_t::ghist_t::SIZE - 1, 1) & 0b1;
        }

    protected:
        static void fold_phist_into(size_t &val, const hist_t &hist, const size_t mask) {
            ghr_t::phist_t phist = hist.second;
            for (size_t i = 0; i < ghr_t::PHIST_SIZE; i += IDX_WIDTH) {
                val ^= phist & mask;
                phist >>= IDX_WIDTH;
            }
        }

        [[nodiscard]] size_t index(const hist_t &hist, const uint64_t pc) const override {
            constexpr size_t MASK = (1 << IDX_WIDTH) - 1;

            size_t idx = 0;

            // Folded (partial) PC
            {
                constexpr size_t chunk_count = 2;
                uint64_t folding_pc = pc;
                for (size_t i = 0; i < chunk_count; ++i) {
                    idx ^= folding_pc & MASK;
                    folding_pc >>= IDX_WIDTH;
                }
            }

            // Folded PHIST
            fold_phist_into(idx, hist, MASK);

            // Folded GHIST
            idx ^= idx_fold;

            return idx;
        }

        [[nodiscard]]
        size_t tag(const std::pair<ghr_t::ghist_t, unsigned short> &hist, const uint64_t pc) const override {
            constexpr size_t MASK = (1 << IDX_WIDTH) - 1;

            size_t tag = 0;

            // Folded (partial) PC
            tag ^= pc & MASK;

            // Folded PHIST
            fold_phist_into(tag, hist, MASK);

            // Folded GHIST
            tag ^= tag_fold[0];
            tag ^= tag_fold[1] << 1;

            return tag;
        }

    private:
        size_t hist_len;
        folded_hist_t idx_fold = 0;
        std::array<folded_hist_t, 2> tag_fold = {};
        folded_hist_t next_evicted_bit = 0;
    };

    // Academic TAGE Conditional Branch Predictor from the 2006 paper.
    class TAGE2006CBP : public common::TageBase<hist_t, pht_t> {
    public:
        explicit TAGE2006CBP() : TageBase(common::U_RESET_THRESHOLD) {
        }

        [[nodiscard]] const char *name() const override {
            return "TAGE2006";
        }

        void history_update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool taken,
                            const uint64_t nextPC) override {
            ghr.update(taken, PC);
            for (auto &pht: phts) {
                pht.update_folded_ghist(ghr.value(), taken);
            }
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
