#pragma once

#include <iostream>
#include <concepts>
#include <vector>
#include <functional>
#include <ranges>
#include <map>

#include "../tage_common.h"

namespace arm::common {
    using idx_t = size_t;
    using tag_t = uint16_t;

    class base_pred_t {
    public:
        static constexpr size_t LOG_SIZE = 13;
        static constexpr size_t ENTRY_WIDTH = 2;
        // Log of how many entries share the same hysteresis.
        static constexpr size_t LOG_HYST_SHARE = 0;

        static constexpr size_t SIZE = 1 << LOG_SIZE;

        explicit base_pred_t() : dir(), hyst() {
            dir.fill(true);
            hyst.fill(0);
        }

        [[nodiscard]] bool predict(const uint64_t PC) const {
            constexpr size_t MASK = SIZE - 1;
            return dir[(PC >> 2) & MASK];
        }

        void update(const uint64_t PC, const bool correct) {
            constexpr uint8_t HYST_MAX = (1 << HYST_WIDTH) - 1;
            auto &d = dir[(PC >> 2) & (SIZE - 1)];
            auto &h = hyst[(PC >> 2) & (HYST_SIZE - 1)];

            if (correct) {
                h = h < HYST_MAX ? h + 1 : HYST_MAX;
            } else if (h == 0) {
                d = !d;
            } else {
                h--;
            }
        }

    private:
        static constexpr size_t HYST_SIZE = 1 << (LOG_SIZE - LOG_HYST_SHARE);
        static constexpr size_t HYST_WIDTH = ENTRY_WIDTH - 1;
        static_assert(HYST_WIDTH <= sizeof(uint8_t) * CHAR_BIT);

        std::array<bool, SIZE> dir;
        std::array<uint8_t, HYST_SIZE> hyst;
    };

    template<size_t PHRT_SIZE, size_t PHRB_SIZE>
    struct hist_t {
        void update(const uint64_t branch, const uint64_t target) {
            // Update PHRT
            phrt.shift(1);
            constexpr uint64_t TMASK = (1 << (31 - 2 + 1)) - 1;
            phrt.xor_low((target >> 2) & TMASK);

            // Update PHRB
            phrb.shift(1);
            constexpr uint64_t BMASK = (1 << (5 - 2 + 1)) - 1;
            phrb.xor_low((branch >> 2) & BMASK);
        }

        template<size_t SIZE>
        class phr_t {
        public:
            template<std::ranges::input_range R>
                requires std::same_as<std::ranges::range_value_t<R>, size_t>
            uint8_t fold_bits(R &&indices) const {
                uint8_t folded = 0;
                for (auto &&i: indices) {
                    folded ^= bit_at(i);
                }
                return folded;
            }

            [[nodiscard]] uint8_t bit_at(const size_t idx) const {
                const uint64_t chunk = reg[idx / CHUNK_SIZE];
                return (chunk >> (idx % CHUNK_SIZE)) & 0b1;
            }

            [[nodiscard]] uint64_t low() const {
                return reg[0];
            }

            void shift(const uint8_t amount) {
                const uint64_t mask = (1 << amount) - 1;
                uint64_t carry = 0;
                for (auto &chunk: reg) {
                    chunk = std::rotl(chunk, amount);
                    const uint64_t new_carry = chunk & mask;
                    chunk &= ~mask;
                    chunk |= carry;
                    carry = new_carry;
                }
                reg[reg.size() - 1] &= LAST_CHUNK_MASK;
            }

            void xor_low(const uint64_t other) {
                reg[0] ^= other;
                reg[reg.size() - 1] &= LAST_CHUNK_MASK;
            }

        private:
            static constexpr size_t CHUNK_SIZE = sizeof(uint64_t) * CHAR_BIT;
            static constexpr uint64_t LAST_CHUNK_MASK = (uint64_t{1} << (SIZE % CHUNK_SIZE)) - 1;

            std::array<uint64_t, SIZE / CHUNK_SIZE + 1> reg{};
        };

