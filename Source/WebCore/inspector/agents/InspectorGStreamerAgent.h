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

#pragma once

#if USE(GSTREAMER)

#include "InspectorWebAgentBase.h"
#include <JavaScriptCore/InspectorBackendDispatchers.h>
#include <JavaScriptCore/InspectorFrontendDispatchers.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

typedef String ErrorString;

class InspectorGStreamerAgent final : public InspectorAgentBase, public Inspector::GStreamerBackendDispatcherHandler {
    WTF_MAKE_NONCOPYABLE(InspectorGStreamerAgent);
    WTF_MAKE_FAST_ALLOCATED;
public:
    InspectorGStreamerAgent(PageAgentContext&);
    ~InspectorGStreamerAgent() override;

    // InspectorAgentBase
    void didCreateFrontendAndBackend(Inspector::FrontendRouter*, Inspector::BackendDispatcher*) override;
    void willDestroyFrontendAndBackend(Inspector::DisconnectReason) override;

    // GStreamerBackendDispatcherHandler
    void getActivePipelinesNames(ErrorString&, RefPtr<JSON::ArrayOf<String>>& pipelines) override;
    void getActivePipelineBinNames(ErrorString&, const String& pipeline, const String& child, RefPtr<JSON::ArrayOf<String>>& names) override;
    void dumpActivePipeline(const String& pipeline, const String& child, Ref<DumpActivePipelineCallback>&&) override;
    void activePipelinesChanged();

private:
    RefPtr<Inspector::GStreamerBackendDispatcher> m_backendDispatcher;
    std::unique_ptr<Inspector::GStreamerFrontendDispatcher> m_frontendDispatcher;
};

} // namespace WebCore

#endif // USE(GSTREAMER)