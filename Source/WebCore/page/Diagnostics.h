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

#pragma once

#include "ContextDestructionObserver.h"

namespace WebCore {

class Diagnostics : public ContextDestructionObserver, public RefCounted<Diagnostics> {
public:
    static Ref<Diagnostics> create(ScriptExecutionContext* context) { return adoptRef(*new Diagnostics(context)); }
    virtual ~Diagnostics();

    size_t usedRam();
    size_t usedGfx();
    int usedRamPercent();
    int usedGfxPercent();
    float imagesRamEstimate();
    float imagesGfxEstimate();
    const char* version() { return "0.1"; }

private:
    Diagnostics(ScriptExecutionContext*);

    size_t m_usedRam;
    size_t m_usedGfx;
    int m_usedRamPercent;
    int m_usedGfxPercent;
    float m_imagesRamEstimate;
    float m_imagesGfxEstimate;
};

}