        phr_t<PHRT_SIZE> phrt{};
        phr_t<PHRB_SIZE> phrb{};
    };

    using pred_t = std::pair<bool, size_t>;

    template<typename Hist>
    struct pred_info_t {
        pred_t pred;
        pred_t altpred;
        Hist hist;
    };

    template<typename Hist>
    class pht_t {
    public:
        using idx_fn_t = std::function<idx_t(uint64_t PC, const Hist &hist)>;
        using tag_hist_fold_fn_t = std::function<tag_t(const Hist &hist)>;
        using index_range_t = std::ranges::stride_view<std::ranges::iota_view<size_t, size_t> >;

        explicit pht_t(const size_t assoc, const size_t log_size, idx_fn_t index, tag_hist_fold_fn_t fold)
            : entries(make_entries(log_size, assoc)), make_idx(std::move(index)), tag_fold_hist(std::move(fold)) {
        }

        virtual ~pht_t() = default;

        [[nodiscard]] std::optional<bool> predict(const uint64_t PC, const Hist &hist) const {
            const auto idx = make_idx(PC, hist);
            const auto tag = make_tag(PC, hist);
            for (const auto &way: entries[idx]) {
                if (way.tag == tag) {
                    return {way.dir.predict()};
                }
            }

            return {};
        }

        void update_provider(const uint64_t pc, const pred_info_t<Hist> &info, const bool predDir,
                             const bool resolveDir) {
            const tag_t tag = make_tag(pc, info.hist);
            for (auto &ways = entries[make_idx(pc, info.hist)]; auto &entry: ways) {
                if (entry.tag != tag) continue;

                // Update usefulness counter `u`
                if (info.pred.first != info.altpred.first) {
                    entry.u.update(info.pred.first == resolveDir);
                }

                // Update prediction
                entry.dir.update(resolveDir);

                return;
            }
        }

        [[nodiscard]] bool can_allocate(const uint64_t pc, const Hist &hist) const {
            return std::ranges::any_of(entries[make_idx(pc, hist)], can_allocate_entry);
        }

        void allocate(const uint64_t pc, const Hist &hist) {
            auto &ways = entries[make_idx(pc, hist)];
            const auto entry = std::ranges::find_if(ways, can_allocate_entry);
            assert(entry != ways.end());
            *entry = entry_t(make_tag(pc, hist));
        }

        void decrement_us() {
            for (auto &ways: entries) {
                for (auto &entry: ways) {
                    entry.u.update(false);
                }
            }
        }

        void age_us(const bool clear_msb) {
            for (auto &ways: entries) {
                for (auto &entry: ways) {
                    entry.u.age(clear_msb);
                }
            }
        }

    protected:
        [[nodiscard]] virtual tag_t make_tag(uint64_t PC, const Hist &hist) const = 0;

        tag_hist_fold_fn_t tag_fold_hist;

    private:
        struct entry_t {
            n_bit_predictor<2> dir;
            tag_t tag;
            tage::common::u_ctr<1> u;

            explicit entry_t() : entry_t(0) {
            }

            explicit entry_t(const tag_t tag) : tag(tag) {
            }
        };

        static std::vector<std::vector<entry_t> > make_entries(const size_t log_size, const size_t assoc) {
            const entry_t entry{};
            const std::vector ways(assoc, entry);
            std::vector entries(1 << log_size, ways);
            return entries;
        }

        static bool can_allocate_entry(const entry_t &entry) {
            return entry.u.value() == 0;
        }

        std::vector<std::vector<entry_t> > entries;
        idx_fn_t make_idx;
    };

    template<typename Hist, typename PHT>
    class ArmBase {
        static constexpr size_t PHT_COUNT = 6;
        static constexpr size_t BASE_PRED_NUMBER = 0;

    protected:
        using phts_t = std::array<PHT, PHT_COUNT>;

    public:
        explicit ArmBase(phts_t phts) : phts(std::move(phts)) {
        }

        virtual ~ArmBase() = default;

        [[nodiscard]] virtual const char *name() const = 0;

