#pragma once

#include <algorithm>
#include <cassert>
#include <climits>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

#include "n_bit_predictor.h"
#include "u_ctr.h"


//TODO: Different uarch apart from Skylake (e.g. Alder Lake, Haswell, etc.)

// Intel Skylake Conditional Branch Predictor.
class IntelSkylakePredictor {
#pragma region internals
    static constexpr size_t PHT_COUNT = 3;
    static constexpr size_t PHT_SIZE = 1 << (9 - 1); // 2^9
    static constexpr size_t BASE_SIZE = 1 << (13 - 1); // 2^13
    static constexpr size_t BASE_PRED_NUMBER = 0;
    static constexpr uint64_t U_RESET_THRESHOLD = 256000;

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

    // Information about a made prediction that would be propagated through
    // the pipeline and later used to
    struct pred_info_t {
        std::pair<bool, size_t> pred;
        std::pair<bool, size_t> altpred;
        phr_t::phist_t hist;

        pred_info_t(const std::pair<bool, size_t> &pred, const std::pair<bool, size_t> &altpred,
                    const phr_t::phist_t &hist) : pred(pred), altpred(altpred), hist(hist) {
        }
    };

    // A 4-way set associative table of predictions.
    class pht_t {
        class tag_t {
        public:
            explicit tag_t(const phr_t::phist_t &hist, const uint64_t addr) {
                // Only a lower part of the address is used
                constexpr uint64_t mask = ~(std::numeric_limits<uint64_t>::max() << 13);
                const uint16_t pc_bits = addr & mask;
                val = {hist, pc_bits};
            }

            bool operator==(const tag_t &other) const {
                return val.first == other.val.first && val.second == other.val.second;
            }

        private:
            // TODO More accurate tags? (unknown hash & folding function)
            std::pair<phr_t::phist_t, uint16_t> val;
        };

        struct entry_t {
            n_bit_predictor<3> pred;
            tag_t tag;
            u_ctr<2> u;

            explicit entry_t(tag_t tag) : tag(std::move(tag)) {
            }

            entry_t() : entry_t(tag_t(phr_t::phist_t(), 0)) {
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

        [[nodiscard]] std::optional<bool> predict(const phr_t::phist_t &hist, const uint64_t pc) const {
            const tag_t tag(hist, pc);
            for (const auto &ways = entries[index(hist, pc)]; const auto &entry: ways) {
                if (entry.tag == tag) {
                    return entry.pred.predict();
                }
            }

            return {};
        }

        void update_provider(const pred_info_t &info, const uint64_t pc, const bool predDir, const bool resolveDir) {
            const tag_t tag(info.hist, pc);
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

        [[nodiscard]] bool can_allocate(const pred_info_t &info, const uint64_t pc) const {
            return std::ranges::any_of(
                entries[index(info.hist, pc)],
                [](auto &entry) { return entry.u.value() == 0; }
            );
        }

        void allocate(const pred_info_t &info, const uint64_t pc) {
            auto &ways = entries[index(info.hist, pc)];
            const auto entry = std::ranges::find_if(
                ways,
                [](auto &way) { return way.u.value() == 0; }
            );
            assert(entry != ways.end());
            tag_t tag(info.hist, pc);
            *entry = entry_t(std::move(tag));
        }

        void decrement_us() {
            for (auto &ways: entries) {
                for (auto &entry: ways) {
                    entry.u.update(false);
                }
            }
        }

        void age_us(const bool clear_msb) {
            for (auto &ways : entries) {
                for (auto &entry: ways) {
                    entry.u.age(clear_msb);
                }
            }
        }

    private:
        [[nodiscard]] size_t index(const phr_t::phist_t &hist, const uint64_t pc) const {
            std::function<ssize_t(ssize_t i)> even_start, odd_start;
            ssize_t i_hi, i_lo, j_hi, j_lo;
            if (level == 1) {
                even_start = [](ssize_t) {
                    return 20;
                };
                odd_start = [](ssize_t) {
                    return 15;
                };
                i_hi = i_lo = j_hi = j_lo = 0;
            } else if (level == 2) {
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
            } else if (level == 3) {
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
            } else {
                std::cerr << "unimplemented index hash function" << std::endl;
                std::abort();
            }

            size_t idx = (pc >> 5) & 0b1;
            // History folding
            {
                constexpr uint8_t FOLD_LEN = 8;
                for (uint8_t offset = 0; offset < FOLD_LEN; offset++) {
                    idx <<= 1;
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
                        f ^= hist.bit_at(even_bit(j));
                    }
                }
            }

            return idx;
        }

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

        std::array<n_bit_predictor<3>, BASE_SIZE> entries;
        //TODO The papers don't state the N for base predictor entries -- one
        //     of them only states that _all_ PHT tables use 3-bit counters
        //     (it is not clear whether that also applies to the base predictor).
    };
#pragma endregion

public:
    IntelSkylakePredictor() : phts{pht_t(1), pht_t(2), pht_t(3)} {
    }

