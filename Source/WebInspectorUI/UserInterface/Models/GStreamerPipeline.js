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

WI.GStreamerPipeline = class GStreamerPipeline
{
    constructor(name, parentBinName, binName, dotData, svgDocument)
    {
        this._name = name;
        this._parentBinName = parentBinName;
        this._binName = binName;
        this._dotData = dotData;
        this._svgDocument = svgDocument;
    }

    // Public

    get name() { return this._name; }
    get parentBinName() { return this._parentBinName; }
    get binName() { return this._binName; }
    get svgElement()
    {
        return this._svgDocument.getElementsByTagName("svg")[0];
    }

    get saveData()
    {
        let suggestedName = WI.unlocalizedString(`GStreamer-pipeline-dump-${this._name}.dot`);
        return {
            url: WI.FileUtilities.inspectorURLForFilename(suggestedName),
            content: this._dotData,
        };
    }
};

WI.GStreamerPipeline.TypeIdentifier = "gstreamer-pipeline";