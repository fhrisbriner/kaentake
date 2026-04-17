//
// Created by Gwen on 4/16/2026.
//
#pragma once
#include <cstdint>
#include "../ztl/tsingleton.h"
#include "../hook.h"
#include "secure.h"
#include "tempstat.h"


class CUserLocal : public TSingleton<CUserLocal, 0xBEBF98> {
};