    void setup() const {
    }

    void terminate() const {
    }

    [[nodiscard]] bool predict(const uint64_t seq_no, const uint8_t piece, const uint64_t PC) {
        const auto id = get_unique_inst_id(seq_no, piece);
        const auto &hist = phr.value();

        std::pair pred = {base.predict(PC), BASE_PRED_NUMBER};
        std::pair altpred = pred;

        for (size_t i = 0; i < PHT_COUNT; i++) {
            const auto &table = phts[PHT_COUNT - i - 1];
            const auto out = table.predict(hist, PC);
            if (!out.has_value()) continue;

            const std::pair p = {out.value(), PHT_COUNT - i};
            if (pred.second == 0) {
                pred = p;
            } else if (altpred.second == 0) {
                altpred = p;
                break;
            }
        }

        spec_pred_info.insert({id, {pred, altpred, hist}});

        //TODO Inform whether to use pred or altpred based on the `u` counters?
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
        if (const auto &info = spec_pred_info.at(id); info.pred.second == BASE_PRED_NUMBER) {
            base.update(PC, resolveDir);
        } else {
            // Update provider component
            phts[info.pred.second - 1].update_provider(info, PC, predDir, resolveDir);

            if (resolveDir != predDir) {
                // Find a new entry in further table to allocate
                size_t next = 0, next_next = 0;
                for (size_t i = info.pred.second; i < PHT_COUNT; i++) {
                    if (phts[i].can_allocate(info, PC)) {
                        if (next == 0) {
                            next = i;
                        } else {
                            next_next = i;
                            break;
                        }
                    }
                }

                if (next == 0) {
                    // No entries can be allocated, decrement all `u`s
                    for (size_t i = info.pred.second; i < PHT_COUNT; i++) {
                        phts[i].decrement_us();
                    }
                    return;
                }

                if (next_next != 0) {
                    //TODO Choose between next and next_next, with higher probability of picking next

                    // next = rand(0, 1) > threshold ? next : next_next;
                }

                phts[next].allocate(info, PC);
            }
        }

        br_ctr++;
        if (br_ctr >= U_RESET_THRESHOLD) {
            br_ctr = 0;
            for (auto &pht : phts) {
                pht.age_us(u_reset_msb);
            }
            u_reset_msb = !u_reset_msb;
        }
    }

private:
    using inst_id_t = uint64_t;

    static inst_id_t get_unique_inst_id(const uint64_t seq_no, const uint8_t piece) {
        assert(piece < 16);
        return (seq_no << 4) | (piece & 0x000F);
    }

    phr_t phr;
    base_pred_t base;
    std::array<pht_t, PHT_COUNT> phts;
    std::map<inst_id_t, pred_info_t> spec_pred_info;
    uint64_t br_ctr = 0;
    bool u_reset_msb = true;
};
