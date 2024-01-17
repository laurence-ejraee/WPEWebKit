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

WI.GStreamerView = class GStreamerView extends WI.ContentView
{
    constructor(identifier, displayName)
    {
        super();

        this._identifier = identifier;
        this._displayName = displayName;
        this._svgElement = null;
        this._scopeBar = null;
        this._currentBinName = "";
        this._currentParentBinName = "";
        this._pipeline = null;
        this._label = null;
        this._autoUpdateIntervalIdentifier = undefined;
        this._panZoom = null;
        this._currentPanZoomParameters = undefined;

        this.element.classList.add("gstreamer-view", identifier);

        this._exportButtonNavigationItem = new WI.ButtonNavigationItem("pipeline-export", WI.UIString("Export"), "Images/Export.svg", 15, 15);
        this._exportButtonNavigationItem.tooltip = WI.UIString("Export pipeline representation to Graphviz dot file (%s)", "The Graphviz dot file format can then be converted to PNG and other picture formats with third-party tools.").format(WI.saveKeyboardShortcut.displayName);
        this._exportButtonNavigationItem.buttonStyle = WI.ButtonNavigationItem.Style.ImageAndText;
        this._exportButtonNavigationItem.visibilityPriority = WI.NavigationItem.VisibilityPriority.Low;
        this._exportButtonNavigationItem.addEventListener(WI.ButtonNavigationItem.Event.Clicked, this._handleExportButtonNavigationItemClicked, this);
        this._updateExportButtonNavigationItemState();

        this._navigationBar = new WI.NavigationBar;
        this._navigationBar.addNavigationItem(this._exportButtonNavigationItem);
        this._navigationBar.addNavigationItem(new WI.DividerNavigationItem);

        this._automaticRefreshNavigationItem = new WI.TextToggleButtonNavigationItem("automatic-pipeline-redisplay", WI.UIString("Automatically refresh the pipeline"));
        this._automaticRefreshNavigationItem.addEventListener(WI.ButtonNavigationItem.Event.Clicked, this._handleAutomaticRefreshButtonNavigationItemClicked, this);
        this._navigationBar.addNavigationItem(this._automaticRefreshNavigationItem);
        this._navigationBar.addNavigationItem(new WI.DividerNavigationItem);

        this.addSubview(this._navigationBar);

        this._containerElement = document.createElement("div");
        this._containerElement.id = "pipeline-container";
        this.element.append(this._containerElement);
    }

    // Public

    get identifier() { return this._identifier; }
    get displayName() { return this._displayName; }

    get supportsSave()
    {
        return !!this._pipeline;
    }

    get saveData()
    {
        return {customSaveHandler: () => { this._exportResult(); }};
    }

    displayPipeline(gstreamerPipeline)
    {
        if (this._scopeBar) {
            this._scopeBar.removeEventListener(WI.ScopeBar.Event.SelectionChanged, this._scopeBarSelectionDidChange, this);
            this._navigationBar.removeNavigationItem(this._scopeBar);
            this._scopeBar = null;
        }

        if (this._panZoom)
            this._panZoom.dispose();

        if (this._label)
            this._navigationBar.removeNavigationItem(this._label);

        this._pipeline = gstreamerPipeline;
        let name = gstreamerPipeline.binName || gstreamerPipeline.name;

        this._updateExportButtonNavigationItemState();
        this._updateAutomaticRefreshButtonNavigationItemState();

        if (this._autoUpdateIntervalIdentifier) {
            this._label = new WI.TextNavigationItem("gst-pipeline-bin-name", WI.UIString("Displaying %s.", "").format(name));
            this._navigationBar.addNavigationItem(this._label);
            this._refreshSVGElement();
            return;
        }

        this._label = new WI.TextNavigationItem("gst-pipeline-bin-name", WI.UIString("Displaying %s. Available children bins: ", "").format(name));
        this._navigationBar.addNavigationItem(this._label);

        let target = WI.assumingMainTarget();
        target.GStreamerAgent.getActivePipelineBinNames(gstreamerPipeline.name, gstreamerPipeline.binName).then((payload) => {
            let scopeBarItems = [];
            let defaultItem = new WI.ScopeBarItem("gst-pipeline-scope-bar-placeholder", "Select...");
            defaultItem.selected = true;
            scopeBarItems.push(defaultItem);

            let scopeBarItem = new WI.ScopeBarItem(gstreamerPipeline.parentBinName, "Parent: " + gstreamerPipeline.parentBinName);
            scopeBarItem.selected = false;
            scopeBarItems.push(scopeBarItem);

            for (let binName of payload.binNames) {
                let innerScopeBarItem = new WI.ScopeBarItem(binName, binName);
                innerScopeBarItem.selected = false;
                innerScopeBarItem.__parent = gstreamerPipeline.binName;
                scopeBarItems.push(innerScopeBarItem);
            }

            this._scopeBar = new WI.ScopeBar("gst-pipeline-scope-bar", scopeBarItems, null, true);
            this._scopeBar.addEventListener(WI.ScopeBar.Event.SelectionChanged, this._scopeBarSelectionDidChange, this);
            this._navigationBar.addNavigationItem(this._scopeBar);

            this._refreshSVGElement();
        }).catch((error) => {
            console.error("Could not dump list pipeline bins: ", error);
        });
    }

    static renderSVGElement(graphRepresentation)
    {
        let parser = new DOMParser();
        return parser.parseFromString(graphRepresentation, "image/svg+xml");
    }

    // Private

    _refreshSVGElement()
    {
        let previousElement = this._svgElement;
        this._svgElement = this._pipeline.svgElement;
        if (previousElement)
            this._containerElement.replaceChild(this._svgElement, previousElement);
        else
            this._containerElement.appendChild(this._svgElement);

        let graphElement = this._svgElement.getElementsByTagName("g")[0];
        this._panZoom = panzoom(graphElement, {
            beforeWheel: function(event) {
                // Allow wheel-zoom only if altKey is down. Otherwise - ignore
                let shouldIgnore = !event.altKey;
                return shouldIgnore;
            },
            autocenter: false,
            smoothScroll: false
        });

        this._panZoom.on("transform", this._updatePanZoomParameters.bind(this));

        if (this._panZoomParameters && (this._autoUpdateIntervalIdentifier !== undefined))
            this._panZoom.moveTo(this._panZoomParameters.x, this._panZoomParameters.y);
    }

    _updatePanZoomParameters(event)
    {
        this._panZoomParameters = this._panZoom.getTransform();
    }

    async _displayBin(binName, parentBinName)
    {
        let agent;
        let target = WI.assumingMainTarget();
        if (target.hasDomain("GStreamer"))
            agent = target.GStreamerAgent;
        else
            return;

        this._currentBinName = binName;
        this._currentParentBinName = parentBinName;
        let pipelineName = this._pipeline.name;
        agent.dumpActivePipeline(pipelineName, binName).then((payload) => {
            let documentFragment = GStreamerView.renderSVGElement(payload.pipelineGraphRepresentation);
            let representedObject = new WI.GStreamerPipeline(pipelineName, parentBinName, binName, payload.pipelineGraphRepresentation, documentFragment);
            WI.showRepresentedObject(representedObject);
        });
    }

    _scopeBarSelectionDidChange(event)
    {
        for (let item of event.target.items) {
            if (item.id !== "gst-pipeline-scope-bar-placeholder" && item.selected) {
                let binName = item.id;
                let parentBinName = item.__parent || "";
                this._displayBin(binName, parentBinName).catch((error) => {
                    console.error("Could not dump pipeline: ", error);
                });
                return;
            }
        }
    }

    _exportResult()
    {
        WI.FileUtilities.save(this._pipeline.saveData);
    }

    _updateExportButtonNavigationItemState()
    {
        this._exportButtonNavigationItem.enabled = !!this._pipeline;
    }

    _handleExportButtonNavigationItemClicked(event)
    {
        this._exportResult();
    }

    _updateAutomaticRefreshButtonNavigationItemState()
    {
        this._automaticRefreshNavigationItem.activated = this._autoUpdateIntervalIdentifier !== undefined;
    }

    _updateGraph()
    {
        this._displayBin(this._currentBinName, this._currentParentBinName).catch((error) => {
            window.clearInterval(this._autoUpdateIntervalIdentifier);
            this._autoUpdateIntervalIdentifier = undefined;
            this._updateAutomaticRefreshButtonNavigationItemState();
        });
    }

    _handleAutomaticRefreshButtonNavigationItemClicked(event)
    {
        const autoUpdateInterval = 1000;
        if (this._autoUpdateIntervalIdentifier === undefined)
            this._autoUpdateIntervalIdentifier = setInterval(this._updateGraph.bind(this), autoUpdateInterval);
        else {
            window.clearInterval(this._autoUpdateIntervalIdentifier);
            this._autoUpdateIntervalIdentifier = undefined;
        }

        this._updateAutomaticRefreshButtonNavigationItemState();
    }
};