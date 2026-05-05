/* Authors: Andre Seznec and Pierre Michaud, January 2006

Code is essentially derived  from the tagged PPM predictor simulator from Pierre Michaud and the OGEHL predictor simulator from Andre Seznec

*/

#ifndef PREDICTOR_H_SEEN
#define PREDICTOR_H_SEEN

#include <cstddef>
#include <cstdlib>
#include <bitset>
#include <cinttypes>
#include <cmath>
#include <cassert>
#include <iostream>
#include <climits>

#include "../lib/sim_common_structs.h"
#include "../perf/base_col_ctr.h"
#include "../perf/col_ctr.h"

#define LOGB 13
#define HYSTSHIFT 0
#define NHIST 6
#define CBITS 2
#define TBITS 16
#define UBITS 1
// #define LOGG 11
// #define LOGASSOC 2
// #define ASSOC (1 << LOGASSOC)

#define ASSERT(cond) if (!(cond)) {printf("assert line %d\n",__LINE__); exit(EXIT_FAILURE);}

// the predictor features NHIST tagged components + a base bimodal component
// a tagged entry in tagged table Ti (0<= i< NHIST)  features a TBITS-(i+ (NHIST & 1))/2 tag, a CBITS prediction counter and a 2-bit useful counter. 
// Tagged components feature 2**LOGG entries
// on the bimodal table: hysteresis is shared among 4 counters: total size 5*2**(LOGB-2)
//Remark a contrario from JILP paper, T0 is the table with the longest history, Sorry !

//#define FIVECOMPONENT
#ifdef FIVECOMPONENT
// a 64 Kbits predictor with 4 tagged tables
#define LOGB 13
#define NHIST 4
#define TBITS 9
#define LOGG (LOGB-3)
#endif

//#define FOURTEENCOMPONENT
#ifdef FOURTEENCOMPONENT
// a 64,5 Kbits predictor with 13 tagged tables
#define LOGB 13
#define NHIST 13
#define TBITS 15
#define LOGG (LOGB-5)
#endif


// bits per counter in the global history tables
#ifndef CBITS
#define CBITS 3
#endif

//the default predictor
// by default a 63.5  Kbits predictor, featuring 7 tagged components and a base bimodal component: NHIST = 7, LOGB =13, LOGG=9, CBITS=3
//10 Kbits for the bimodal table.
//8.5 Kbits for T0
//8 Kbits  for T1 and T2
//7.5 Kbits for T3 and T4
//7 Kbits for T5 and T6

#ifndef LOGB
#define LOGB 13
#endif

#ifndef NHIST
#define NHIST 7
#endif
// base 2 logarithm of number of entries  on each tagged component
#ifndef LOGG
#define LOGG (LOGB-4)
#endif

//Total width of an entry in the tagged table with the longest history length
#ifndef TBITS
#define TBITS 12
#endif


using namespace std;
#undef LOGG
#undef LOGASSOC
#undef ASSOC


typedef uint64_t address_t;
typedef bitset<100> phrt_t;
typedef bitset<28> phrb_t;

// all the predictor is there

class PREDICTOR {
public:
    // bimodal table entry
    class bentry {
    public:
        int8_t hyst;
        int8_t pred;

        bentry() {
            pred = 0;
            //    hyst = 0;
            hyst = 1;


#ifdef INITRAND

            pred = random() & 1;
            hyst = random() & 1;
#endif
        }
    };

    // global table entry
    class gentry {
    public:
        int8_t ctr;
        uint16_t tag;
        int8_t ubit;

        gentry() {
            ctr = 0;
            tag = 0;
            ubit = 0;

#ifdef INITRAND
            ctr = (random() & ((1 << CBITS) - 1)) - (1 << (CBITS - 1));
            tag = 0;
            ubit = (random() & 3);

#endif
        }
    };

