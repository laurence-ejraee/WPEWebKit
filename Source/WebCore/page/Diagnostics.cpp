/*
 * Copyright (c) consult.Red 2024
 * Copyright © 2021 MEASAT Broadcast Network Systems Sdn Bhd 199201008561 (240064-A). All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Consult Red and MEASAT Broadcast Network Systems nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "Diagnostics.h"

#include <wtf/MemoryPressureHandler.h>

namespace WebCore {

Diagnostics::Diagnostics(ScriptExecutionContext* context)
    : ContextDestructionObserver(context)
    , m_usedRam(0)
    , m_usedGfx(0)
    , m_usedRamPercent(0)
    , m_usedGfxPercent(0)
{
}

Diagnostics::~Diagnostics() = default;

size_t Diagnostics::usedRam()
{
    size_t procValue = 0;
    if (MemoryPressureHandler::singleton().usedWebProcMemory(procValue))
        m_usedRam = int(procValue / MB);

    return m_usedRam;
}

size_t Diagnostics::usedGfx()
{
    size_t gfxValue = MemoryPressureHandler::singleton().usedGfxMemory();
    m_usedGfx = int(gfxValue / MB);

    return m_usedGfx;
}

int Diagnostics::usedRamPercent()
{
    int procPercent = 0;
    if (MemoryPressureHandler::singleton().usedWebProcPercent(procPercent))
        m_usedRamPercent = procPercent;

    return m_usedRamPercent;
}

int Diagnostics::usedGfxPercent()
{
    int gfxPercent = MemoryPressureHandler::singleton().usedGfxPercent();
    m_usedGfxPercent = gfxPercent;

    return m_usedGfxPercent;
}

float Diagnostics::imagesRamEstimate()
{
    float ramEstimate = 0.0f;
    if (MemoryPressureHandler::singleton().ramImagesEstimate(ramEstimate))
        m_imagesRamEstimate = ramEstimate;
    return m_imagesRamEstimate;
}
float Diagnostics::imagesGfxEstimate()
{
    float gfxEstimate = 0.0f;
    if (MemoryPressureHandler::singleton().gfxImagesEstimate(gfxEstimate))
        m_imagesGfxEstimate = gfxEstimate;
    return m_imagesGfxEstimate;
}
}