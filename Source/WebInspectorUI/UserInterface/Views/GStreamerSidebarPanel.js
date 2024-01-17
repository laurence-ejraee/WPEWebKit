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

WI.GStreamerSidebarPanel = class GStreamerSidebarPanel extends WI.NavigationSidebarPanel
{
    constructor()
    {
        super("gstreamer", WI.UIString("GStreamer"));

        this.contentTreeOutline.addEventListener(WI.TreeOutline.Event.SelectionDidChange, this._treeSelectionDidChange, this);

        WI.gstreamerManager.addEventListener(WI.GStreamerManager.Event.ActivePipelinesChanged, this._refreshPipelinesList, this);
    }

    // Protected

    initialLayout()
    {
        super.initialLayout();

        let pipelinesRow = new WI.DetailsSectionRow;
        pipelinesRow.element.appendChild(this.contentTreeOutline.element);

        let pipelinesGroup = new WI.DetailsSectionGroup([pipelinesRow]);
        this._pipelinesSection = new WI.DetailsSection("pipelines", WI.UIString("Pipelines"), [pipelinesGroup]);
        this.contentView.element.appendChild(this._pipelinesSection.element);

        this._refreshPipelinesList();
    }

    hasCustomFilters()
    {
        return true;
    }

    matchTreeElementAgainstCustomFilters(treeElement, flags)
    {
        let textFilterRegex = simpleGlobStringToRegExp(this.filterBar.filters.text, "i");
        if (!textFilterRegex)
            return true;

        return textFilterRegex.test(treeElement.name);
    }

    // Private

    _refreshPipelinesList()
    {
        if (!WI.targetsAvailable()) {
            WI.whenTargetsAvailable().then(() => {
                this._refreshPipelinesList();
            });
            return;
        }

        let agent;
        let target = WI.assumingMainTarget();
        if (target.hasDomain("GStreamer"))
            agent = target.GStreamerAgent;
        else
            return;

        agent.getActivePipelinesNames().then((payload) => {
            const suppressOnDeselect = true;
            this.contentTreeOutline.removeChildren(suppressOnDeselect);
            for (let name of payload.pipelines) {
                let element = new WI.TreeElement(name);
                this.contentTreeOutline.appendChild(element);
            }
        }).catch((error) => {
            console.error("Could not fetch pipelines: ", error);
        });
    }

    _treeSelectionDidChange(event)
    {
        if (!this.selected)
            return;

        let treeElement = this.contentTreeOutline.selectedTreeElement;
        if (!treeElement)
            return;

        let pipelineName = treeElement.title;
        let target = WI.assumingMainTarget();
        let child = "";
        target.GStreamerAgent.dumpActivePipeline(pipelineName, child).then((payload) => {
            let documentFragment = WI.GStreamerView.renderSVGElement(payload.pipelineGraphRepresentation);
            let parentBinName = "";
            let binName = "";
            let representedObject = new WI.GStreamerPipeline(pipelineName, parentBinName, binName, payload.pipelineGraphRepresentation, documentFragment);
            WI.showRepresentedObject(representedObject);
        }).catch((error) => {
            console.error("Could not dump pipeline: ", error);
        });
    }
};