    // predictor storage data
    int PWIN;
    // 4 bits to determine whether newly allocated entries should be considered as
    // valid or not for delivering  the prediction
    int TICK;
    // use a path history as for the OGEHL predictor
    phrt_t phrt;
    phrb_t phrb;
    bentry *btable;
    gentry *gtable[NHIST];
    size_t logg[NHIST] = {10, 10, 10, 11, 11, 11};
    size_t assoc[NHIST] = {4, 4, 4, 4, 6, 6};


    perf::base_col_ctr *b_col_ctrs[2];
    size_t dir_flips = 0;
    perf::collision_ctr *g_col_ctrs[NHIST];

    PREDICTOR() {
        phrt = 0;
        phrb = 0;

        btable = new bentry[1 << LOGB];
        for (int i = 0; i < NHIST; i++) {
            gtable[i] = new gentry[(1 << logg[i]) * assoc[i]];
        }

        for (auto &b_col_ctr: b_col_ctrs) {
            b_col_ctr = new perf::base_col_ctr(1 << LOGB);
        }
        for (size_t i = 0; i < NHIST; i++) {
            g_col_ctrs[i] = new perf::collision_ctr(assoc[i], 1 << logg[i]);
        }
    }

    void setup() {
        int STORAGESIZE = 0;


        for (int i = NHIST - 1; i >= 0; i--) {
            STORAGESIZE += (1 << logg[i]) * (CBITS + UBITS + TBITS) * assoc[i];
        }
        STORAGESIZE += (1 << LOGB) + (1 << (LOGB - HYSTSHIFT));
#ifdef PRINT_SIZE
        printf("Testing Qualcomm Oryon CBP (%d bits, %d KiB)\n", STORAGESIZE, STORAGESIZE / (1024 * 8));
#else
        printf("Testing Qualcomm Oryon CBP\n");
#endif
    }

    void terminate() {
        std::cout <<
                "-----------------------------------------------------------Table Statistics------------------------------------------------------------"
                << std::endl;
        for (int i = 0; i < NHIST; ++i) {
            std::cout << "PHT #" << NHIST - i << ':' << std::endl;
            g_col_ctrs[i]->print_stats(1);
            std::cout << std::endl;
        }
        std::cout << "Base predictor:" << std::endl;
#if HYSTSHIFT == 0
        b_col_ctrs[0]->print_stats(1);
        std::cout << "\tDirection flips:\t" << dir_flips << std::endl;
#else
        std::cout << "\tDirection table:" << std::endl;
        b_col_ctrs[0]->print_stats(2);
        std::cout << "\t\tDirection flips:\t" << dir_flips << std::endl;

        std::cout << "\tHysteresis table:" << std::endl;
        b_col_ctrs[1]->print_stats(2);
#endif
        std::cout <<
                "---------------------------------------------------------------------------------------------------------------------------------------"
                << std::endl;
    }


    // index function for the bimodal table

    int bindex(address_t pc) {
        return ((pc >> 2) & ((1 << (LOGB)) - 1));
    }

    // indexes to the different tables are computed only once  and store in GI and BI
    int GI[NHIST];
    int BI;

#define PUSH_BIT(bit) { fold <<= 1; fold |= bit; }

