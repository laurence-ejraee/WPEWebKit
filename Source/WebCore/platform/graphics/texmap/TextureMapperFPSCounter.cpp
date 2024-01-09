/*
    Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2012, 2013 Company 100, Inc.
    Copyright (C) 2012, 2013 basysKom GmbH

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"

#include "TextureMapperFPSCounter.h"

#include "TextureMapper.h"
#include <wtf/text/WTFString.h>
#include <wtf/MemoryPressureHandler.h>

namespace WebCore {

TextureMapperFPSCounter& TextureMapperFPSCounter::singleton()
{
    static NeverDestroyed<TextureMapperFPSCounter> textureMapperFPSCounter;
    return textureMapperFPSCounter;
}

TextureMapperFPSCounter::TextureMapperFPSCounter()
    : m_isShowingFPS(false)
    , m_fpsInterval(0_s)
    , m_lastFPS(0)
    , m_frameCount(0)
    , m_isShowingMem(false)
    , m_memInterval(0_s)
    , m_lastGfxMem(0)
    , m_lastGfxPercent(0)
    , m_layersMem(0)
    , m_lastProcMem(0)
    , m_lastProcPercent(0)
{
    String showFPSEnvironment = getenv("WEBKIT_SHOW_FPS");
    bool ok = false;
    m_fpsInterval = Seconds(showFPSEnvironment.toDouble(&ok));
    if (ok && m_fpsInterval) {
        m_isShowingFPS = true;
        m_fpsTimestamp = MonotonicTime::now();
    }

    String showMemEnvironment = getenv("WEBKIT_SHOW_MEMORY");
    ok = false;
    m_memInterval = Seconds(showMemEnvironment.toDouble(&ok));
    if (ok && m_memInterval) {
        m_isShowingMem = true;
        m_memTimestamp = MonotonicTime::now();
    }
}

void TextureMapperFPSCounter::updateFPSAndDisplay(TextureMapper& textureMapper, const FloatPoint& location, const TransformationMatrix& matrix)
{
    if (!m_isShowingFPS)
        return;

    m_frameCount++;
    Seconds delta = MonotonicTime::now() - m_fpsTimestamp;
    if (delta >= m_fpsInterval) {
        m_lastFPS = int(m_frameCount / delta.seconds());
        m_frameCount = 0;
        m_fpsTimestamp += delta;
    }

    textureMapper.drawNumber(m_lastFPS, Color::black, location, matrix);
}

// TODO Could move these to their own source file
void TextureMapperFPSCounter::updateMemAndDisplay(TextureMapper& textureMapper, const FloatPoint& location, const TransformationMatrix& matrix)
{    
    if (!m_isShowingMem)
        return;

    size_t gfxValue = MemoryPressureHandler::singleton().GetUsedGpuRam();
    int gfxPercent = MemoryPressureHandler::singleton().GetUsedGpuPercent();
    size_t procValue = 0;
    int procPercent = 0;

    Seconds delta = MonotonicTime::now() - m_memTimestamp;
    if (delta >= m_memInterval) {
        m_lastGfxMem = int(gfxValue / MB);
        m_lastGfxPercent = gfxPercent;

        if (MemoryPressureHandler::singleton().GetUsedWebProcMem(procValue))
            m_lastProcMem = int(procValue / MB);

        if (MemoryPressureHandler::singleton().GetUsedWebProcPercent(procPercent))
            m_lastProcPercent = procPercent;
        
        m_memTimestamp += delta;
    }

    Color bgColor = Color(128, 128, 128, 128);
    Color layersColor = ((m_layersMem >= 95) ? ((m_layersMem >= 100) ? Color(0, 0, 255) : Color(0,128, 255)) : Color::black);
    textureMapper.drawText("Layers: " + String::number(m_layersMem) + "MB", bgColor, layersColor, location, TransformationMatrix().scale(3.0));

    textureMapper.drawText("WebProcess", bgColor, Color::black, FloatPoint(0, 12), TransformationMatrix().scale(3.0));
    textureMapper.drawText(String::number(m_lastProcMem) + "MB", bgColor, Color::black, FloatPoint(0, 24), TransformationMatrix().scale(3.0));
    String procLevel = ((m_lastProcPercent >= 95) ? ((m_lastProcPercent >= 100) ? "(critical)" : "(near-critical)") : "");
    Color procColor = ((m_lastProcPercent >= 95) ? ((m_lastProcPercent >= 100) ? Color(0, 0, 255) : Color(0,128, 255)) : Color::black);
    textureMapper.drawText(String::number(m_lastProcPercent) + "% " + procLevel, bgColor, procColor, FloatPoint(0, 36), TransformationMatrix().scale(3.0));

    const char* showGfxMem = getenv("WEBKIT_SHOW_GFX_MEMORY");
    if( showGfxMem && showGfxMem[0] != '0' ) {
        textureMapper.drawText("GFX", bgColor, Color::black, FloatPoint(0, 48), TransformationMatrix().scale(3.0));
        textureMapper.drawText(String::number(m_lastGfxMem) + "MB", bgColor, Color::black, FloatPoint(0, 60), TransformationMatrix().scale(3.0));
        // Note: Color(B, G, R) not RGB
        String gfxLevel = ((m_lastGfxPercent >= 95) ? ((m_lastGfxPercent >= 100) ? "(critical)" : "(near-critical)") : "");
        Color gfxColor = ((m_lastGfxPercent >= 95) ? ((m_lastGfxPercent >= 100) ? Color(0, 0, 255) : Color(0,128, 255)) : Color::black);
        textureMapper.drawText(String::number(m_lastGfxPercent) + "% " + gfxLevel, bgColor, gfxColor, FloatPoint(0, 72), TransformationMatrix().scale(3.0));
    }
}

void TextureMapperFPSCounter::updateLayersMem(int layersMem)
{
    m_layersMem = layersMem;
}

} // namespace WebCore
