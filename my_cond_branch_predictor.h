#pragma once

#ifdef TAGE2006
#include "tage2006.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef TAGE2006BIG
#include "tage2006_104KiB.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef SKYLAKE
#include "skylake.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef SKYLAKECUSTOM
#include "skylake_firestorm_based.h"
static intel_new::SkylakeCBP cond_predictor_impl;
#endif

#ifdef SKYLAKECUSTOMOLD
#include "impls/intel/skylake.h"
static intel::SkylakeCBP cond_predictor_impl;
#endif

#ifdef FIRESTORM
#include "firestorm.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef FIRESTORMCUSTOM
#include "impls/firestorm.h"
static apple::FirestormCBP cond_predictor_impl;
#endif

#ifdef ORYON
#include "oryon.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef ORYONCUSTOM
#include "impls/oryon.h"
static qualcomm::OryonCBP cond_predictor_impl;
#endif

#ifdef TAGE2016
#include "cbp2016_tage_sc_l_104KiB.h"
static CBP2016_TAGE_SC_L cond_predictor_impl;
#endif

#ifdef TAGE2016COOKBOOK
#include "cbp2016_tage_sc_cookbook_104KiB.h"
static PREDICTOR cond_predictor_impl;
#endif
