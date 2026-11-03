// Shared source – edit only in firmware/_common, then run tools/common/sync_common.py
//
// FW_VERSION and FW_DEVICE (plus optional GUARD_* overrides) for the shared modules:
// from fw_id.h if the sketch has one (because its config.h holds personal data and
// variables, like the Road Buddy), otherwise from config.h.
#pragma once
#if __has_include("fw_id.h")
#include "fw_id.h"
#else
#include "config.h"
#endif
