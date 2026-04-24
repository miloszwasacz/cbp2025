#pragma once

#include <iostream>
#include <concepts>
#include <vector>
#include <functional>
#include <ranges>
#include <map>

#include "../tage_common.h"
#include "../../perf/base_col_ctr.h"
#include "../../perf/col_ctr.h"

namespace arm::common {
    using idx_t = size_t;
    using tag_t = uint16_t;
    inline constexpr size_t PHT_COUNT = 6;

    class base_pred_t {
    public:
        static constexpr size_t LOG_SIZE = 13;
        static constexpr size_t ENTRY_WIDTH = 2;
        // Log of how many entries share the same hysteresis.
        static constexpr size_t LOG_HYST_SHARE = 0;

        static constexpr size_t SIZE = 1 << LOG_SIZE;

        explicit base_pred_t() : dir(), hyst(), dir_col_ctr(SIZE), hyst_col_ctr(HYST_SIZE) {
            dir.fill(true);
            hyst.fill(0);
        }

        [[nodiscard]] bool predict(const uint64_t PC) const {
            return dir[index(PC)];
        }

        void update(const uint64_t PC, const bool correct) {
            constexpr uint8_t HYST_MAX = (1 << HYST_WIDTH) - 1;
            const size_t dir_idx = index(PC);
            const size_t hyst_idx = dir_idx >> LOG_HYST_SHARE;
            dir_col_ctr.insert(PC, dir_idx);
            // ReSharper disable once CppDFAUnreachableCode
            if constexpr (LOG_HYST_SHARE > 0) {
                hyst_col_ctr.insert(PC, hyst_idx);
            }
            auto &d = dir[dir_idx];
            auto &h = hyst[hyst_idx];

            if (correct) {
                h = h < HYST_MAX ? h + 1 : HYST_MAX;
            } else if (h == 0) {
                d = !d;
                dir_flips++;
            } else {
                h--;
            }
        }

        [[nodiscard]] static size_t size() {
            return SIZE + HYST_SIZE * HYST_WIDTH;
        }

        void print_stats(const uint8_t indent = 0) const {
            const std::string idt(indent, '\t');
            // ReSharper disable once CppDFAUnreachableCode
            if constexpr (LOG_HYST_SHARE == 0) {
                dir_col_ctr.print_stats(idt.size());
                std::cout << idt << "Direction flips:\t" << dir_flips << std::endl;
            } else {
                std::cout << idt << "Direction table:" << std::endl;
                dir_col_ctr.print_stats(idt.size() + 1);
                std::cout << idt << "\tDirection flips:\t" << dir_flips << std::endl;

                std::cout << idt << "Hysteresis table:" << std::endl;
                hyst_col_ctr.print_stats(idt.size() + 1);
            }
        }

    private:
        static constexpr size_t HYST_SIZE = 1 << (LOG_SIZE - LOG_HYST_SHARE);
        static constexpr size_t HYST_WIDTH = ENTRY_WIDTH - 1;
        static_assert(HYST_WIDTH <= sizeof(uint8_t) * CHAR_BIT);

        [[nodiscard]] static size_t index(const uint64_t pc) {
            return (pc >> 2) & (SIZE - 1);
        }

        std::array<bool, SIZE> dir;
        std::array<uint8_t, HYST_SIZE> hyst;

        perf::base_col_ctr dir_col_ctr;
        perf::base_col_ctr hyst_col_ctr;
        size_t dir_flips = 0;
    };

    template<size_t PHRT_SIZE, size_t PHRB_SIZE>
    struct hist_t {
        [[nodiscard]] size_t static size() {
            return PHRT_SIZE + PHRB_SIZE;
        }

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

    template<typename Hist, size_t TAG_WIDTH>
    class pht_t {
    public:
        using idx_fn_t = std::function<idx_t(uint64_t PC, const Hist &hist)>;
        using tag_hist_fold_fn_t = std::function<tag_t(const Hist &hist)>;
        using index_range_t = std::ranges::stride_view<std::ranges::iota_view<size_t, size_t> >;

        explicit pht_t(const size_t assoc, const size_t log_size, idx_fn_t index, tag_hist_fold_fn_t fold)
            : tag_fold_hist(std::move(fold)), col_ctr(assoc, 1 << log_size),
              entries(make_entries(log_size, assoc)), make_idx(std::move(index)),
              _size((1 << log_size) * assoc * entry_t::SIZE) {
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
            const auto idx = make_idx(pc, hist);
            col_ctr.insert(pc, idx);
            auto &ways = entries[idx];
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
            u_dec++;
        }

        void age_us(const bool clear_msb) {
            for (auto &ways: entries) {
                for (auto &entry: ways) {
                    entry.u.age(clear_msb);
                }
            }
        }

        [[nodiscard]] size_t size() const {
            return _size;
        }

        void print_stats(const uint8_t indent = 0) const {
            const std::string idt(indent, '\t');
            col_ctr.print_stats(idt.size());
            std::cout << idt << "`u` decrements:\t\t" << u_dec << std::endl;
        }

    protected:
        [[nodiscard]] virtual tag_t make_tag(uint64_t PC, const Hist &hist) const = 0;

        tag_hist_fold_fn_t tag_fold_hist;

        perf::collision_ctr col_ctr;
        size_t u_dec = 0;

    private:
        struct entry_t {
        private:
            static constexpr size_t CTR_WIDTH = 2;
            static constexpr size_t U_WIDTH = 1;

        public:
            static constexpr size_t SIZE = CTR_WIDTH + TAG_WIDTH + U_WIDTH;

            n_bit_predictor<CTR_WIDTH> dir;
            tag_t tag;
            tage::common::u_ctr<U_WIDTH> u;

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
        size_t _size;
    };

    template<typename Hist, typename PHT>
    class ArmBase {
        static constexpr size_t BASE_PRED_NUMBER = 0;

    protected:
        using phts_t = std::array<PHT, PHT_COUNT>;

    public:
        virtual ~ArmBase() = default;

        [[nodiscard]] virtual const char *name() const = 0;

        virtual void setup() {
            std::cout << "Testing " << name() << " CBP";
#ifdef PRINT_SIZE
            const auto s = size();
            std::cout << " (" << s << " b, " << s / (1024 * 8) << " KiB)";
#endif
            std::cout << std::endl;
        }

        virtual void terminate() {
            std::cout <<
                    "-----------------------------------------------------------Table Statistics------------------------------------------------------------"
                    << std::endl;
            print_stats();
            for (size_t i = phts.size(); i > 0; --i) {
                std::cout << "PHT #" << i << ":" << std::endl;
                phts[i - 1].print_stats(1);
                std::cout << std::endl;
            }
            std::cout << "Base predictor:" << std::endl;
            base.print_stats(1);
            std::cout <<
                    "---------------------------------------------------------------------------------------------------------------------------------------"
                    << std::endl;
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

        explicit ArmBase(phts_t phts) : phts(std::move(phts)) {
        }

        virtual void print_stats() const {
        }

        size_t size() const {
            return phr.size() +
                   base.size() + // NOLINT(*-static-accessed-through-instance)
                   std::ranges::fold_left(phts, size_t{0}, [](auto acc, const auto &pht) { return acc + pht.size(); });
        }

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