    int gindex(address_t PC, int bank) const {
        assert(sizeof(int) * CHAR_BIT >= logg[bank]);
        const bitset<sizeof(uint64_t) * CHAR_BIT> pc = PC;
        int fold = 0;
        // NOTE: These functions are extrapolated from the `firestorm.toml`
        //       config and values mentioned in the paper, since there is
        //       no proper config file for Oryon.
        switch (bank) {
            case 0:
                PUSH_BIT(pc[6]);
                PUSH_BIT(phrt[54] ^ phrt[60] ^ phrb[0]);
                PUSH_BIT(phrt[39] ^ phrt[90] ^ phrb[30]);
                PUSH_BIT(phrt[34] ^ phrt[85] ^ phrb[25]);
                PUSH_BIT(phrt[29] ^ phrt[80] ^ phrb[20]);
                PUSH_BIT(phrt[24] ^ phrt[75] ^ phrb[15]);
                PUSH_BIT(phrt[19] ^ phrt[70] ^ phrb[10]);
                PUSH_BIT(phrt[14] ^ phrt[65] ^ phrb[5]);
                PUSH_BIT(phrt[8] ^ phrt[49] ^ pc[7]);
                PUSH_BIT(phrt[3] ^ phrt[44] ^ phrt[95]);
                break;
            case 1:
                PUSH_BIT(pc[6]);
                PUSH_BIT(phrt[34] ^ phrb[3] ^ phrb[30]);
                PUSH_BIT(phrt[24] ^ phrb[31] ^ phrb[0]);
                PUSH_BIT(phrt[21] ^ phrb[28] ^ phrb[26]);
                PUSH_BIT(phrt[18] ^ phrb[23] ^ pc[9]);
                PUSH_BIT(phrt[14] ^ phrt[51] ^ phrb[20]);
                PUSH_BIT(phrt[11] ^ phrt[48] ^ phrb[16]);
                PUSH_BIT(phrt[8] ^ phrt[44] ^ phrb[13]);
                PUSH_BIT(phrt[4] ^ phrt[41] ^ phrb[10]);
                PUSH_BIT(phrt[1] ^ phrt[38] ^ phrb[6]);
                break;
            case 2:
                PUSH_BIT(pc[6]);
                PUSH_BIT(phrt[26] ^ phrb[8] ^ phrb[26]);
                PUSH_BIT(phrt[24] ^ phrb[6] ^ phrb[24]);
                PUSH_BIT(phrt[21] ^ phrb[4] ^ phrb[22]);
                PUSH_BIT(phrt[19] ^ phrb[2] ^ phrb[20]);
                PUSH_BIT(phrt[10] ^ phrt[17] ^ phrb[0]);
                PUSH_BIT(phrt[8] ^ phrt[15] ^ phrb[17]);
                PUSH_BIT(phrt[6] ^ phrb[15] ^ phrb[13]);
                PUSH_BIT(phrt[4] ^ phrb[13] ^ pc[10]);
                PUSH_BIT(phrt[1] ^ phrb[11] ^ pc[8]);
                break;
            case 3:
                PUSH_BIT(pc[6]);
                PUSH_BIT(phrb[3] ^ phrb[13] ^ pc[9]);
                PUSH_BIT(phrb[2] ^ phrb[12] ^ pc[8]);
                PUSH_BIT(phrb[1] ^ phrb[11] ^ pc[7]);
                PUSH_BIT(phrt[7] ^ phrt[13] ^ phrb[0]);
                PUSH_BIT(phrt[6] ^ phrt[12] ^ phrb[10]);
                PUSH_BIT(phrt[5] ^ phrt[10] ^ phrb[9]);
                PUSH_BIT(phrt[4] ^ phrt[9] ^ phrb[7]);
                PUSH_BIT(phrt[3] ^ phrt[8] ^ phrb[6]);
                PUSH_BIT(phrt[1] ^ phrb[5] ^ pc[11]);
                PUSH_BIT(phrt[0] ^ phrb[4] ^ pc[10]);
                break;
            case 4:
                PUSH_BIT(pc[6]);
                PUSH_BIT(phrb[3] ^ pc[10]);
                PUSH_BIT(phrb[2] ^ pc[9]);
                PUSH_BIT(phrb[1] ^ pc[8]);
                PUSH_BIT(phrb[0] ^ pc[7]);
                PUSH_BIT(phrt[6] ^ phrb[5]);
                PUSH_BIT(phrt[5] ^ phrb[4]);
                PUSH_BIT(phrt[4] ^ pc[13]);
                PUSH_BIT(phrt[3] ^ pc[12]);
                PUSH_BIT(phrt[2] ^ pc[11]);
                PUSH_BIT(phrt[0] ^ phrt[1] ^ phrb[6]);
                break;
            case 5:
                PUSH_BIT(pc[6]);
                PUSH_BIT(pc[19]);
                PUSH_BIT(pc[18]);
                PUSH_BIT(phrb[3] ^ pc[11]);
                PUSH_BIT(phrb[2] ^ pc[10]);
                PUSH_BIT(phrb[1] ^ pc[9]);
                PUSH_BIT(phrb[0] ^ pc[7] ^ pc[8]);
                PUSH_BIT(phrt[3] ^ pc[17]);
                PUSH_BIT(phrt[2] ^ pc[16]);
                PUSH_BIT(phrt[1] ^ pc[15]);
                PUSH_BIT(phrt[0] ^ pc[13] ^ pc[14]);
                break;
            default:
                assert(false);
        }
        return fold;
    }

