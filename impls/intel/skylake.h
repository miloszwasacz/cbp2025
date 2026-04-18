#pragma once

#include "common.h"

//TODO: Different uarch apart from Skylake (e.g. Alder Lake, Haswell, etc.)
namespace intel {
    // Intel Skylake Conditional Branch Predictor.
    class SkylakeCBP final : public common::IntelBase {
    public:
        [[nodiscard]] const char *name() const override {
            return "Intel Skylake";
        }

        void history_update(const uint64_t seq_no, const uint8_t piece, const uint64_t PC, const bool taken,
                            const uint64_t nextPC) override {
            if (taken) {
                phr.update(PC, nextPC);
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
                    phr.update(PC, nextPC);
                    break;
                case InstClass::condBranchInstClass:
                    assert(false && "should be handled by history_update");
                default:
                    assert(false);
            }
        }

    protected:
        const common::hist_t &get_hist() override {
            return phr.value();
        }

    private:
        common::phr_t phr;
    };
}
