#pragma once

#include "impls/intel/skylake.h"
#include "impls/tage2006.h"
#include "impls/firestorm.h"
#include "cbp2016_tage_sc_l.h"

// static CBP2016_TAGE_SC_L cond_predictor_impl;
// static tage::TAGE2006CBP cond_predictor_impl;
// static intel::SkylakeCBP cond_predictor_impl;
static firestorm::FirestormCBP cond_predictor_impl;