    //  tag computation
    uint16_t gtag(address_t PC, const int bank) const {
        static_assert(sizeof(uint16_t) * CHAR_BIT >= TBITS);
        const bitset<sizeof(uint64_t) * CHAR_BIT> pc = PC;
        uint16_t fold = 0;
        switch (bank) {
            case 0:
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[0] ^ phrt[12] ^ phrt[24] ^ phrt[36] ^ phrt[48] ^ phrt[60] ^ phrt[72] ^ phrt[84] ^ phrt[96] ^ phrb[0] ^ phrb[12] ^ phrb[24]);
                PUSH_BIT(phrt[11] ^ phrt[23] ^ phrt[35] ^ phrt[47] ^ phrt[59] ^ phrt[71] ^ phrt[83] ^ phrt[95] ^ phrb[11] ^ phrb[23]);
                PUSH_BIT(phrt[10] ^ phrt[22] ^ phrt[34] ^ phrt[46] ^ phrt[58] ^ phrt[70] ^ phrt[82] ^ phrt[94] ^ phrb[10] ^ phrb[22]);
                PUSH_BIT(phrt[9] ^ phrt[21] ^ phrt[33] ^ phrt[45] ^ phrt[57] ^ phrt[69] ^ phrt[81] ^ phrt[93] ^ phrb[9] ^ phrb[21]);
                PUSH_BIT(phrt[8] ^ phrt[20] ^ phrt[32] ^ phrt[44] ^ phrt[56] ^ phrt[68] ^ phrt[80] ^ phrt[92] ^ phrb[8] ^ phrb[20]);
                PUSH_BIT(phrt[7] ^ phrt[19] ^ phrt[31] ^ phrt[43] ^ phrt[55] ^ phrt[67] ^ phrt[79] ^ phrt[91] ^ phrb[7] ^ phrb[19] ^ phrb[31]);
                PUSH_BIT(phrt[6] ^ phrt[18] ^ phrt[30] ^ phrt[42] ^ phrt[54] ^ phrt[66] ^ phrt[78] ^ phrt[90] ^ phrb[6] ^ phrb[18] ^ phrb[30]);
                PUSH_BIT(pc[12] ^ phrt[5] ^ phrt[17] ^ phrt[29] ^ phrt[41] ^ phrt[53] ^ phrt[65] ^ phrt[77] ^ phrt[89] ^ phrb[5] ^ phrb[17] ^ phrb[29]);
                PUSH_BIT(pc[11] ^ phrt[4] ^ phrt[16] ^ phrt[28] ^ phrt[40] ^ phrt[52] ^ phrt[64] ^ phrt[76] ^ phrt[88] ^ phrb[4] ^ phrb[16] ^ phrb[28]);
                PUSH_BIT(pc[10] ^ phrt[3] ^ phrt[15] ^ phrt[27] ^ phrt[39] ^ phrt[51] ^ phrt[63] ^ phrt[75] ^ phrt[87] ^ phrt[99] ^ phrb[3] ^ phrb[15] ^ phrb[27]);
                PUSH_BIT(pc[9] ^ phrt[2] ^ phrt[14] ^ phrt[26] ^ phrt[38] ^ phrt[50] ^ phrt[62] ^ phrt[74] ^ phrt[86] ^ phrt[98] ^ phrb[2] ^ phrb[14] ^ phrb[26]);
                PUSH_BIT(pc[8] ^ phrt[1] ^ phrt[13] ^ phrt[25] ^ phrt[37] ^ phrt[49] ^ phrt[61] ^ phrt[73] ^ phrt[85] ^ phrt[97] ^ phrb[1] ^ phrb[13] ^ phrb[25]);
                break;
            case 1:
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[0] ^ phrt[12] ^ phrt[24] ^ phrt[36] ^ phrt[48] ^ phrb[0] ^ phrb[12] ^ phrb[24]);
                PUSH_BIT(phrt[11] ^ phrt[23] ^ phrt[35] ^ phrt[47] ^ phrb[11] ^ phrb[23]);
                PUSH_BIT(phrt[10] ^ phrt[22] ^ phrt[34] ^ phrt[46] ^ phrb[10] ^ phrb[22]);
                PUSH_BIT(phrt[9] ^ phrt[21] ^ phrt[33] ^ phrt[45] ^ phrb[9] ^ phrb[21]);
                PUSH_BIT(phrt[8] ^ phrt[20] ^ phrt[32] ^ phrt[44] ^ phrb[8] ^ phrb[20]);
                PUSH_BIT(phrt[7] ^ phrt[19] ^ phrt[31] ^ phrt[43] ^ phrb[7] ^ phrb[19] ^ phrb[31]);
                PUSH_BIT(phrt[6] ^ phrt[18] ^ phrt[30] ^ phrt[42] ^ phrb[6] ^ phrb[18] ^ phrb[30]);
                PUSH_BIT(pc[12] ^ phrt[5] ^ phrt[17] ^ phrt[29] ^ phrt[41] ^ phrb[5] ^ phrb[17] ^ phrb[29]);
                PUSH_BIT(pc[11] ^ phrt[4] ^ phrt[16] ^ phrt[28] ^ phrt[40] ^ phrb[4] ^ phrb[16] ^ phrb[28]);
                PUSH_BIT(pc[10] ^ phrt[3] ^ phrt[15] ^ phrt[27] ^ phrt[39] ^ phrt[51] ^ phrb[3] ^ phrb[15] ^ phrb[27]);
                PUSH_BIT(pc[9] ^ phrt[2] ^ phrt[14] ^ phrt[26] ^ phrt[38] ^ phrt[50] ^ phrb[2] ^ phrb[14] ^ phrb[26]);
                PUSH_BIT(pc[8] ^ phrt[1] ^ phrt[13] ^ phrt[25] ^ phrt[37] ^ phrt[49] ^ phrb[1] ^ phrb[13] ^ phrb[25]);
                break;
            case 2:
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[0] ^ phrt[12] ^ phrt[24] ^ phrb[0] ^ phrb[12] ^ phrb[24]);
                PUSH_BIT(phrt[11] ^ phrt[23] ^ phrb[11] ^ phrb[23]);
                PUSH_BIT(phrt[10] ^ phrt[22] ^ phrb[10] ^ phrb[22]);
                PUSH_BIT(phrt[9] ^ phrt[21] ^ phrb[9] ^ phrb[21]);
                PUSH_BIT(phrt[8] ^ phrt[20] ^ phrb[8] ^ phrb[20]);
                PUSH_BIT(phrt[7] ^ phrt[19] ^ phrb[7] ^ phrb[19]);
                PUSH_BIT(phrt[6] ^ phrt[18] ^ phrb[6] ^ phrb[18]);
                PUSH_BIT(pc[12] ^ phrt[5] ^ phrt[17] ^ phrb[5] ^ phrb[17]);
                PUSH_BIT(pc[11] ^ phrt[4] ^ phrt[16] ^ phrb[4] ^ phrb[16]);
                PUSH_BIT(pc[10] ^ phrt[3] ^ phrt[15] ^ phrb[3] ^ phrb[15]);
                PUSH_BIT(pc[9] ^ phrt[2] ^ phrt[14] ^ phrt[26] ^ phrb[2] ^ phrb[14] ^ phrb[26]);
                PUSH_BIT(pc[8] ^ phrt[1] ^ phrt[13] ^ phrt[25] ^ phrb[1] ^ phrb[13] ^ phrb[25]);
                break;
            case 3:
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[0] ^ phrt[12] ^ phrb[0] ^ phrb[12]);
                PUSH_BIT(phrt[11] ^ phrb[11]);
                PUSH_BIT(phrt[10] ^ phrb[10]);
                PUSH_BIT(phrt[9] ^ phrb[9]);
                PUSH_BIT(phrt[8] ^ phrb[8]);
                PUSH_BIT(phrt[7] ^ phrb[7]);
                PUSH_BIT(phrt[6] ^ phrb[6]);
                PUSH_BIT(pc[12] ^ phrt[5] ^ phrb[5]);
                PUSH_BIT(pc[11] ^ phrt[4] ^ phrb[4]);
                PUSH_BIT(pc[10] ^ phrt[3] ^ phrb[3]);
                PUSH_BIT(pc[9] ^ phrt[2] ^ phrb[2]);
                PUSH_BIT(pc[8] ^ phrt[1] ^ phrt[13] ^ phrb[1] ^ phrb[13]);
                break;
            case 4:
                // NOTE: These functions are very low confidence, since there is
                //       no config file and they differ from the previous pattern.
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[6] ^ phrb[6]);
                PUSH_BIT(phrt[5] ^ phrb[5]);
                PUSH_BIT(phrt[4] ^ phrb[4]);
                PUSH_BIT(phrt[3] ^ phrb[3]);
                PUSH_BIT(phrt[2] ^ phrb[2]);
                PUSH_BIT(phrt[1] ^ phrb[1]);
                PUSH_BIT(phrt[0] ^ phrb[0]);
                PUSH_BIT(pc[12]);
                PUSH_BIT(pc[11]);
                PUSH_BIT(pc[10]);
                PUSH_BIT(pc[9]);
                PUSH_BIT(pc[8]);
                break;
            case 5:
                // NOTE: These functions are very low confidence, since there is
                //       no config file and they differ from the previous pattern.
                PUSH_BIT(pc[2]);
                PUSH_BIT(pc[3]);
                PUSH_BIT(pc[4]);
                PUSH_BIT(pc[5]);
                PUSH_BIT(phrt[2]);
                PUSH_BIT(phrt[1]);
                PUSH_BIT(phrt[0]);
                PUSH_BIT(phrt[3] ^ phrb[3]);
                PUSH_BIT(phrb[2]);
                PUSH_BIT(phrb[1]);
                PUSH_BIT(phrb[0]);
                PUSH_BIT(pc[12]);
                PUSH_BIT(pc[11]);
                PUSH_BIT(pc[10]);
                PUSH_BIT(pc[9]);
                PUSH_BIT(pc[8]);
                break;
            default:
                assert(false);
        }
        return fold;
    }
