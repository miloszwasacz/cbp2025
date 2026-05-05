#pragma once

#ifndef CUSTOM


#ifdef TAGE2006
#include "models/tage2006.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef TAGE2006BIG
#include "models/tage2006_104KiB.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef SKYLAKE
#include "models/skylake.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef SKYLAKE_FS_SIZE
#include "models/skylake_fs_size.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef FIRESTORM
#include "models/firestorm.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef FIRESTORM_OY_SIZE
#include "models/firestorm_oy_size.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef FIRESTORM_SL_SIZE
#include "models/firestorm_sl_size.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef ORYON
#include "models/oryon.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef ORYON_FS_SIZE
#include "models/oryon_fs_size.h"
static PREDICTOR cond_predictor_impl;
#endif

#ifdef TAGE2016
#include "models/cbp2016_tage_sc_l_104KiB.h"
static CBP2016_TAGE_SC_L cond_predictor_impl;
#endif

#ifdef TAGE2016COOKBOOK
#include "models/cbp2016_tage_sc_cookbook_104KiB.h"
static PREDICTOR cond_predictor_impl;
#endif


#else


#ifdef TAGE2006
#include "models/custom/tage2006.h"
static tage::TAGE2006CBP cond_predictor_impl;
#endif

#ifdef SKYLAKE
#include "models/custom/intel/skylake.h"
static intel::SkylakeCBP cond_predictor_impl;
#endif

// #ifdef SKYLAKE_FS_BASED
// #include "models/custom/skylake_firestorm_based.h"
// static intel_new::SkylakeCBP cond_predictor_impl;
// #endif

#ifdef FIRESTORM
#include "models/custom/firestorm.h"
static apple::FirestormCBP cond_predictor_impl;
#endif

#ifdef ORYON
#include "models/custom/oryon.h"
static qualcomm::OryonCBP cond_predictor_impl;
#endif


#endif
