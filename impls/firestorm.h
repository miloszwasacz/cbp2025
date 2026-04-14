#pragma once

#include "arm/common.h"

namespace apple::firestorm {
    static constexpr size_t PHRT_SIZE = 100;
    static constexpr size_t PHRB_SIZE = 28;

    class hist_t final : public arm::common::hist_t<PHRT_SIZE, PHRB_SIZE> {
    };

    class pht_t final : public arm::common::pht_t<hist_t> {
    public:
        explicit pht_t(const size_t assoc, const size_t log_size, idx_fn_t index, tag_hist_fold_fn_t fold)
            : arm::common::pht_t<hist_t>(assoc, log_size, std::move(index), std::move(fold)) {
        }

        ~pht_t() override = default;

    protected:
        [[nodiscard]] arm::common::tag_t make_tag(const uint64_t PC, const hist_t &hist) const override {
            using arm::common::tag_t;
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
    };
}

namespace apple {
    class FirestormCBP final : public arm::common::ArmBase<firestorm::hist_t, firestorm::pht_t> {
    public:
        explicit FirestormCBP() : ArmBase(make_phts()) {
        }

        [[nodiscard]] const char *name() const override {
            return "Apple Firestorm";
        }

    private:
        [[nodiscard]] static phts_t make_phts() {
            using arm::common::idx_t;
            using arm::common::tag_t;
            using firestorm::pht_t;
            using firestorm::hist_t;
#define PC(IDX) (((pc) >> IDX) & 0b1)
#define PHRT(IDX) hist.phrt.bit_at(IDX)
#define PHRB(IDX) hist.phrb.bit_at(IDX)
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
                          PHRT(2) ^ PHRT(43) ^ PHRT(93),
                          PHRT(7) ^ PHRT(48) ^ PHRT(99),
                          PHRT(12) ^ PHRT(63) ^ PHRB(5),
                          PHRT(17) ^ PHRT(68) ^ PHRB(10),
                          PHRT(22) ^ PHRT(73) ^ PHRB(15),
                          PHRT(27) ^ PHRT(78) ^ PHRB(20),
                          PHRT(33) ^ PHRT(83) ^ PHRB(25),
                          PHRT(38) ^ PHRT(88) ^ PC(9),
                          PHRT(53) ^ PHRT(58) ^ PHRB(0),
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
                          PHRT(1) ^ PHRT(35) ^ PHRB(10),
                          PHRT(4) ^ PHRT(38) ^ PHRB(13),
                          PHRT(8) ^ PHRT(42) ^ PHRB(17),
                          PHRT(11) ^ PHRT(45) ^ PHRB(20),
                          PHRT(14) ^ PHRT(49) ^ PHRB(23),
                          PHRT(18) ^ PHRT(52) ^ PHRB(27),
                          PHRT(21) ^ PHRT(56) ^ PHRB(0),
                          PHRT(25) ^ PHRT(28) ^ PHRB(3),
                          PHRT(32) ^ PHRB(6) ^ PC(9),
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
                          PHRT(1) ^ PHRT(26) ^ PHRB(19),
                          PHRT(3) ^ PHRT(28) ^ PHRB(0),
                          PHRT(6) ^ PHRT(31) ^ PHRB(2),
                          PHRT(8) ^ PHRT(11) ^ PHRB(4),
                          PHRT(13) ^ PHRB(7) ^ PHRB(22),
                          PHRT(16) ^ PHRB(9) ^ PHRB(24),
                          PHRT(18) ^ PHRB(12) ^ PHRB(27),
                          PHRT(21) ^ PHRB(14) ^ PC(8),
                          PHRT(23) ^ PHRB(17) ^ PC(11),
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
                          PHRT(0) ^ PHRT(15) ^ PHRB(2),
                          PHRT(1) ^ PHRT(17) ^ PHRB(4),
                          PHRT(3) ^ PHRT(4) ^ PHRB(5),
                          PHRT(5) ^ PHRB(6) ^ PHRB(13),
                          PHRT(7) ^ PHRB(8) ^ PHRB(15),
                          PHRT(8) ^ PHRB(9) ^ PHRB(16),
                          PHRT(10) ^ PHRB(11) ^ PHRB(17),
                          PHRT(11) ^ PHRB(12) ^ PC(8),
                          PHRT(12) ^ PHRB(0) ^ PC(9),
                          PHRT(14) ^ PHRB(1) ^ PC(11),
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
                          PHRT(0) ^ PHRT(1) ^ PHRB(5),
                          PHRT(2) ^ PHRB(6) ^ PHRB(10),
                          PHRT(3) ^ PHRB(7) ^ PC(7),
                          PHRT(4) ^ PHRB(8) ^ PC(8),
                          PHRT(5) ^ PHRB(9) ^ PC(9),
                          PHRT(6) ^ PHRB(0) ^ PC(10),
                          PHRT(7) ^ PHRB(1) ^ PC(11),
                          PHRT(8) ^ PHRB(2) ^ PC(12),
                          PHRT(9) ^ PHRB(3) ^ PC(13),
                          PHRT(10) ^ PHRB(4) ^ PC(14),
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
#undef PHRB
#undef PHRT
#undef PC
        }
    };
}
