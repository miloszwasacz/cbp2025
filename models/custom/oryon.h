#pragma once

#include "arm/common.h"

namespace qualcomm::oryon {
    static constexpr size_t PHRT_SIZE = 100;
    static constexpr size_t PHRB_SIZE = 32;
    inline constexpr size_t TAG_WIDTH = 16;

    class hist_t final : public arm::common::hist_t<PHRT_SIZE, PHRB_SIZE> {
    };

    class pht_t final : public arm::common::pht_t<hist_t, TAG_WIDTH> {
    public:
        explicit pht_t(const size_t assoc, const size_t log_size, idx_fn_t index, tag_hist_fold_fn_t fold)
            : arm::common::pht_t<hist_t, TAG_WIDTH>(assoc, log_size, std::move(index), std::move(fold)) {
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

namespace qualcomm {
    class OryonCBP final : public arm::common::ArmBase<oryon::hist_t, oryon::pht_t> {
    public:
        explicit OryonCBP() : ArmBase(make_phts()) {
        }

        [[nodiscard]] const char *name() const override {
            return "Qualcomm Oryon";
        }

    private:
        [[nodiscard]] static phts_t make_phts() {
            using arm::common::idx_t;
            using arm::common::tag_t;
            using oryon::pht_t;
            using oryon::hist_t;
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
            // NOTE: These functions are extrapolated from the `firestorm.toml`
            //       config and values mentioned in the paper, since there is
            //       no proper config file for Oryon.
            phts_t phts = {
                pht_t(4, 10,
                      IDX_FN(
                          PHRT(3) ^ PHRT(44) ^ PHRT(95),
                          PHRT(8) ^ PHRT(49) ^ PC(7),
                          PHRT(14) ^ PHRT(65) ^ PHRB(5),
                          PHRT(19) ^ PHRT(70) ^ PHRB(10),
                          PHRT(24) ^ PHRT(75) ^ PHRB(15),
                          PHRT(29) ^ PHRT(80) ^ PHRB(20),
                          PHRT(34) ^ PHRT(85) ^ PHRB(25),
                          PHRT(39) ^ PHRT(90) ^ PHRB(30),
                          PHRT(54) ^ PHRT(60) ^ PHRB(0),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          96, TAG_PHRB3(0, 12, 24),
                          97, TAG_PHRB3(1, 13, 25),
                          98, TAG_PHRB3(2, 14, 26),
                          99, TAG_PHRB3(3, 15, 27),
                          88, TAG_PHRB3(4, 16, 28),
                          89, TAG_PHRB3(5, 17, 29),
                          90, TAG_PHRB3(6, 18, 30),
                          91, TAG_PHRB3(7, 19, 31),
                          92, TAG_PHRB2(8, 20),
                          93, TAG_PHRB2(9, 21),
                          94, TAG_PHRB2(10, 22),
                          95, TAG_PHRB2(11, 23)
                      )
                ),
                pht_t(4, 10,
                      IDX_FN(
                          PHRT(1) ^ PHRT(38) ^ PHRB(6),
                          PHRT(4) ^ PHRT(41) ^ PHRB(10),
                          PHRT(8) ^ PHRT(44) ^ PHRB(13),
                          PHRT(11) ^ PHRT(48) ^ PHRB(16),
                          PHRT(14) ^ PHRT(51) ^ PHRB(20),
                          PHRT(18) ^ PHRB(23) ^ PC(9),
                          PHRT(21) ^ PHRB(28) ^ PHRB(26),
                          PHRT(24) ^ PHRB(31) ^ PHRB(0),
                          PHRT(34) ^ PHRB(3) ^ PHRB(30),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          48, TAG_PHRB3(0, 12, 24),
                          49, TAG_PHRB3(1, 13, 25),
                          50, TAG_PHRB3(2, 14, 26),
                          51, TAG_PHRB3(3, 15, 27),
                          40, TAG_PHRB3(4, 16, 28),
                          41, TAG_PHRB3(5, 17, 29),
                          42, TAG_PHRB3(6, 18, 30),
                          43, TAG_PHRB3(7, 19, 31),
                          44, TAG_PHRB2(8, 20),
                          45, TAG_PHRB2(9, 21),
                          46, TAG_PHRB2(10, 22),
                          47, TAG_PHRB2(11, 23)
                      )
                ),
                pht_t(4, 10,
                      IDX_FN(
                          PHRT(1) ^ PHRB(11) ^ PC(8),
                          PHRT(4) ^ PHRB(13) ^ PC(10),
                          PHRT(6) ^ PHRB(15) ^ PHRB(13),
                          PHRT(8) ^ PHRT(15) ^ PHRB(17),
                          PHRT(10) ^ PHRT(17) ^ PHRB(0),
                          PHRT(19) ^ PHRB(2) ^ PHRB(20),
                          PHRT(21) ^ PHRB(4) ^ PHRB(22),
                          PHRT(24) ^ PHRB(6) ^ PHRB(24),
                          PHRT(26) ^ PHRB(8) ^ PHRB(26),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          24, TAG_PHRB3(0, 12, 24),
                          25, TAG_PHRB3(1, 13, 25),
                          26, TAG_PHRB3(2, 14, 26),
                          15, TAG_PHRB2(3, 15),
                          16, TAG_PHRB2(4, 16),
                          17, TAG_PHRB2(5, 17),
                          18, TAG_PHRB2(6, 18),
                          19, TAG_PHRB2(7, 19),
                          20, TAG_PHRB2(8, 20),
                          21, TAG_PHRB2(9, 21),
                          22, TAG_PHRB2(10, 22),
                          23, TAG_PHRB2(11, 23)
                      )
                ),
                pht_t(4, 11,
                      IDX_FN2(
                          PHRT(0) ^ PHRB(4) ^ PC(10),
                          PHRT(1) ^ PHRB(5) ^ PC(11),
                          PHRT(3) ^ PHRT(8) ^ PHRB(6),
                          PHRT(4) ^ PHRT(9) ^ PHRB(7),
                          PHRT(5) ^ PHRT(10) ^ PHRB(9),
                          PHRT(6) ^ PHRT(12) ^ PHRB(10),
                          PHRT(7) ^ PHRT(13) ^ PHRB(0),
                          PHRB(1) ^ PHRB(11) ^ PC(7),
                          PHRB(2) ^ PHRB(12) ^ PC(8),
                          PHRB(3) ^ PHRB(13) ^ PC(9),
                          PC(6)
                      ),
                      TAG_FOLD_FN(
                          12, TAG_PHRB2(0, 12),
                          13, TAG_PHRB2(1, 13),
                          2, TAG_PHRB1(2),
                          3, TAG_PHRB1(3),
                          4, TAG_PHRB1(4),
                          5, TAG_PHRB1(5),
                          6, TAG_PHRB1(6),
                          7, TAG_PHRB1(7),
                          8, TAG_PHRB1(8),
                          9, TAG_PHRB1(9),
                          10, TAG_PHRB1(10),
                          11, TAG_PHRB1(11)
                      )
                ),
                // NOTE: These functions are very low confidence, since there is
                //       no config file and they differ from the previous pattern.
                pht_t(4, 11,
                      IDX_FN2(
                          PHRT(0) ^ PHRT(1) ^ PHRB(6),
                          PHRT(2) ^ PC(11),
                          PHRT(3) ^ PC(12),
                          PHRT(4) ^ PC(13),
                          PHRT(5) ^ PHRB(4),
                          PHRT(6) ^ PHRB(5),
                          PHRB(0) ^ PC(7),
                          PHRB(1) ^ PC(8),
                          PHRB(2) ^ PC(9),
                          PHRB(3) ^ PC(10),
                          PC(6)
                      ),
                      [](const hist_t &hist) {
                          constexpr tag_t PHRT_MASK = (1 << 7) - 1;
                          constexpr tag_t PHRB_MASK = (1 << 7) - 1;

                          const tag_t phrt = hist.phrt.low() & PHRT_MASK;
                          const tag_t phrb = (hist.phrb.low() & PHRB_MASK) << 5;

                          return phrt ^ phrb;
                      }
                ),
                // NOTE: These functions are very low confidence, since there is
                //       no config file and they differ from the previous pattern.
                pht_t(6, 11,
                      IDX_FN2(
                          PHRT(0) ^ PC(13) ^ PC(14),
                          PHRT(1) ^ PC(15),
                          PHRT(2) ^ PC(16),
                          PHRT(3) ^ PC(17),
                          PHRB(0) ^ PC(7) ^ PC(8),
                          PHRB(1) ^ PC(9),
                          PHRB(2) ^ PC(10),
                          PHRB(3) ^ PC(11),
                          PC(18),
                          PC(19),
                          PC(6)
                      ),
                      [](const hist_t &hist) {
                          constexpr tag_t PHRT_MASK = (1 << 4) - 1;
                          constexpr tag_t PHRB_MASK = (1 << 4) - 1;

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
