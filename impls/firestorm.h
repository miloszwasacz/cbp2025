#pragma once

#include <filesystem>
#include <vector>
#include <functional>
#include <ranges>
#include <map>

#include "tage_common.h"

namespace firestorm {
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

    struct hist_t {
        static constexpr size_t PHRT_SIZE = 100;
        static constexpr size_t PHRB_SIZE = 28;

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

    struct pred_info_t {
        pred_t pred;
        pred_t altpred;
        hist_t hist;
    };

    class pht_t {
    public:
        using idx_fn_t = std::function<idx_t(uint64_t PC, const hist_t &hist)>;
        using tag_hist_fold_fn_t = std::function<tag_t(const hist_t &hist)>;
        using index_range_t = std::ranges::stride_view<std::ranges::iota_view<size_t, size_t> >;

        explicit pht_t(const size_t assoc, const size_t log_size, idx_fn_t index, tag_hist_fold_fn_t fold)
            : entries(make_entries(log_size, assoc)), make_idx(std::move(index)), tag_fold_hist(std::move(fold)) {
        }

        [[nodiscard]] std::optional<bool> predict(const uint64_t PC, const hist_t &hist) const {
            const auto idx = make_idx(PC, hist);
            const auto tag = make_tag(PC, hist);
            for (const auto &way: entries[idx]) {
                if (way.tag == tag) {
                    return {way.dir.predict()};
                }
            }

            return {};
        }