#undef PUSH_BIT


    // up-down saturating counter
    void ctrupdate(int8_t &ctr, bool taken, int nbits) {
        if (taken) {
            if (ctr < ((1 << (nbits - 1)) - 1)) {
                ctr++;
            }
        } else {
            if (ctr > -(1 << (nbits - 1))) {
                ctr--;
            }
        }
    }

    int bank;
    int way;
    int altbank;
    int altway;
    // prediction given by longest matching global history
    // altpred contains the alternate prediction
    bool read_prediction(address_t pc, bool &altpred) {
        bank = NHIST;
        altbank = NHIST;


        {
            for (int i = 0; i < NHIST; i++) {
                for (int j = 0; j < assoc[i]; j++) {
                    if (gtable[i][GI[i] + j].tag == gtag(pc, i)) {
                        bank = i;
                        way = j;
                        goto l1;
                    }
                }
            }
        l1:
            for (int i = bank + 1; i < NHIST; i++) {
                for (int j = 0; j < assoc[i]; j++) {
                    if (gtable[i][GI[i] + j].tag == gtag(pc, i)) {
                        altbank = i;
                        altway = j;
                        goto l2;
                    }
                }
            }
        l2:
            if (bank < NHIST) {
                if (altbank < NHIST)
                    altpred = (gtable[altbank][GI[altbank] + altway].ctr >= 0);
                else
                    altpred = getbim(pc);
                //if the entry is recognized as a newly allocated entry and
                //counter PWIN is negative use the alternate prediction
                // see section 3.2.4
                // if ((PWIN < 0) || (abs (2 * gtable[bank][GI[bank] + way].ctr + 1) != 1)
                //     || (gtable[bank][GI[bank] + way].ubit != 0))

                return (gtable[bank][GI[bank] + way].ctr >= 0);
                // else
                //   return (altpred);
            } else {
                altpred = getbim(pc);

                return altpred;
            }
        }
    }

    // PREDICTION
    bool pred_taken, alttaken;

    bool get_prediction(const address_t pc) {
        // computes the table addresses
        for (int i = 0; i < NHIST; i++)
            GI[i] = gindex(pc, i) * assoc[i];
        BI = bindex(pc);

        pred_taken = read_prediction(pc, alttaken);
        // bank contains the number of the matching table, NHIST if no match
        // pred_taken is the prediction
        // alttaken is the alternate prediction
        return pred_taken;
    }

    bool getbim(address_t pc) {
        return (btable[BI].pred > 0);
    }

    // update  the bimodal predictor
    void baseupdate(address_t pc, bool Taken) {
        //just a normal 2-bit counter apart that hysteresis is shared
        const bool old_pred = getbim(pc);
        if (Taken == old_pred) {
            if (Taken) {
                if (btable[BI].pred)
                    btable[BI >> HYSTSHIFT].hyst = 1;
            } else {
                if (!btable[BI].pred)
                    btable[BI >> HYSTSHIFT].hyst = 0;
            }
        } else {
            int inter = (btable[BI].pred << 1) + btable[BI >> HYSTSHIFT].hyst;
            if (Taken) {
                if (inter < 3)
                    inter += 1;
            } else {
                if (inter > 0)
                    inter--;
            }

            btable[BI].pred = inter >> 1;
            btable[BI >> HYSTSHIFT].hyst = (inter & 1);
        }

        b_col_ctrs[0]->insert(pc, BI);
#if HYSTSHIFT != 0
        b_col_ctrs[1]->insert(pc, BI);
#endif
        if (old_pred != getbim(pc)) {
            dir_flips++;
        }
    }

    //just building our own simple pseudo random number generator based on linear feedback shift register
    int Seed;


    int MYRANDOM() {
        Seed = ((1 << 2 * NHIST) + 1) * Seed + 0xf3f531;
        Seed = (Seed & ((1 << (2 * (NHIST))) - 1));
        return (Seed);
    };


    // PREDICTOR UPDATE
    void update_predictor(const address_t pc, const InstClass op, const bool taken, const address_t nextPC) {
        bool is_conditional = false;
        switch (op) {
            case InstClass::condBranchInstClass:
                is_conditional = true;
                break;
            case InstClass::uncondDirectBranchInstClass:
            case InstClass::uncondIndirectBranchInstClass:
            case InstClass::callDirectInstClass:
            case InstClass::callIndirectInstClass:
            case InstClass::ReturnInstClass:
                is_conditional = false;
                assert(taken);
                break;
            default:
                assert(false);
        }

        // int NRAND = MYRANDOM ();

        if (is_conditional) {
            // in a real processor, it is not necessary to re-read the predictor at update
            // it suffices to propagate the prediction along with the branch instruction
            bool ALLOC = ((pred_taken != taken) & (bank > 0));


            // 	if (bank < NHIST)
            // 	  {
            // 	    bool loctaken = (gtable[bank][GI[bank] + way].ctr >= 0);
            // 	    bool PseudoNewAlloc =
            // 	      (abs (2 * gtable[bank][GI[bank] + way].ctr + 1) == 1)
            // 	      && (gtable[bank][GI[bank] + way].ubit == 0);
            // // is entry "pseudo-new allocated"
            //
            // 	    if (PseudoNewAlloc)
            // 	      {
            //
            // 		if (loctaken == taken)
            // 		  ALLOC = false;
            // // if the provider component  was delivering the correct prediction; no need to allocate a new entry
            // //even if the overall prediction was false
            //
            //
            // //see section 3.2.4
            // 		if (loctaken != alttaken)
            // 		  {
            // 		    if (alttaken == taken)
            // 		      {
            //
            // 			if (PWIN < 7)
            // 			  PWIN++;
            // 		      }
            //
            // 		    else if (PWIN > -8)
            // 		      PWIN--;
            // 		  }
            // 	      }
            // 	  }


            // try to allocate a  new entries only if prediction was wrong
            if (ALLOC) {
                // is there some "unuseful" entry to allocate
                int8_t min = 3;
                for (int i = 0; i < bank; i++) {
                    for (int j = 0; j < assoc[i]; j++) {
                        if (gtable[i][GI[i] + j].ubit < min)
                            min = gtable[i][GI[i] + j].ubit;
                    }
                }

                if (min > 0) {
                    //NO UNUSEFUL ENTRY TO ALLOCATE: age all possible targets, but do not allocate
                    for (int i = bank - 1; i >= 0; i--) {
                        for (int j = 0; j < assoc[i]; j++) {
                            gtable[i][GI[i] + j].ubit--;
                        }
                    }
                } else {
                    //YES: allocate one entry, but apply some randomness
                    // bank I is twice more probable than bank I-1


                    // int Y = NRAND & ((1 << (bank - 1)) - 1);
                    int X = bank - 1;
                    // while ((Y & 1) != 0)
                    //   {
                    //     X--;
                    //     Y >>= 1;
                    //
                    //   }


                    for (int i = X; i >= 0; i--) {
                        int T = i;
                        for (int j = 0; j < assoc[i]; j++) {
                            if (gtable[T][GI[T] + j].ubit == min) {
                                gtable[T][GI[T] + j].tag = gtag(pc, T);
                                gtable[T][GI[T] + j].ctr = (taken) ? 0 : -1;
                                gtable[T][GI[T] + j].ubit = 0;
                                g_col_ctrs[T]->insert(pc, GI[T]);
                                goto l3;
                            }
                        }
                    }
                l3:


                }
            }


            //periodic reset of ubit: reset is not complete but bit by bit
            TICK++;
            if ((TICK & ((1 << 18) - 1)) == 0) {
                int X = (TICK >> 18) & 1;
                if ((X & 1) == 0)
                    X = 2;
                for (int i = 0; i < NHIST; i++)
                    for (int j = 0; j < ((1 << logg[i]) * assoc[i]); j++)
                        gtable[i][j].ubit = gtable[i][j].ubit & X;
            }

            // update the counter that provided the prediction, and only this counter
            if (bank < NHIST) {
                ctrupdate(gtable[bank][GI[bank] + way].ctr, taken, CBITS);
            } else {
                baseupdate(pc, taken);
            }
            // update the ubit counter
            if ((pred_taken != alttaken)) {
                ASSERT(bank < NHIST);


                if (pred_taken == taken) {
                    if (gtable[bank][GI[bank] + way].ubit < ((1 << UBITS) - 1))
                        gtable[bank][GI[bank] + way].ubit++;
                } else {
                    if (gtable[bank][GI[bank] + way].ubit > 0)
                        gtable[bank][GI[bank] + way].ubit--;
                }
            }
        }
        // update global history and cyclic shift registers
        //use also history on unconditional branches as for OGEHL predictors.


        if (!is_conditional | taken) {
            // Update PHRT
            phrt <<= 1;
            constexpr uint64_t TMASK = (1 << (31 - 2 + 1)) - 1;
            phrt ^= ((nextPC >> 2) & TMASK);

            // Update PHRB
            phrb <<= 1;
            constexpr uint64_t BMASK = (1 << (5 - 2 + 1)) - 1;
            phrb ^= ((pc >> 2) & BMASK);
        }
    }
};
#endif // PREDICTOR_H_SEEN
