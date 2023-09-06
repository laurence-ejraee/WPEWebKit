/*
 *  Copyright (C) 2009, 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  Copyright (C) 2013 Collabora Ltd.
 *  Copyright (C) 2013 Orange
 *  Copyright (C) 2014, 2015 Sebastian Dröge <sebastian@centricular.com>
 *  Copyright (C) 2015, 2016 Metrological Group B.V.
 *  Copyright (C) 2015, 2016 Igalia, S.L
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
#include "WebKitMediaSourceGStreamer.h"

#include "PlaybackPipeline.h"

#if ENABLE(VIDEO) && ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)

#include "AudioTrackPrivateGStreamer.h"
#include "GStreamerCommon.h"
#include "MediaDescription.h"
#include "MediaPlayerPrivateGStreamerMSE.h"
#include "MediaSample.h"
#include "MediaSourcePrivateGStreamer.h"
#include "NotImplemented.h"
#include "SourceBufferPrivateGStreamer.h"
#include "TimeRanges.h"
#include "VideoTrackPrivateGStreamer.h"
#include "WebKitMediaSourceGStreamerPrivate.h"

#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <wtf/Condition.h>
#include <wtf/MainThread.h>
#include <wtf/RefPtr.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY_STATIC(webkit_media_src_debug);
#define GST_CAT_DEFAULT webkit_media_src_debug

#define webkit_media_src_parent_class parent_class
#define WEBKIT_MEDIA_SRC_CATEGORY_INIT GST_DEBUG_CATEGORY_INIT(webkit_media_src_debug, "webkitmediasrc", 0, "websrc element");

static GstStaticPadTemplate srcTemplate = GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC,
    GST_PAD_SOMETIMES, GST_STATIC_CAPS_ANY);

static void enabledAppsrcNeedData(GstAppSrc*, guint, gpointer);
static void enabledAppsrcEnoughData(GstAppSrc*, gpointer);
static gboolean enabledAppsrcSeekData(GstAppSrc*, guint64, gpointer);

static void disabledAppsrcNeedData(GstAppSrc*, guint, gpointer) { };
static void disabledAppsrcEnoughData(GstAppSrc*, gpointer) { };
static gboolean disabledAppsrcSeekData(GstAppSrc*, guint64, gpointer)
{
    return FALSE;
};

GstAppSrcCallbacks enabledAppsrcCallbacks = {
    enabledAppsrcNeedData,
    enabledAppsrcEnoughData,
    enabledAppsrcSeekData,
    { 0 }
};

GstAppSrcCallbacks disabledAppsrcCallbacks = {
    disabledAppsrcNeedData,
    disabledAppsrcEnoughData,
    disabledAppsrcSeekData,
    { 0 }
};

static Stream* getStreamByAppsrc(WebKitMediaSrc*, GstElement*);
static void seekNeedsDataMainThread(WebKitMediaSrc*);
static void notifyReadyForMoreSamplesMainThread(WebKitMediaSrc*, Stream*);

static WebKitMediaSrcMainThreadNotification streamTypeToNotificationType(WebCore::MediaSourceStreamTypeGStreamer type)
{
    switch(type) {
    case WebCore::Video:
        return WebKitMediaSrcMainThreadNotification::VideoReadyForMoreSamples;
    case WebCore::Audio:
        return WebKitMediaSrcMainThreadNotification::AudioReadyForMoreSamples;
    case WebCore::Text:
        return WebKitMediaSrcMainThreadNotification::TextReadyForMoreSamples;
    default:
        break;
    }
    RELEASE_ASSERT_NOT_REACHED();
    return WebKitMediaSrcMainThreadNotification::VideoReadyForMoreSamples;
}

static void enabledAppsrcNeedData(GstAppSrc* appsrc, guint, gpointer userData)
{
    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);
    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));

    GST_OBJECT_LOCK(webKitMediaSrc);
    OnSeekDataAction appsrcSeekDataNextAction = webKitMediaSrc->priv->appsrcSeekDataNextAction;
    Stream* appsrcStream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));
    bool allAppsrcNeedDataAfterSeek = false;

    if (webKitMediaSrc->priv->appsrcSeekDataCount > 0) {
        if (appsrcStream && !appsrcStream->appsrcNeedDataFlag) {
            ++webKitMediaSrc->priv->appsrcNeedDataCount;
            appsrcStream->appsrcNeedDataFlag = true;
        }
        int numAppsrcs = webKitMediaSrc->priv->streams.size();
        if (webKitMediaSrc->priv->appsrcSeekDataCount == numAppsrcs && webKitMediaSrc->priv->appsrcNeedDataCount == numAppsrcs) {
            GST_DEBUG("All needDatas completed");
            allAppsrcNeedDataAfterSeek = true;
            webKitMediaSrc->priv->appsrcSeekDataCount = 0;
            webKitMediaSrc->priv->appsrcNeedDataCount = 0;
            webKitMediaSrc->priv->appsrcSeekDataNextAction = Nothing;

            for (Stream* stream : webKitMediaSrc->priv->streams)
                stream->appsrcNeedDataFlag = false;
        }
    }
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    if (allAppsrcNeedDataAfterSeek) {
        GST_DEBUG("All expected appsrcSeekData() and appsrcNeedData() calls performed. Running next action (%d)", static_cast<int>(appsrcSeekDataNextAction));

        switch (appsrcSeekDataNextAction) {
        case MediaSourceSeekToTime:
            webKitMediaSrc->priv->notifier->notify(WebKitMediaSrcMainThreadNotification::SeekNeedsData, [webKitMediaSrc] {
                seekNeedsDataMainThread(webKitMediaSrc);
            });
            break;
        case Nothing:
            break;
        }
    } else if (appsrcSeekDataNextAction == Nothing) {
        LockHolder locker(webKitMediaSrc->priv->streamLock);

        GST_OBJECT_LOCK(webKitMediaSrc);

        // Search again for the Stream, just in case it was removed between the previous lock and this one.
        appsrcStream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));

        if (appsrcStream && appsrcStream->type != WebCore::Invalid) {
            auto notificationType = streamTypeToNotificationType(appsrcStream->type);

            webKitMediaSrc->priv->notifier->notify(notificationType, [webKitMediaSrc, appsrcStream] {
                notifyReadyForMoreSamplesMainThread(webKitMediaSrc, appsrcStream);
            });
        }

        GST_OBJECT_UNLOCK(webKitMediaSrc);
    }
}

static void enabledAppsrcEnoughData(GstAppSrc *appsrc, gpointer userData)
{
    // No need to lock on webKitMediaSrc, we're on the main thread and nobody is going to remove the stream in the meantime.
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);
    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));
    GST_OBJECT_LOCK(webKitMediaSrc);

    Stream* stream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));

    // This callback might have been scheduled from a child thread before the stream was removed.
    // Then, the removal code might have run, and later this callback.
    // This check solves the race condition.
    if (!stream || stream->type == WebCore::Invalid)
        return;

    auto notificationType = streamTypeToNotificationType(stream->type);
    webKitMediaSrc->priv->notifier->cancelPendingNotifications(notificationType);
    stream->sourceBuffer->setReadyForMoreSamples(false);

    GST_OBJECT_UNLOCK(webKitMediaSrc);
}

static gboolean enabledAppsrcSeekData(GstAppSrc*, guint64, gpointer userData)
{
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);

    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));

    GST_OBJECT_LOCK(webKitMediaSrc);
    webKitMediaSrc->priv->appsrcSeekDataCount++;
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    return TRUE;
}

static Stream* getStreamByAppsrc(WebKitMediaSrc* source, GstElement* appsrc)
{
    for (Stream* stream : source->priv->streams) {
        if (stream->appsrc == appsrc)
            return stream;
    }
    return nullptr;
}

G_DEFINE_TYPE_WITH_CODE(WebKitMediaSrc, webkit_media_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, webKitMediaSrcUriHandlerInit);
    WEBKIT_MEDIA_SRC_CATEGORY_INIT);

guint webKitMediaSrcSignals[LAST_SIGNAL] = { 0 };

static void webkit_media_src_class_init(WebKitMediaSrcClass* klass)
{
    GObjectClass* oklass = G_OBJECT_CLASS(klass);
    GstElementClass* eklass = GST_ELEMENT_CLASS(klass);

    oklass->finalize = webKitMediaSrcFinalize;
    oklass->set_property = webKitMediaSrcSetProperty;
    oklass->get_property = webKitMediaSrcGetProperty;

    gst_element_class_add_pad_template(eklass, gst_static_pad_template_get(&srcTemplate));

    gst_element_class_set_static_metadata(eklass, "WebKit Media source element", "Source", "Handles Blob uris", "Stephane Jadaud <sjadaud@sii.fr>, Sebastian Dröge <sebastian@centricular.com>, Enrique Ocaña González <eocanha@igalia.com>");

    // Allows setting the uri using the 'location' property, which is used for example by gst_element_make_from_uri().
    g_object_class_install_property(oklass,
        PROP_LOCATION,
        g_param_spec_string("location", "location", "Location to read from", nullptr,
        GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_AUDIO,
        g_param_spec_int("n-audio", "Number Audio", "Total number of audio streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_VIDEO,
        g_param_spec_int("n-video", "Number Video", "Total number of video streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_TEXT,
        g_param_spec_int("n-text", "Number Text", "Total number of text streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    webKitMediaSrcSignals[SIGNAL_VIDEO_CHANGED] =
        g_signal_new("video-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, videoChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
    webKitMediaSrcSignals[SIGNAL_AUDIO_CHANGED] =
        g_signal_new("audio-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, audioChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
    webKitMediaSrcSignals[SIGNAL_TEXT_CHANGED] =
        g_signal_new("text-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, textChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

    eklass->change_state = webKitMediaSrcChangeState;

    g_type_class_add_private(klass, sizeof(WebKitMediaSrcPrivate));
}

static GstFlowReturn webkitMediaSrcChain(GstPad* pad, GstObject* parent, GstBuffer* buffer)
{
    GRefPtr<WebKitMediaSrc> self = adoptGRef(WEBKIT_MEDIA_SRC(gst_object_get_parent(parent)));
    GstFlowReturn ret = gst_proxy_pad_chain_default(pad, GST_OBJECT(self.get()), buffer);
    if (ret != GST_FLOW_FLUSHING)
        return gst_flow_combiner_update_pad_flow(self->priv->flowCombiner.get(), pad, ret);
    return ret;
}

static void webkit_media_src_init(WebKitMediaSrc* source)
{
    source->priv = WEBKIT_MEDIA_SRC_GET_PRIVATE(source);
    new (source->priv) WebKitMediaSrcPrivate();
    source->priv->seekTime = MediaTime::invalidTime();
    source->priv->appsrcSeekDataCount = 0;
    source->priv->appsrcNeedDataCount = 0;
    source->priv->appsrcSeekDataNextAction = Nothing;
    source->priv->flowCombiner = GUniquePtr<GstFlowCombiner>(gst_flow_combiner_new());
    source->priv->notifier = WebCore::MainThreadNotifier<WebKitMediaSrcMainThreadNotification>::create();

    // No need to reset Stream.appsrcNeedDataFlag because there are no Streams at this point yet.
}

void webKitMediaSrcFinalize(GObject* object)
{
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);
    WebKitMediaSrcPrivate* priv = source->priv;

    GST_DEBUG_OBJECT(source, "Finalizing %p", source);
    Vector<Stream*> oldStreams;
    source->priv->streams.swap(oldStreams);

    for (Stream* stream : oldStreams)
        webKitMediaSrcFreeStream(source, stream);

    priv->seekTime = MediaTime::invalidTime();

    source->priv->notifier->invalidate();

    if (priv->mediaPlayerPrivate)
        webKitMediaSrcSetMediaPlayerPrivate(source, nullptr);

    // We used a placement new for construction, the destructor won't be called automatically.
    priv->~_WebKitMediaSrcPrivate();

    GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

void webKitMediaSrcSetProperty(GObject* object, guint propId, const GValue* value, GParamSpec* pspec)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);

    switch (propId) {
    case PROP_LOCATION:
        gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(source), g_value_get_string(value), nullptr);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
        break;
    }
}

void webKitMediaSrcGetProperty(GObject* object, guint propId, GValue* value, GParamSpec* pspec)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);
    WebKitMediaSrcPrivate* priv = source->priv;

    GST_OBJECT_LOCK(source);
    switch (propId) {
    case PROP_LOCATION:
        g_value_set_string(value, priv->location.get());
        break;
    case PROP_N_AUDIO:
        g_value_set_int(value, priv->numberOfAudioStreams);
        break;
    case PROP_N_VIDEO:
        g_value_set_int(value, priv->numberOfVideoStreams);
        break;
    case PROP_N_TEXT:
        g_value_set_int(value, priv->numberOfTextStreams);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(source);
}

void webKitMediaSrcDoAsyncStart(WebKitMediaSrc* source)
{
    source->priv->asyncStart = true;
    GST_BIN_CLASS(parent_class)->handle_message(GST_BIN(source),
        gst_message_new_async_start(GST_OBJECT(source)));
}

void webKitMediaSrcDoAsyncDone(WebKitMediaSrc* source)
{
    WebKitMediaSrcPrivate* priv = source->priv;
    if (priv->asyncStart) {
        GST_BIN_CLASS(parent_class)->handle_message(GST_BIN(source),
            gst_message_new_async_done(GST_OBJECT(source), GST_CLOCK_TIME_NONE));
        priv->asyncStart = false;
    }
}

GstStateChangeReturn webKitMediaSrcChangeState(GstElement* element, GstStateChange transition)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(element);
    WebKitMediaSrcPrivate* priv = source->priv;

    GST_DEBUG_OBJECT(element, "%s", gst_state_change_get_name(transition));
    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        priv->allTracksConfigured = false;
        webKitMediaSrcDoAsyncStart(source);
        break;
    default:
        break;
    }

    GstStateChangeReturn result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (G_UNLIKELY(result == GST_STATE_CHANGE_FAILURE)) {
        GST_WARNING_OBJECT(source, "State change failed");
        webKitMediaSrcDoAsyncDone(source);
        return result;
    }

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        result = GST_STATE_CHANGE_ASYNC;
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        webKitMediaSrcDoAsyncDone(source);
        priv->allTracksConfigured = false;
        break;
    default:
        break;
    }

    return result;
}

gint64 webKitMediaSrcGetSize(WebKitMediaSrc* webKitMediaSrc)
{
    gint64 duration = 0;
    for (Stream* stream : webKitMediaSrc->priv->streams)
        duration = std::max<gint64>(duration, gst_app_src_get_size(GST_APP_SRC(stream->appsrc)));
    return duration;
}

gboolean webKitMediaSrcQueryWithParent(GstPad* pad, GstObject* parent, GstQuery* query)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(GST_ELEMENT(parent));
    gboolean result = FALSE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DURATION: {
        GstFormat format;
        gst_query_parse_duration(query, &format, nullptr);

        GST_DEBUG_OBJECT(source, "duration query in format %s", gst_format_get_name(format));
        GST_OBJECT_LOCK(source);
        switch (format) {
        case GST_FORMAT_TIME: {
            if (source->priv && source->priv->mediaPlayerPrivate) {
                MediaTime duration = source->priv->mediaPlayerPrivate->durationMediaTime();
                if (duration > MediaTime::zeroTime()) {
                    gst_query_set_duration(query, format, WebCore::toGstClockTime(duration));
                    GST_DEBUG_OBJECT(source, "Answering: duration=%" GST_TIME_FORMAT, GST_TIME_ARGS(WebCore::toGstClockTime(duration)));
                    result = TRUE;
                }
            }
            break;
        }
        case GST_FORMAT_BYTES: {
            if (source->priv) {
                gint64 duration = webKitMediaSrcGetSize(source);
                if (duration) {
                    gst_query_set_duration(query, format, duration);
                    GST_DEBUG_OBJECT(source, "size: %" G_GINT64_FORMAT, duration);
                    result = TRUE;
                }
            }
            break;
        }
        default:
            break;
        }

        GST_OBJECT_UNLOCK(source);
        break;
    }
    case GST_QUERY_URI:
        if (source) {
            GST_OBJECT_LOCK(source);
            if (source->priv)
                gst_query_set_uri(query, source->priv->location.get());
            GST_OBJECT_UNLOCK(source);
        }
        result = TRUE;
        break;
    default: {
        GRefPtr<GstPad> target = adoptGRef(gst_ghost_pad_get_target(GST_GHOST_PAD_CAST(pad)));
        // Forward the query to the proxy target pad.
        if (target)
            result = gst_pad_query(target.get(), query);
        break;
    }
    }

    return result;
}

void webKitMediaSrcUpdatePresentationSize(GstCaps* caps, Stream* stream)
{
    GST_OBJECT_LOCK(stream->parent);
    if (WebCore::doCapsHaveType(caps, GST_VIDEO_CAPS_TYPE_PREFIX)) {
        Optional<WebCore::FloatSize> size = WebCore::getVideoResolutionFromCaps(caps);
        if (size.hasValue())
            stream->presentationSize = size.value();
        else
            stream->presentationSize = WebCore::FloatSize();
    } else
        stream->presentationSize = WebCore::FloatSize();

    gst_caps_ref(caps);
    stream->caps = adoptGRef(caps);
    GST_OBJECT_UNLOCK(stream->parent);
}

#if ENABLE(ENCRYPTED_MEDIA)
GstElement* createDecryptor(const char* requestedProtectionSystemUuid)
{
    GstElement* decryptor = nullptr;
    GList* decryptors = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECRYPTOR, GST_RANK_MARGINAL);

    // Prefer WebKit decryptors
    decryptors = g_list_sort(decryptors, [](gconstpointer p1, gconstpointer p2) -> gint {
        GstPluginFeature *f1, *f2;
        const gchar* name;
        f1 = (GstPluginFeature *) p1;
        f2 = (GstPluginFeature *) p2;
        if ((name = gst_plugin_feature_get_name(f1)) && g_str_has_prefix(name, "webkit"))
            return -1;
        if ((name = gst_plugin_feature_get_name(f2)) && g_str_has_prefix(name, "webkit"))
            return 1;
        return gst_plugin_feature_rank_compare_func(p1, p2);
    });

    for (GList* walk = decryptors; !decryptor && walk; walk = g_list_next(walk)) {
        GstElementFactory* factory = reinterpret_cast<GstElementFactory*>(walk->data);

        for (const GList* current = gst_element_factory_get_static_pad_templates(factory); current && !decryptor; current = g_list_next(current)) {
            GstStaticPadTemplate* staticPadTemplate = static_cast<GstStaticPadTemplate*>(current->data);
            GRefPtr<GstCaps> caps = adoptGRef(gst_static_pad_template_get_caps(staticPadTemplate));
            unsigned length = gst_caps_get_size(caps.get());

            GST_TRACE("factory %s caps has size %u", GST_OBJECT_NAME(factory), length);
            for (unsigned i = 0; !decryptor && i < length; ++i) {
                GstStructure* structure = gst_caps_get_structure(caps.get(), i);
                GST_TRACE("checking structure %s", gst_structure_get_name(structure));
                if (gst_structure_has_field_typed(structure, GST_PROTECTION_SYSTEM_ID_CAPS_FIELD, G_TYPE_STRING)) {
                    const char* protectionSystemUuid = gst_structure_get_string(structure, GST_PROTECTION_SYSTEM_ID_CAPS_FIELD);
                    GST_TRACE("structure %s has protection system %s", gst_structure_get_name(structure), protectionSystemUuid);
                    if (!g_ascii_strcasecmp(requestedProtectionSystemUuid, protectionSystemUuid)) {
                        GST_DEBUG("found decryptor %s for %s", GST_OBJECT_NAME(factory), requestedProtectionSystemUuid);
                        decryptor = gst_element_factory_create(factory, nullptr);
                        break;
                    }
                }
            }
        }
    }
    gst_plugin_feature_list_free(decryptors);
    GST_TRACE("returning decryptor %p", decryptor);
    return decryptor;
}

typedef struct _DecryptorProbeData DecryptorProbeData;
struct _DecryptorProbeData
{
    _DecryptorProbeData(WebKitMediaSrc* parent)
        : parent(parent) {
    }
    ~_DecryptorProbeData() {
        GST_WARNING("Destroying Decryptor probe, decryptor=%p(attached: %s), payloader=%p(attached: %s)",
                    decryptor.get(), decryptorAttached ? "yes" : "no",
                    payloader.get(), payloaderAttached ? "yes" : "no");
    }
    WebKitMediaSrc* parent;
    GRefPtr<GstElement> decryptor;
    GRefPtr<GstElement> payloader;
    bool decryptorAttached { false };
    bool didTryCreatePayloader { false };
    bool payloaderAttached { false };
    bool didFail { false };
    WTF_MAKE_NONCOPYABLE(_DecryptorProbeData);
};

GstPadProbeReturn onAppSrcPadEvent(GstPad* pad, GstPadProbeInfo* info, gpointer data)
{
    if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM))
        return GST_PAD_PROBE_OK;

    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    if (GST_EVENT_TYPE (event) != GST_EVENT_CAPS)
        return GST_PAD_PROBE_OK;

    DecryptorProbeData* probData = reinterpret_cast<DecryptorProbeData*>(data);
    if (probData->didFail)
        return GST_PAD_PROBE_OK;

    GstCaps* caps = nullptr;
    gst_event_parse_caps(event, &caps);

    if (caps != nullptr)
    {
        unsigned padId = static_cast<unsigned>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(pad), "padId")));
        GUniquePtr<gchar> padName(g_strdup_printf("src_%u", padId));

        GstElement* decryptor = probData->decryptor.get();
        bool decryptorAttached = decryptor && probData->decryptorAttached;
        GstElement* payloader = probData->payloader.get();
        bool payloaderAttached = payloader && probData->payloaderAttached;

        if (probData->didTryCreatePayloader == false)
        {
            probData->didTryCreatePayloader = true;
            if (WebCore::doCapsHaveType(caps, GST_VIDEO_CAPS_TYPE_PREFIX)) {
                probData->payloader = gst_element_factory_make("svppay", nullptr);
                payloader = probData->payloader.get();
                if (payloader)
                    gst_bin_add(GST_BIN(probData->parent), payloader);
            }
        }

        if(!decryptorAttached && WebCore::areEncryptedCaps(caps))
        {
            if(!decryptor)
            {
                GstStructure* structure = gst_caps_get_structure(caps, 0);
                probData->decryptor = createDecryptor(gst_structure_get_string(structure, "protection-system"));
                decryptor = probData->decryptor.get();
                if (!decryptor)
                {
                    GST_ERROR("Failed to create decryptor");
                    probData->didFail = true;
                    return GST_PAD_PROBE_OK;
                }

                gst_bin_add(GST_BIN(probData->parent), decryptor);
            }

            GST_DEBUG("padname: %s Got CAPS=%" GST_PTR_FORMAT ", Add decryptor %" GST_PTR_FORMAT, padName.get(), caps, decryptor);

            gst_element_sync_state_with_parent(decryptor);

            GRefPtr<GstPad> decryptorSinkPad = adoptGRef(gst_element_get_static_pad(decryptor, "sink"));
            GRefPtr<GstPad> decryptorSrcPad = adoptGRef(gst_element_get_static_pad(decryptor, "src"));
            GstPad *srcPad = pad;
            GstPadLinkReturn rc;

            GRefPtr<GstPad> peerPad = adoptGRef(gst_pad_get_peer(srcPad));

            if(payloader && !payloaderAttached){
                GRefPtr<GstPad> payloaderSinkPad = adoptGRef(gst_element_get_static_pad(payloader, "sink"));
                GRefPtr<GstPad> payloaderSrcPad = adoptGRef(gst_element_get_static_pad(payloader, "src"));

                // Insert decryptor and payloader between appsrc and the decodebin
                gst_element_sync_state_with_parent(payloader);

                if (!gst_pad_unlink(srcPad, peerPad.get()))
                    GST_ERROR("Failed to unlink '%s' src pad", padName.get());
                else if (GST_PAD_LINK_OK != (rc = gst_pad_link_full(srcPad, decryptorSinkPad.get(), GST_PAD_LINK_CHECK_NOTHING)))
                    GST_ERROR("Failed to link pad to decryptorSinkPad, rc = %d", rc);
                else if (GST_PAD_LINK_OK != (rc = gst_pad_link(decryptorSrcPad.get(), payloaderSinkPad.get())))
                    GST_ERROR("Failed to link decryptorSrcPad to payloader sinkpad, rc = %d", rc);
                else if (GST_PAD_LINK_OK != (rc = gst_pad_link(payloaderSrcPad.get(), peerPad.get())))
                    GST_ERROR("Failed to link payloaderSrcPad to app sink, rc = %d", rc);

                probData->payloaderAttached = true;
            } else {
                // Insert decryptor between appsrc and the decodebin or the payloader
                if (!gst_pad_unlink(srcPad, peerPad.get()))
                    GST_ERROR("Failed to unlink '%s' src pad", padName.get());
                else if (GST_PAD_LINK_OK != (rc = gst_pad_link_full(srcPad, decryptorSinkPad.get(), GST_PAD_LINK_CHECK_NOTHING)))
                    GST_ERROR("Failed to link pad to decryptorSinkPad, rc = %d", rc);
                else if (GST_PAD_LINK_OK != (rc = gst_pad_link(decryptorSrcPad.get(), peerPad.get())))
                    GST_ERROR("Failed to link decryptorSrcPad to app sink, rc = %d", rc);
            }

            probData->decryptorAttached = true;
        }
        else if (decryptorAttached && !WebCore::areEncryptedCaps(caps))
        {
            GST_DEBUG("padname: %s Got CAPS=%" GST_PTR_FORMAT ", Remove decryptor %" GST_PTR_FORMAT, padName.get(), caps, decryptor);

            GRefPtr<GstPad> decryptorSinkPad = adoptGRef(gst_element_get_static_pad(decryptor, "sink"));
            GRefPtr<GstPad> decryptorSrcPad = adoptGRef(gst_element_get_static_pad(decryptor, "src"));
            GRefPtr<GstPad> peerPad = adoptGRef(gst_pad_get_peer(decryptorSrcPad.get()));
            GstPad *srcPad = pad;
            GstPadLinkReturn rc;

            if (!gst_pad_unlink(decryptorSrcPad.get(), peerPad.get()))
                GST_ERROR("Failed to unlink decryptorSrcPad");
            else if (!gst_pad_unlink(srcPad, decryptorSinkPad.get()))
                GST_ERROR("Failed to unlink decryptorSinkPad");
            else if (GST_PAD_LINK_OK != (rc = gst_pad_link(srcPad, peerPad.get())))
                GST_ERROR("Failed to link '%s' to peer pad, rc = %d", padName.get(), rc);

            probData->decryptorAttached = false;
        }
        else if (payloader && !payloaderAttached && !WebCore::areEncryptedCaps(caps))
        {
            GST_DEBUG("padname: %s Got CAPS=%" GST_PTR_FORMAT ", Attach payloader %" GST_PTR_FORMAT, padName.get(), caps, payloader);

            gst_element_sync_state_with_parent(payloader);

            GRefPtr<GstPad> payloaderSinkPad = adoptGRef(gst_element_get_static_pad(payloader, "sink"));
            GRefPtr<GstPad> payloaderSrcPad = adoptGRef(gst_element_get_static_pad(payloader, "src"));
            GstPad *srcPad = pad;
            GRefPtr<GstPad> peerPad = adoptGRef(gst_pad_get_peer(srcPad));
            GstPadLinkReturn rc;

            if (!gst_pad_unlink(srcPad, peerPad.get()))
                GST_ERROR("Failed to unlink '%s' src pad", padName.get());
            else if (GST_PAD_LINK_OK != (rc = gst_pad_link(srcPad, payloaderSinkPad.get())))
                GST_ERROR("Failed to link pad to payloaderSinkPad, rc = %d", rc);
            else if (GST_PAD_LINK_OK != (rc = gst_pad_link(payloaderSrcPad.get(), peerPad.get())))
                GST_ERROR("Failed to link payloaderSrcPad to app sink, rc = %d", rc);

            probData->payloaderAttached = true;
        }
        else
        {
            GST_DEBUG("padname: %s Got CAPS %" GST_PTR_FORMAT ", decryptorAttached = %s, payloaderAttached = %s, caps are encrypted= %s",
                    padName.get(), caps, decryptorAttached ? "yes" : "no", payloaderAttached ? "yes" : "no",
                    WebCore::areEncryptedCaps(caps) ? "yes" : "no");
        }
    }

    return GST_PAD_PROBE_OK;
}
#endif

#if ENABLE(INSTANT_RATE_CHANGE)
GstPadProbeReturn handleInstantRateChangeSeekProbe(GstPad* pad, GstPadProbeInfo* info, gpointer data)
{
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    GstSegment *segment = reinterpret_cast<GstSegment*>(data);
    switch ( GST_EVENT_TYPE(event) ) {
        case GST_EVENT_SEEK:
            // handled below
            break;
        case GST_EVENT_SEGMENT:
            gst_event_copy_segment(event, segment);
            FALLTHROUGH;
        default:
          return GST_PAD_PROBE_OK;
    };
    gdouble rate = 1.0;
    GstSeekFlags flags = GST_SEEK_FLAG_NONE;
    gst_event_parse_seek (event, &rate, nullptr, &flags, nullptr, nullptr, nullptr, nullptr);
    if ( !!(flags & GST_SEEK_FLAG_INSTANT_RATE_CHANGE) ) {
        gdouble rateMultiplier = rate / segment->rate;
        GstEvent *rateChangeEvent =
            gst_event_new_instant_rate_change(rateMultiplier, static_cast<GstSegmentFlags>(flags));
        gst_event_set_seqnum (rateChangeEvent, gst_event_get_seqnum (event));
        GstPad *peerPad = gst_pad_get_peer(pad);
        GST_DEBUG("Sending to pad: %" GST_PTR_FORMAT ", event: %" GST_PTR_FORMAT, peerPad, rateChangeEvent);
        if ( gst_pad_send_event (peerPad, rateChangeEvent) != TRUE )
            GST_PAD_PROBE_INFO_FLOW_RETURN(info) = GST_FLOW_NOT_SUPPORTED;
        gst_object_unref(peerPad);
        gst_event_unref(event);
        return GST_PAD_PROBE_HANDLED;
    }
    return GST_PAD_PROBE_OK;
}
#endif

void webKitMediaSrcLinkStreamToSrcPad(GstPad* sourcePad, Stream* stream)
{
    unsigned padId = static_cast<unsigned>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sourcePad), "padId")));
    GST_DEBUG_OBJECT(stream->parent, "linking stream to src pad (id: %u)", padId);

    GUniquePtr<gchar> padName(g_strdup_printf("src_%u", padId));
    GstPad* ghostpad = WebCore::webkitGstGhostPadFromStaticTemplate(&srcTemplate, padName.get(), sourcePad);

    auto proxypad = adoptGRef(GST_PAD(gst_proxy_pad_get_internal(GST_PROXY_PAD(ghostpad))));
    gst_flow_combiner_add_pad(stream->parent->priv->flowCombiner.get(), proxypad.get());
    gst_pad_set_chain_function(proxypad.get(), static_cast<GstPadChainFunction>(webkitMediaSrcChain));
    gst_pad_set_query_function(ghostpad, webKitMediaSrcQueryWithParent);

    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(stream->parent), ghostpad);

#if ENABLE(ENCRYPTED_MEDIA)
    if (!stream->decryptorProbeId) {
        stream->decryptorProbeId =
            gst_pad_add_probe(sourcePad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                    onAppSrcPadEvent, new DecryptorProbeData(stream->parent),
                    [](gpointer data) { delete static_cast<DecryptorProbeData*>(data);});
    }
#endif

#if ENABLE(INSTANT_RATE_CHANGE)
    gst_pad_add_probe (
        ghostpad,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
        handleInstantRateChangeSeekProbe,
        gst_segment_new(),
        reinterpret_cast<GDestroyNotify>(gst_segment_free));
#endif
}

void webKitMediaSrcLinkSourcePad(GstPad* sourcePad, GstCaps* caps, Stream* stream)
{
    ASSERT(caps && stream->parent);
    if (!caps || !stream->parent) {
        GST_ERROR("Unable to link parser");
        return;
    }

    webKitMediaSrcUpdatePresentationSize(caps, stream);

    // FIXME: drop webKitMediaSrcLinkStreamToSrcPad() and move its code here.
    if (!gst_pad_is_linked(sourcePad)) {
        GST_DEBUG_OBJECT(stream->parent, "pad not linked yet");
        webKitMediaSrcLinkStreamToSrcPad(sourcePad, stream);
    }

    webKitMediaSrcCheckAllTracksConfigured(stream->parent);
}

void webKitMediaSrcFreeStream(WebKitMediaSrc* source, Stream* stream)
{
    GST_DEBUG_OBJECT(source, "Releasing stream: %p", stream);

#if ENABLE(ENCRYPTED_MEDIA)
    if (stream->appsrc && stream->decryptorProbeId) {
        GRefPtr<GstPad> appsrcPad = adoptGRef(gst_element_get_static_pad(stream->appsrc, "src"));
        gst_pad_remove_probe(appsrcPad.get(), stream->decryptorProbeId);
        stream->decryptorProbeId = 0;
    }
#endif

    if (GST_IS_APP_SRC(stream->appsrc)) {
        // Don't trigger callbacks from this appsrc to avoid using the stream anymore.
        gst_app_src_set_callbacks(GST_APP_SRC(stream->appsrc), &disabledAppsrcCallbacks, nullptr, nullptr);
        gst_app_src_end_of_stream(GST_APP_SRC(stream->appsrc));
        gst_object_unref(stream->appsrc);
    }

    GST_OBJECT_LOCK(source);
    switch (stream->type) {
    case WebCore::Audio:
        source->priv->numberOfAudioStreams--;
        break;
    case WebCore::Video:
        source->priv->numberOfVideoStreams--;
        break;
    case WebCore::Text:
        source->priv->numberOfTextStreams--;
        break;
    default:
        break;
    }
    GST_OBJECT_UNLOCK(source);

    if (stream->type != WebCore::Invalid) {
        GST_DEBUG("Freeing track-related info on stream %p", stream);

        LockHolder locker(source->priv->streamLock);

        if (stream->caps)
            stream->caps = nullptr;

        if (stream->audioTrack)
            stream->audioTrack = nullptr;
        if (stream->videoTrack)
            stream->videoTrack = nullptr;

        int signal = -1;
        switch (stream->type) {
        case WebCore::Audio:
            signal = SIGNAL_AUDIO_CHANGED;
            break;
        case WebCore::Video:
            signal = SIGNAL_VIDEO_CHANGED;
            break;
        case WebCore::Text:
            signal = SIGNAL_TEXT_CHANGED;
            break;
        default:
            break;
        }
        stream->type = WebCore::Invalid;

        if (signal != -1)
            g_signal_emit(G_OBJECT(source), webKitMediaSrcSignals[signal], 0, nullptr);

        source->priv->streamCondition.notifyOne();
    }

    delete stream;
}

void webKitMediaSrcRestoreTracks(WebKitMediaSrc* oldSource, WebKitMediaSrc* newSource)
{
    newSource->priv->streams = WTFMove(oldSource->priv->streams);
    for (auto* stream : newSource->priv->streams) {
        stream->parent = newSource;

        auto oldSourcePad = adoptGRef(gst_element_get_static_pad(stream->appsrc, "src"));
        unsigned padId = static_cast<unsigned>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(oldSourcePad.get()), "padId")));

        gst_object_unref(stream->appsrc);

        // Ensure ownership is not transfered to the bin. The appsrc element is managed by its parent Stream.
        stream->appsrc = GST_ELEMENT_CAST(gst_object_ref_sink(gst_element_factory_make("appsrc", nullptr)));
        stream->appsrcNeedDataFlag = true;

        gst_app_src_set_callbacks(GST_APP_SRC(stream->appsrc), &enabledAppsrcCallbacks, stream->parent, nullptr);
        gst_app_src_set_emit_signals(GST_APP_SRC(stream->appsrc), FALSE);
        gst_app_src_set_stream_type(GST_APP_SRC(stream->appsrc), GST_APP_STREAM_TYPE_SEEKABLE);

        gst_app_src_set_max_bytes(GST_APP_SRC(stream->appsrc), 2 * WTF::MB);
        g_object_set(G_OBJECT(stream->appsrc), "block", FALSE, "min-percent", 20, "format", GST_FORMAT_TIME, nullptr);

        gst_bin_add(GST_BIN_CAST(newSource), stream->appsrc);

        auto sourcePad = adoptGRef(gst_element_get_static_pad(stream->appsrc, "src"));
        g_object_set_data(G_OBJECT(sourcePad.get()), "padId", GINT_TO_POINTER(padId));
    }

    newSource->priv->numberOfAudioStreams = oldSource->priv->numberOfAudioStreams;
    newSource->priv->numberOfTextStreams = oldSource->priv->numberOfTextStreams;
    newSource->priv->numberOfVideoStreams = oldSource->priv->numberOfVideoStreams;
    newSource->priv->numberOfPads = oldSource->priv->numberOfPads;
    newSource->priv->allTracksConfigured = oldSource->priv->allTracksConfigured;
}

void webKitMediaSrcSignalTracks(WebKitMediaSrc* source)
{
    for (auto* stream : source->priv->streams) {
        auto sourcePad = adoptGRef(gst_element_get_static_pad(stream->appsrc, "src"));
        webKitMediaSrcLinkStreamToSrcPad(sourcePad.get(), stream);

        gst_element_sync_state_with_parent(stream->appsrc);

        int signal = -1;
        switch (stream->type) {
        case WebCore::MediaSourceStreamTypeGStreamer::Video:
            signal = SIGNAL_VIDEO_CHANGED;
            break;
        case WebCore::MediaSourceStreamTypeGStreamer::Audio:
            signal = SIGNAL_AUDIO_CHANGED;
            break;
        case WebCore::MediaSourceStreamTypeGStreamer::Text:
            signal = SIGNAL_TEXT_CHANGED;
            break;
        default:
            break;
        }
        if (signal != -1)
            g_signal_emit(G_OBJECT(stream->parent), webKitMediaSrcSignals[signal], 0, nullptr);
    }

    if (source->priv->streams.size()) {
        gst_element_no_more_pads(GST_ELEMENT_CAST(source));
        webKitMediaSrcDoAsyncDone(source);
    }
}

void webKitMediaSrcCheckAllTracksConfigured(WebKitMediaSrc* webKitMediaSrc)
{
    bool allTracksConfigured = false;

    GST_OBJECT_LOCK(webKitMediaSrc);
    if (!webKitMediaSrc->priv->allTracksConfigured) {
        allTracksConfigured = true;
        for (Stream* stream : webKitMediaSrc->priv->streams) {
            if (stream->type == WebCore::Invalid) {
                allTracksConfigured = false;
                break;
            }
        }
        if (allTracksConfigured)
            webKitMediaSrc->priv->allTracksConfigured = true;
    }
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    if (allTracksConfigured) {
        GST_DEBUG("All tracks attached. Completing async state change operation.");
        gst_element_no_more_pads(GST_ELEMENT(webKitMediaSrc));
        webKitMediaSrcDoAsyncDone(webKitMediaSrc);
    }
}

// Uri handler interface.
GstURIType webKitMediaSrcUriGetType(GType)
{
    return GST_URI_SRC;
}

const gchar* const* webKitMediaSrcGetProtocols(GType)
{
    static const char* protocols[] = {"mediasourceblob", nullptr };
    return protocols;
}

gchar* webKitMediaSrcGetUri(GstURIHandler* handler)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(handler);
    gchar* result;

    GST_OBJECT_LOCK(source);
    result = g_strdup(source->priv->location.get());
    GST_OBJECT_UNLOCK(source);
    return result;
}

gboolean webKitMediaSrcSetUri(GstURIHandler* handler, const gchar* uri, GError**)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(handler);

    if (GST_STATE(source) >= GST_STATE_PAUSED) {
        GST_ERROR_OBJECT(source, "URI can only be set in states < PAUSED");
        return FALSE;
    }

    GST_OBJECT_LOCK(source);
    WebKitMediaSrcPrivate* priv = source->priv;
    priv->location = nullptr;
    if (!uri) {
        GST_OBJECT_UNLOCK(source);
        return TRUE;
    }

    URL url(URL(), uri);

    priv->location = GUniquePtr<gchar>(g_strdup(url.string().utf8().data()));
    GST_OBJECT_UNLOCK(source);
    return TRUE;
}

void webKitMediaSrcUriHandlerInit(gpointer gIface, gpointer)
{
    GstURIHandlerInterface* iface = (GstURIHandlerInterface *) gIface;

    iface->get_type = webKitMediaSrcUriGetType;
    iface->get_protocols = webKitMediaSrcGetProtocols;
    iface->get_uri = webKitMediaSrcGetUri;
    iface->set_uri = webKitMediaSrcSetUri;
}

static void seekNeedsDataMainThread(WebKitMediaSrc* source)
{
    GST_DEBUG("Buffering needed before seek");

    ASSERT(WTF::isMainThread());

    GST_OBJECT_LOCK(source);
    MediaTime seekTime = source->priv->seekTime;
    WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate = source->priv->mediaPlayerPrivate;

    if (!mediaPlayerPrivate) {
        GST_OBJECT_UNLOCK(source);
        return;
    }

    for (Stream* stream : source->priv->streams) {
        if (stream->type != WebCore::Invalid)
            stream->sourceBuffer->setReadyForMoreSamples(true);
    }
    GST_OBJECT_UNLOCK(source);
    mediaPlayerPrivate->notifySeekNeedsDataForTime(seekTime);
}

static void notifyReadyForMoreSamplesMainThread(WebKitMediaSrc* source, Stream* appsrcStream)
{
    GST_OBJECT_LOCK(source);

    auto it = std::find(source->priv->streams.begin(), source->priv->streams.end(), appsrcStream);
    if (it == source->priv->streams.end()) {
        GST_OBJECT_UNLOCK(source);
        return;
    }

    WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate = source->priv->mediaPlayerPrivate;
    bool shouldNotify = mediaPlayerPrivate && mediaPlayerPrivate->gstSeekCompleted();
    GST_OBJECT_UNLOCK(source);

    if (shouldNotify)
        appsrcStream->sourceBuffer->notifyReadyForMoreSamples();
}

void webKitMediaSrcSetMediaPlayerPrivate(WebKitMediaSrc* source, WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate)
{
    GST_OBJECT_LOCK(source);

    // Set to nullptr on MediaPlayerPrivateGStreamer destruction, never a dangling pointer.
    source->priv->mediaPlayerPrivate = mediaPlayerPrivate;
    GST_OBJECT_UNLOCK(source);
}

void webKitMediaSrcSetReadyForSamples(WebKitMediaSrc* source, bool isReady)
{
    if (source) {
        GST_OBJECT_LOCK(source);
        for (Stream* stream : source->priv->streams)
            stream->sourceBuffer->setReadyForMoreSamples(isReady);
        GST_OBJECT_UNLOCK(source);
    }
}

void webKitMediaSrcPrepareSeek(WebKitMediaSrc* source, const MediaTime& time)
{
    GST_OBJECT_LOCK(source);
    source->priv->seekTime = time;
    source->priv->appsrcSeekDataCount = 0;
    source->priv->appsrcNeedDataCount = 0;

    for (Stream* stream : source->priv->streams) {
        stream->appsrcNeedDataFlag = false;
        stream->lastEnqueuedTime = MediaTime::invalidTime();
    }

    // The pending action will be performed in enabledAppsrcSeekData().
    source->priv->appsrcSeekDataNextAction = MediaSourceSeekToTime;
    GST_OBJECT_UNLOCK(source);
}

GstPadProbeReturn initialSeekSegmentFixerProbe(GstPad *pad, GstPadProbeInfo *info, gpointer userData)
{
    GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
        const GstSegment* originalSegment = nullptr;
        const GstSegment* fixedSegment = static_cast<GstSegment*>(userData);
        gst_event_parse_segment(event, &originalSegment);
        GST_DEBUG("Segment at %s: %" GST_SEGMENT_FORMAT ", replaced by %" GST_SEGMENT_FORMAT, GST_ELEMENT_NAME(GST_PAD_PARENT(pad)), originalSegment, fixedSegment);
        gst_event_replace(reinterpret_cast<GstEvent**>(&info->data), gst_event_new_segment(fixedSegment));
        return GST_PAD_PROBE_REMOVE;
    }
    return GST_PAD_PROBE_OK;
}

void webKitMediaSrcPrepareInitialSeek(WebKitMediaSrc* source, double rate, const MediaTime& startTime, const MediaTime& endTime)
{
    GST_OBJECT_LOCK(source);
    MediaTime seekTime = (rate >= 0) ? startTime : endTime;
    source->priv->seekTime = seekTime;
    source->priv->appsrcSeekDataCount = 0;
    source->priv->appsrcNeedDataCount = 0;

    for (Stream* stream : source->priv->streams)
        stream->appsrcNeedDataFlag = false;

    GUniquePtr<GstSegment> segment(gst_segment_new());
    segment->format = GST_FORMAT_TIME;
    gst_segment_do_seek(segment.get(), rate, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, WebCore::toGstUnsigned64Time(startTime),
        GST_SEEK_TYPE_SET, WebCore::toGstUnsigned64Time(endTime), nullptr);

    for (Stream* stream : source->priv->streams) {
        // This probe will fix the segment autogenerated by appsrc.
        GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(stream->appsrc, "src"));
        gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
            initialSeekSegmentFixerProbe, gst_segment_copy(segment.get()), reinterpret_cast<GDestroyNotify>(gst_segment_free));
        stream->sourceBuffer->setReadyForMoreSamples(true);
    }

    GST_OBJECT_UNLOCK(source);
}

namespace WTF {
template <> GRefPtr<WebKitMediaSrc> adoptGRef(WebKitMediaSrc* ptr)
{
    ASSERT(!ptr || !g_object_is_floating(G_OBJECT(ptr)));
    return GRefPtr<WebKitMediaSrc>(ptr, GRefPtrAdopt);
}

template <> WebKitMediaSrc* refGPtr<WebKitMediaSrc>(WebKitMediaSrc* ptr)
{
    if (ptr)
        gst_object_ref_sink(GST_OBJECT(ptr));

    return ptr;
}

template <> void derefGPtr<WebKitMediaSrc>(WebKitMediaSrc* ptr)
{
    if (ptr)
        gst_object_unref(ptr);
}
};

#endif // USE(GSTREAMER)