        void update_provider(const uint64_t pc, const pred_info_t &info, const bool predDir, const bool resolveDir) {
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

        [[nodiscard]] bool can_allocate(const uint64_t pc, const hist_t &hist) const {
            return std::ranges::any_of(entries[make_idx(pc, hist)], can_allocate_entry);
        }

        void allocate(const uint64_t pc, const hist_t &hist) {
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

        [[nodiscard]] tag_t make_tag(const uint64_t PC, const hist_t &hist) const {
            constexpr uint8_t FOLD_WIDTH = 12;
            constexpr tag_t FOLD_MASK = (1 << FOLD_WIDTH) - 1;
            constexpr uint8_t INDIV_START = 2;
            constexpr uint8_t INDIV_WIDTH = 5 - INDIV_START + 1;
            constexpr tag_t INDIV_MASK = (1 << INDIV_WIDTH) - 1;
            constexpr uint8_t PC_XOR_START = 7;

            const tag_t hist_fold = tag_fold_hist(hist);
            tag_t tag = ((PC >> INDIV_START) & INDIV_MASK) << FOLD_WIDTH;
            tag |= (PC >> PC_XOR_START) & FOLD_MASK;
            tag ^= hist_fold & FOLD_MASK;
            return tag;
        }

        static bool can_allocate_entry(const entry_t &entry) {
            return entry.u.value() == 0;
        }

        std::vector<std::vector<entry_t> > entries;
        idx_fn_t make_idx;
        tag_hist_fold_fn_t tag_fold_hist;
    };

    class FirestormCBP final {
        static constexpr size_t PHT_COUNT = 6;
        using phts_t = std::array<pht_t, PHT_COUNT>;
        static constexpr size_t BASE_PRED_NUMBER = 0;

    public:
        explicit FirestormCBP() : phts(make_phts()) {
            std::cout << "Testing Firestorm CBP" << std::endl;
        }

        void setup() {
        }

        void terminate() {
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

    private:
        using inst_id_t = uint64_t;

        static constexpr pht_t::index_range_t bit_indices(const size_t fst, const size_t snd, const size_t end) {
            using std::views::iota;
            using std::views::stride;
            return iota(fst, end + 1) | stride(snd - fst);
        }

        [[nodiscard]] static phts_t make_phts() {
#define PC(IDX) (((pc) >> IDX) & 0b1)
#define PHRT(IDX) hist.phrt.bit_at(IDX)
#define PHRB(IDX) hist.phrb.bit_at(IDX)
#define XOR(P1, P2, P3) (P1 ^ P2 ^ P3)
#define XOR2(P1, P2) (P1 ^ P2)
#define PUSH_BIT(idx, B) { \
    idx <<= 1;\
    idx |= B;\
}
#define IDX_FN(B0, B1, B2, B3, B4, B5, B6, B7, B8, B9) \
    [](const uint64_t pc, const hist_t &hist) { \
        idx_t idx = B9; \
        PUSH_BIT(idx, B8); \
        PUSH_BIT(idx, B7); \
        PUSH_BIT(idx, B6); \
        PUSH_BIT(idx, B5); \
        PUSH_BIT(idx, B4); \
        PUSH_BIT(idx, B3); \
        PUSH_BIT(idx, B2); \
        PUSH_BIT(idx, B1); \
        PUSH_BIT(idx, B0); \
        return idx; \
    }
#define IDX_FN2(B0, B1, B2, B3, B4, B5, B6, B7, B8, B9, B10) \
    [](const uint64_t pc, const hist_t &hist) { \
        idx_t idx = B10; \
        PUSH_BIT(idx, B9); \
        PUSH_BIT(idx, B8); \
        PUSH_BIT(idx, B7); \
        PUSH_BIT(idx, B6); \
        PUSH_BIT(idx, B5); \
        PUSH_BIT(idx, B4); \
        PUSH_BIT(idx, B3); \
        PUSH_BIT(idx, B2); \
        PUSH_BIT(idx, B1); \
        PUSH_BIT(idx, B0); \
        return idx; \
    }
#define TAG_PHRB3(P1, P2, P3) (phrb_idx_t{P1, P2, P3})
#define TAG_PHRB2(P1, P2) TAG_PHRB3(P1, P2, INVALID_IDX)
#define TAG_PHRB1(P1) TAG_PHRB2(P1, INVALID_IDX)
#define TAG_FOLD_FN(T0, B0, T1, B1, T2, B2, T3, B3, T4, B4, T5, B5, T6, B6, T7, B7, T8, B8, T9, B9, T10, B10, T11, B11) \
    [](const hist_t &hist) { \
        constexpr uint8_t FOLD_WIDTH = 12;\
        constexpr size_t INVALID_IDX = -1;\
\
        using std::pair;\
        using phrb_idx_t = std::array<size_t, 3>;\
        std::array<pair<pht_t::index_range_t, phrb_idx_t>, FOLD_WIDTH> fold_idx = {\
            pair{bit_indices(0, 12, T0), B0},\
            pair{bit_indices(1, 13, T1), B1},\
            pair{bit_indices(2, 14, T2), B2},\
            pair{bit_indices(3, 15, T3), B3},\
            pair{bit_indices(4, 16, T4), B4},\
            pair{bit_indices(5, 17, T5), B5},\
            pair{bit_indices(6, 18, T6), B6},\
            pair{bit_indices(7, 19, T7), B7},\
            pair{bit_indices(8, 20, T8), B8},\
            pair{bit_indices(9, 21, T9), B9},\
            pair{bit_indices(10, 22, T10), B10},\
            pair{bit_indices(11, 23, T11), B11}\
        };\
        tag_t hist_fold = 0;\
        for (auto &&[phrt_idx, phrb_idx]: std::views::reverse(fold_idx)) {\
            hist_fold <<= 1;\
            const auto t = hist.phrt.fold_bits(phrt_idx);\
            const auto b = hist.phrb.fold_bits(std::views::filter(phrb_idx, [INVALID_IDX](auto idx) {\
                return idx != INVALID_IDX;\
            }));\
            hist_fold |= (t ^ b) & 0b1;\
        }\
        return hist_fold;\
    }
            phts_t phts = {
                pht_t(4, 10,
                      IDX_FN(
                          XOR(PHRT(2), PHRT(43), PHRT(93)),
                          XOR(PHRT(7), PHRT(48), PHRT(99)),
                          XOR(PHRT(12), PHRT(63), PHRB(5)),
                          XOR(PHRT(17), PHRT(68), PHRB(10)),
                          XOR(PHRT(22), PHRT(73), PHRB(15)),
                          XOR(PHRT(27), PHRT(78), PHRB(20)),
                          XOR(PHRT(33), PHRT(83), PHRB(25)),
                          XOR(PHRT(38), PHRT(88), PC(9)),
                          XOR(PHRT(53), PHRT(58), PHRB(0)),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          96, TAG_PHRB2(8, 21),
                          97, TAG_PHRB2(9, 22),
                          98, TAG_PHRB3(10, 23, 24),
                          99, TAG_PHRB3(11, 12, 25),
                          88, TAG_PHRB3(0, 13, 26),
                          89, TAG_PHRB3(1, 14, 27),
                          90, TAG_PHRB2(2, 15),
                          91, TAG_PHRB2(3, 16),
                          92, TAG_PHRB2(4, 17),
                          93, TAG_PHRB2(5, 18),
                          94, TAG_PHRB2(6, 19),
                          95, TAG_PHRB2(7, 20)
                      )
                ),
                pht_t(4, 10,
                      IDX_FN(
                          XOR(PHRT(1), PHRT(35), PHRB(10)),
                          XOR(PHRT(4), PHRT(38), PHRB(13)),
                          XOR(PHRT(8), PHRT(42), PHRB(17)),
                          XOR(PHRT(11), PHRT(45), PHRB(20)),
                          XOR(PHRT(14), PHRT(49), PHRB(23)),
                          XOR(PHRT(18), PHRT(52), PHRB(27)),
                          XOR(PHRT(21), PHRT(56), PHRB(0)),
                          XOR(PHRT(25), PHRT(28), PHRB(3)),
                          XOR(PHRT(32), PHRB(6), PC(9)),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          48, TAG_PHRB2(8, 21),
                          49, TAG_PHRB2(9, 22),
                          50, TAG_PHRB3(10, 23, 24),
                          51, TAG_PHRB3(11, 12, 25),
                          52, TAG_PHRB3(0, 13, 26),
                          53, TAG_PHRB3(1, 14, 27),
                          54, TAG_PHRB2(2, 15),
                          55, TAG_PHRB2(3, 16),
                          56, TAG_PHRB2(4, 17),
                          45, TAG_PHRB2(5, 18),
                          46, TAG_PHRB2(6, 19),
                          47, TAG_PHRB2(7, 20)
                      )
                ),
                pht_t(4, 10,
                      IDX_FN(
                          XOR(PHRT(1), PHRT(26), PHRB(19)),
                          XOR(PHRT(3), PHRT(28), PHRB(0)),
                          XOR(PHRT(6), PHRT(31), PHRB(2)),
                          XOR(PHRT(8), PHRT(11), PHRB(4)),
                          XOR(PHRT(13), PHRB(7), PHRB(22)),
                          XOR(PHRT(16), PHRB(9), PHRB(24)),
                          XOR(PHRT(18), PHRB(12), PHRB(27)),
                          XOR(PHRT(21), PHRB(14), PC(8)),
                          XOR(PHRT(23), PHRB(17), PC(11)),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          24, TAG_PHRB2(8, 21),
                          25, TAG_PHRB2(9, 22),
                          26, TAG_PHRB3(10, 23, 24),
                          27, TAG_PHRB3(11, 12, 25),
                          28, TAG_PHRB3(0, 13, 26),
                          29, TAG_PHRB3(1, 14, 27),
                          30, TAG_PHRB2(2, 15),
                          19, TAG_PHRB2(3, 16),
                          20, TAG_PHRB2(4, 17),
                          21, TAG_PHRB2(5, 18),
                          22, TAG_PHRB2(6, 19),
                          23, TAG_PHRB2(7, 20)
                      )
                ),
                pht_t(4, 11,
                      IDX_FN2(
                          XOR(PHRT(0), PHRT(15), PHRB(2)),
                          XOR(PHRT(1), PHRT(17), PHRB(4)),
                          XOR(PHRT(3), PHRT(4), PHRB(5)),
                          XOR(PHRT(5), PHRB(6), PHRB(13)),
                          XOR(PHRT(7), PHRB(8), PHRB(15)),
                          XOR(PHRT(8), PHRB(9), PHRB(16)),
                          XOR(PHRT(10), PHRB(11), PHRB(17)),
                          XOR(PHRT(11), PHRB(12), PC(8)),
                          XOR(PHRT(12), PHRB(0), PC(9)),
                          XOR(PHRT(14), PHRB(1), PC(11)),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          12, TAG_PHRB1(8),
                          13, TAG_PHRB1(9),
                          14, TAG_PHRB1(10),
                          15, TAG_PHRB2(11, 12),
                          16, TAG_PHRB2(0, 13),
                          17, TAG_PHRB2(1, 14),
                          6, TAG_PHRB2(2, 15),
                          7, TAG_PHRB2(3, 16),
                          8, TAG_PHRB2(4, 17),
                          9, TAG_PHRB1(5),
                          10, TAG_PHRB1(6),
                          11, TAG_PHRB1(7)
                      )
                ),
                pht_t(6, 11,
                      IDX_FN2(
                          XOR(PHRT(0), PHRT(1), PHRB(5)),
                          XOR(PHRT(2), PHRB(6), PHRB(10)),
                          XOR(PHRT(3), PHRB(7), PC(7)),
                          XOR(PHRT(4), PHRB(8), PC(8)),
                          XOR(PHRT(5), PHRB(9), PC(9)),
                          XOR(PHRT(6), PHRB(0), PC(10)),
                          XOR(PHRT(7), PHRB(1), PC(11)),
                          XOR(PHRT(8), PHRB(2), PC(12)),
                          XOR(PHRT(9), PHRB(3), PC(13)),
                          XOR(PHRT(10), PHRB(4), PC(14)),
                          PC(6)
                      ),
                      [](const hist_t &hist) {
                          constexpr tag_t PHRT_MASK = (1 << 11) - 1;
                          constexpr tag_t PHRB_MASK1 = (1 << 3) - 1;
                          constexpr tag_t PHRB_MASK2 = (1 << 8) - 1;

                          const tag_t phrt = hist.phrt.low() & PHRT_MASK;
                          const tag_t phrb1 = (hist.phrb.low() >> 8) & PHRB_MASK1;
                          const tag_t phrb2 = (hist.phrb.low() & PHRB_MASK2) << 4;
                          const tag_t phrb = phrb1 | phrb2;

                          return phrt ^ phrb;
                      }
                ),
                pht_t(6, 11,
                      IDX_FN2(
                          PHRT(0) ^ PHRT(1) ^ PC(14) ^ PC(15),
                          PHRT(2) ^ PC(16),
                          PHRT(3) ^ PC(17),
                          PHRT(4) ^ PC(18),
                          PHRT(5) ^ PC(19),
                          PHRB(0) ^ PHRB(1) ^ PC(7) ^ PC(8),
                          PHRB(2) ^ PC(9),
                          PHRB(3) ^ PC(10),
                          PHRB(4) ^ PC(11),
                          PHRB(5) ^ PC(12),
                          PC(6)
                      ),
                      [](const hist_t &hist) {
                          constexpr tag_t PHRT_MASK = (1 << 6) - 1;
                          constexpr tag_t PHRB_MASK = (1 << 6) - 1;

                          const tag_t phrt = hist.phrt.low() & PHRT_MASK;
                          const tag_t phrb = (hist.phrb.low() & PHRB_MASK) << 4;

                          return phrt ^ phrb;
                      }
                ),
            };
            std::ranges::reverse(phts);
            return phts;
#undef TAG_FOLD_FN
#undef TAG_PHRB1
#undef TAG_PHRB2
#undef TAG_PHRB3
#undef IDX_FN2
#undef IDX_FN
#undef PUSH_BIT
#undef XOR2
#undef XOR
#undef PHRB
#undef PHRT
#undef PC
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

        hist_t phr;
        base_pred_t base;
        phts_t phts;
        std::map<inst_id_t, pred_info_t> spec_pred_info;

        uint64_t br_ctr = 0;
        bool u_reset_msb = true;
        static constexpr uint64_t u_reset_threshold = tage::common::U_RESET_THRESHOLD;
    };
}
