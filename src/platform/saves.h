// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"
namespace Native {
// Native saves retain the original chunk format and add an optional NRNG chunk.
uint32 validateSave(SeekableReadStream& stream);
} // namespace Native
