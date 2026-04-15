#pragma once

#include "col_ctr.h"

namespace perf {
    class base_col_ctr : public collision_ctr {
    public:
        explicit base_col_ctr(const size_t size) : collision_ctr(1, size) {
        }
    };
}
