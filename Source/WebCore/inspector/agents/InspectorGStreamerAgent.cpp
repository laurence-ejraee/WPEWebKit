/*
 *  Copyright (C) 2020 Igalia S.L
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "InspectorGStreamerAgent.h"

#if USE(GSTREAMER)

#include "GStreamerCommon.h"
#include "InspectorPageAgent.h"
#include "SharedBuffer.h"

namespace WebCore {

using namespace Inspector;

using DumpActivePipelineCallback = Inspector::GStreamerBackendDispatcherHandler::DumpActivePipelineCallback;

InspectorGStreamerAgent::InspectorGStreamerAgent(PageAgentContext& context)
    : InspectorAgentBase("GStreamer"_s, context)
    , m_backendDispatcher(Inspector::GStreamerBackendDispatcher::create(context.backendDispatcher, this))
    , m_frontendDispatcher(makeUnique<Inspector::GStreamerFrontendDispatcher>(context.frontendRouter))
{
}

InspectorGStreamerAgent::~InspectorGStreamerAgent() = default;

void InspectorGStreamerAgent::didCreateFrontendAndBackend(Inspector::FrontendRouter*, Inspector::BackendDispatcher*)
{
    ASSERT(m_instrumentingAgents.persistentInspectorGStreamerAgent() != this);
    m_instrumentingAgents.setPersistentInspectorGStreamerAgent(this);
}

void InspectorGStreamerAgent::willDestroyFrontendAndBackend(Inspector::DisconnectReason)
{
    ASSERT(m_instrumentingAgents.persistentInspectorGStreamerAgent() == this);
    m_instrumentingAgents.setPersistentInspectorGStreamerAgent(nullptr);
}

void InspectorGStreamerAgent::getActivePipelinesNames(ErrorString&, RefPtr<JSON::ArrayOf<String>>& pipelines)
{
    pipelines = JSON::ArrayOf<String>::create();
    for (auto item : activePipelinesNames())
        pipelines->addItem(String::fromUTF8(item));
}

void InspectorGStreamerAgent::getActivePipelineBinNames(ErrorString&, const String& pipeline, const String& child, RefPtr<JSON::ArrayOf<String>>& names)
{
    names = JSON::ArrayOf<String>::create();
    for (auto item : getBinChildren(pipeline, child))
        names->addItem(String::fromUTF8(item));
}

void InspectorGStreamerAgent::dumpActivePipeline(const String& pipeline, const String& child, Ref<DumpActivePipelineCallback>&& dumpCallback)
{
    if (m_instrumentingAgents.persistentInspectorGStreamerAgent() != this) {
        dumpCallback->sendFailure("GStreamer domain must be enabled"_s);
        return;
    }

    WebCore::dumpActivePipeline(pipeline, child, [dumpCallback = dumpCallback.copyRef()](Ref<WebCore::SharedBuffer>&& svgData) {
        dumpCallback->sendSuccess(svgData->data());
    }, [dumpCallback = dumpCallback.copyRef()](const String& errorMessage) {
        dumpCallback->sendFailure(errorMessage);
    });
}

void InspectorGStreamerAgent::activePipelinesChanged()
{
    m_frontendDispatcher->activePipelinesChanged();
}

} // namespace WebCore

#endif // USE(GSTREAMER)