#pragma once

#include <algorithm>
#include <cassert>
#include <climits>
#include <limits>
#include <map>

#include "../n_bit_predictor.h"

namespace tage::common {
    inline constexpr size_t PHT_COUNT = 3;
    inline constexpr size_t PHT_LOG_SIZE = 9;
    inline constexpr size_t PHT_SIZE = 1 << PHT_LOG_SIZE; // 2^9
    inline constexpr size_t BASE_SIZE = 1 << 13; // 2^13
    inline constexpr size_t BASE_PRED_NUMBER = 0;
    inline constexpr uint64_t U_RESET_THRESHOLD = 256000;

    // N-bit usefulness counter
    template<int8_t N>
    class u_ctr {
    public:
        u_ctr() {
            static_assert(N > 0);
            static_assert(N <= sizeof(int16_t) * CHAR_BIT / 2);
            val = 0;
        }

        void update(const bool correct) {
            val = std::clamp(val + (correct ? 1 : -1), 0, {MAX});
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


    // Information about a made prediction that would be propagated through
    // the pipeline and later used to
    template<typename Hist>
    struct pred_info_t {
        using pred_t = std::pair<bool, size_t>;

        pred_t pred;
        pred_t altpred;
        Hist hist;

        pred_info_t(const std::pair<bool, size_t> &pred, const std::pair<bool, size_t> &altpred,
                    const Hist &hist) : pred(pred), altpred(altpred), hist(hist) {
        }
    };

    // A 4-way set associative table of predictions.
    template<typename Hist>
    class pht_t {
        using tag_t = uint64_t;

        struct entry_t {
            n_bit_predictor<3> pred;
            tag_t tag;
            u_ctr<2> u;

            explicit entry_t(const tag_t tag) : tag(tag) {
            }

            entry_t() : entry_t(0) {
            }
        };

    public:
        explicit pht_t(const size_t level) {
            assert(level > 0 && level <= PHT_COUNT && "invalid PHT level");
            this->level = level;
            const entry_t entry{};
            std::array<entry_t, 4> ways{};
            ways.fill(entry);
            entries.fill(ways);
        }

        virtual ~pht_t() = default;

        [[nodiscard]] std::optional<bool> predict(const Hist &hist, const uint64_t pc) const {
            const tag_t tag = this->tag(hist, pc);
            for (const auto &ways = entries[index(hist, pc)]; const auto &entry: ways) {
                if (entry.tag == tag) {
                    return entry.pred.predict();
                }
            }

            return {};
        }

        void update_provider(const pred_info_t<Hist> &info, const uint64_t pc, const bool predDir,
                             const bool resolveDir) {
            const tag_t tag = this->tag(info.hist, pc);
            for (auto &ways = entries[index(info.hist, pc)]; auto &entry: ways) {
                if (entry.tag != tag) continue;

                // Update usefulness counter `u`
                if (info.pred.first != info.altpred.first) {
                    entry.u.update(info.pred.first == resolveDir);
                }

                // Update prediction
                entry.pred.update(resolveDir);

                return;
            }
        }

        [[nodiscard]] bool can_allocate(const pred_info_t<Hist> &info, const uint64_t pc) const {
            return std::ranges::any_of(
                entries[index(info.hist, pc)],
                [](auto &entry) { return entry.u.value() == 0; }
            );
        }

        void allocate(const pred_info_t<Hist> &info, const uint64_t pc) {
            auto &ways = entries[index(info.hist, pc)];
            const auto entry = std::ranges::find_if(
                ways,
                [](auto &way) { return way.u.value() == 0; }
            );
            assert(entry != ways.end());
            const tag_t tag = this->tag(info.hist, pc);
            *entry = entry_t(tag);
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
        [[nodiscard]] virtual size_t index(const Hist &hist, uint64_t pc) const = 0;

        [[nodiscard]] virtual size_t tag(const Hist &hist, uint64_t pc) const = 0;

        std::array<std::array<entry_t, 4>, PHT_SIZE> entries;
        size_t level;
    };

    class base_pred_t {
    public:
        [[nodiscard]] bool predict(const uint64_t pc) const {
            const auto &entry = entries[index(pc)];
            return entry.predict();
        }

        void update(const uint64_t pc, const bool taken) {
            auto &entry = entries[index(pc)];
            entry.update(taken);
        }

    private:
        [[nodiscard]] static size_t index(const uint64_t pc) {
            constexpr uint64_t mask = BASE_SIZE - 1;
            return pc & mask;
        }

        std::array<n_bit_predictor<2>, BASE_SIZE> entries;
    };

    template<typename Hist, typename PHT>
    class TageBase {
        using pred_t = pred_info_t<Hist>::pred_t;

    public:
        virtual ~TageBase() = default;

        virtual void setup() {
        }

        virtual void terminate() {
        }

        [[nodiscard]] virtual bool predict(const uint64_t seq_no, const uint8_t piece, const uint64_t PC) {
            const auto id = get_unique_inst_id(seq_no, piece);
            const auto &hist = get_hist();

            pred_t pred = {base.predict(PC), BASE_PRED_NUMBER};
            pred_t altpred = pred;

            for (size_t i = 0; i < PHT_COUNT; i++) {
                const auto &table = phts[PHT_COUNT - i - 1];
                const auto out = table.predict(hist, PC);
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

            //TODO Inform whether to use pred or altpred based on the `u` counters?
            return pred.first;
        }

        virtual void history_update(uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC) = 0;

        virtual void track_other_inst(uint64_t PC, InstClass instClass, bool predDir, bool resolveDir,
                                      uint64_t nextPC) {
        }

        virtual void update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool resolveDir,
                            const bool predDir, const uint64_t nextPC) {
            const auto id = get_unique_inst_id(seq_no, piece);
            const auto &info = spec_pred_info.at(id);

            // Update provider component
            if (info.pred.second == BASE_PRED_NUMBER) {
                base.update(PC, resolveDir);
            } else {
                phts[info.pred.second - 1].update_provider(info, PC, predDir, resolveDir);
            }

            if (resolveDir != predDir) {
                // Find a new entry in further table to allocate
                size_t next = -1, next_next = -1;
                static_assert(PHT_COUNT < static_cast<size_t>(-1));
                for (size_t i = info.pred.second; i < PHT_COUNT; ++i) {
                    if (phts[i].can_allocate(info, PC)) {
                        if (next == -1) {
                            next = i;
                        } else {
                            next_next = i;
                            break;
                        }
                    }
                }

                if (next == -1) {
                    // No entries can be allocated, decrement all `u`s
                    for (size_t i = info.pred.second; i < PHT_COUNT; ++i) {
                        phts[i].decrement_us();
                    }
                    return;
                }

                if (next_next != -1) {
                    //TODO Choose between next and next_next, with higher probability of picking next

                    // next = rand(0, 1) > threshold ? next : next_next;
                }

                phts[next].allocate(info, PC);
            }

            age_us();
        }

    protected:
        using inst_id_t = uint64_t;

        explicit TageBase(const uint64_t u_reset_threshold)
            : phts{PHT(1), PHT(2), PHT(3)},
              u_reset_threshold(u_reset_threshold) {
        }

        static inst_id_t get_unique_inst_id(const uint64_t seq_no, const uint8_t piece) {
            assert(piece < 16);
            return (seq_no << 4) | (piece & 0x000F);
        }

        virtual const Hist &get_hist() = 0;

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

        base_pred_t base;
        std::array<PHT, PHT_COUNT> phts;
        std::map<inst_id_t, pred_info_t<Hist> > spec_pred_info;

    private:
        uint64_t br_ctr = 0;
        bool u_reset_msb = true;
        uint64_t u_reset_threshold;
    };
}
