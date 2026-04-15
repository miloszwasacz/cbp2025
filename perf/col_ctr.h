#pragma once

#include <ranges>
#include <unordered_map>
#include <set>

namespace perf {
    class collision_ctr {
    public:
        using pc_t = uint64_t;
        using idx_t = size_t;

        explicit collision_ctr(const size_t assoc, const size_t size) : assoc(assoc), size(size) {
            assert(assoc > 0);
        }

        virtual ~collision_ctr() = default;

        void insert(const pc_t pc, const idx_t idx) {
            m[idx].insert(pc);
        }

        virtual void print_stats(const uint8_t indent) const {
            const std::string idt(indent, '\t');
            size_t colls = 0;
            const size_t unused = size - m.size();
            size_t avg = 0, med = 0, max = 0, count = 0;
            {
                size_t sum = 0;
                std::multiset<size_t> vals;
                for (const auto &pcs: m | std::views::values) {
                    if (const size_t s = pcs.size(); s > assoc) {
                        colls += s - assoc;
                        sum += s;
                        vals.insert(s);
                    }
                }
                if (count = vals.size(); count > 0) {
                    avg = sum / count;
                    max = *vals.rbegin();
                    const auto mid = std::next(vals.begin(), static_cast<ssize_t>(count / 2));
                    if (count % 2 == 0) {
                        med = (*std::prev(mid, 1) + *mid) / 2;
                    } else {
                        med = *mid;
                    }
                }
            }

            std::cout
                    << idt << "Total collisions:\t" << colls << std::endl
                    << idt << "Unused entries:\t\t" << unused << '/' << size
                    << std::format(" ({:.2f}%)", static_cast<float>(unused) * 100.f / static_cast<float>(size))
                    << std::endl
                    << idt << "Entries w/ collisions:\t" << count << '/' << size
                    << std::format(" ({:.2f}%)", static_cast<float>(count) * 100.f / static_cast<float>(size))
                    << std::endl;
            if (max > 0) {
                std::cout << idt << "Colliding PCs:\t\t(" << avg << ',' << med << ',' << max << ")/"
                        << assoc << "\t\t# (avg,med,max)/assoc" << std::endl;
            }
        }

    protected:
        using pcs_t = std::set<pc_t>;

        size_t assoc;
        size_t size;
        std::unordered_map<idx_t, pcs_t> m;
    };
}
