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

WI.GStreamerTabContentView = class GStreamerTabContentView extends WI.ContentBrowserTabContentView
{
    constructor(identifier)
    {
        let tabBarItem = WI.GeneralTabBarItem.fromTabInfo(WI.GStreamerTabContentView.tabInfo());

        super("gstreamer", "gstreamer", tabBarItem, WI.GStreamerSidebarPanel);
        this._gstreamerView = null;
    }

    // Static

    static tabInfo()
    {
        return {
            image: "Images/GStreamer.svg",
            title: WI.UIString("GStreamer"),
        };
    }

    static isTabAllowed()
    {
        return InspectorBackend.hasDomain("GStreamer") && (WI.Platform.port === "gtk" || WI.Platform.port === "wpe");
    }

    // Public

    get type() { return WI.GStreamerTabContentView.Type; }

    get supportsSplitContentBrowser()
    {
        return true;
    }

    canShowRepresentedObject(representedObject)
    {
        return representedObject instanceof WI.GStreamerPipeline;
    }

    showRepresentedObject(representedObject, cookie)
    {
        this._gstreamerView.displayPipeline(representedObject);
    }

    // Protected

    initialLayout()
    {
        super.initialLayout();
        this._gstreamerView = new WI.GStreamerView("general", WI.UIString("General"));
        this.addSubview(this._gstreamerView);
    }
};

WI.GStreamerTabContentView.Type = "gstreamer";