        virtual void setup() {
            std::cout << "Testing " << name() << " CBP" << std::endl;
        }

        virtual void terminate() {
        }

        [[nodiscard]] bool predict(const uint64_t seq_no, const uint8_t piece, const uint64_t PC) {
            const auto id = get_unique_inst_id(seq_no, piece);
            const auto &hist = phr;

            pred_t pred = {base.predict(PC), BASE_PRED_NUMBER};
            pred_t altpred = pred;

            for (size_t i = 0; i < PHT_COUNT; i++) {
                const auto &table = phts[PHT_COUNT - i - 1];
                const auto out = table.predict(PC, hist);
                if (!out.has_value()) continue;

                const pred_t p = {out.value(), PHT_COUNT - i};
                if (pred.second == BASE_PRED_NUMBER) {
                    pred = p;
                } else if (altpred.second == BASE_PRED_NUMBER) {
                    altpred = p;
                    break;
                }
            }

            spec_pred_info.insert({id, {pred, altpred, hist}});

            return pred.first;
        }

        void history_update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool taken,
                            const uint64_t nextPC) {
            if (taken) {
                phr.update(PC, nextPC);
            }
        }

        void track_other_inst(const uint64_t PC, const InstClass instClass, const bool predDir, const bool resolveDir,
                              const uint64_t nextPC) {
            switch (instClass) {
                case InstClass::uncondDirectBranchInstClass:
                case InstClass::uncondIndirectBranchInstClass:
                case InstClass::callDirectInstClass:
                case InstClass::callIndirectInstClass:
                case InstClass::ReturnInstClass:
                    assert(resolveDir && "unconditional branches and calls should always be taken");
                    phr.update(PC, nextPC);
                    break;
                case InstClass::condBranchInstClass:
                    assert(false && "should be handled by history_update");
                default:
                    assert(false);
            }
        }

        void update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool resolveDir,
                    const bool predDir, const uint64_t nextPC) {
            const auto id = get_unique_inst_id(seq_no, piece);
            const auto &info = spec_pred_info[id];

            // Update provider component
            if (info.pred.second == BASE_PRED_NUMBER) {
                base.update(PC, predDir == resolveDir);
            } else {
                phts[info.pred.second - 1].update_provider(PC, info, predDir, resolveDir);
            }

            if (resolveDir != predDir) {
                // Find a new entry in further table to allocate
                size_t next = -1;
                static_assert(PHT_COUNT < static_cast<size_t>(-1));
                for (size_t i = info.pred.second; i < PHT_COUNT; ++i) {
                    if (phts[i].can_allocate(PC, info.hist)) {
                        next = i;
                        break;
                    }
                }

                if (next == -1) {
                    // No entries can be allocated, decrement all `u`s
                    for (size_t i = info.pred.second; i < PHT_COUNT; ++i) {
                        phts[i].decrement_us();
                    }
                    return;
                }

                phts[next].allocate(PC, info.hist);
            }

            age_us();
        }

    protected:
        using inst_id_t = uint64_t;

        static constexpr PHT::index_range_t bit_indices(const size_t fst, const size_t snd, const size_t end) {
            using std::views::iota;
            using std::views::stride;
            return iota(fst, end + 1) | stride(snd - fst);
        }

        static inst_id_t get_unique_inst_id(const uint64_t seq_no, const uint8_t piece) {
            assert(piece < 16);
            return (seq_no << 4) | (piece & 0x000F);
        }

        void age_us() {
            br_ctr++;
            if (br_ctr >= u_reset_threshold) {
                br_ctr = 0;
                for (auto &pht: phts) {
                    pht.age_us(u_reset_msb);
                }
                u_reset_msb = !u_reset_msb;
            }
        }

        Hist phr;
        base_pred_t base;
        phts_t phts;
        std::map<inst_id_t, pred_info_t<Hist> > spec_pred_info;

        uint64_t br_ctr = 0;
        bool u_reset_msb = true;
        static constexpr uint64_t u_reset_threshold = tage::common::U_RESET_THRESHOLD;
    };
}
