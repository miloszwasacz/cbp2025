#pragma once

#ifndef TAGE2006
#ifndef SKYLAKE
#ifndef FIRESTORM
#ifndef ORYON
#ifndef TAGE2016
#ifndef TAGE2016COOKBOOK
#define ORYON
#endif
#endif
#endif
#endif
#endif
#endif

#ifdef TAGE2006
#include "impls/tage2006.h"
static tage::TAGE2006CBP cond_predictor_impl;
#endif

#ifdef SKYLAKE
#include "impls/intel/skylake.h"
static intel::SkylakeCBP cond_predictor_impl;
#endif

#ifdef FIRESTORM
#include "impls/firestorm.h"
static apple::FirestormCBP cond_predictor_impl;
#endif

#ifdef ORYON
#include "impls/oryon.h"
static qualcomm::OryonCBP cond_predictor_impl;
#endif

#ifdef TAGE2016
#include "cbp2016_tage_sc_l_104KiB.h"
static CBP2016_TAGE_SC_L cond_predictor_impl;
#endif

#ifdef TAGE2016COOKBOOK
#include "cbp2016_tage_sc_cookbook.h"
static PREDICTOR cond_predictor_impl;
#endif
