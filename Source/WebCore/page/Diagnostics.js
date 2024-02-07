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

var intervalRAM;
var intervalGFX;

function registerRAMcallback(func, triggerPoint, interval) {
    diagnostics.unregisterRAMcallback(); // Make sure only ever one callback is registered
    if (triggerPoint < 0 || triggerPoint > 250) {
        console.log("Error: invalid triggerPoint specified (" + triggerPoint + ")! Please specify triggerPoint in the range [0-250]");
        return;
    }
    if (interval < 0 || interval > 65535) {
        console.log("Error: invalid interval specified (" + interval + ")! Please specify interval in the range [0-65535]");
        return;
    }
    var checkOnce = false;
    if (typeof interval === 'undefined' || interval == 0) {
        interval = 1; // Check every 1s when we want to trigger only once.
        checkOnce = true;
    }
    intervalRAM = setInterval( () => {
        if (diagnostics.usedRamPercent >= triggerPoint) {
            func(true);
            if (checkOnce)
                diagnostics.unregisterRAMcallback();
        }
    }, interval * 1000); //param interval is in seconds
}

function unregisterRAMcallback() {
    if (typeof intervalRAM !== 'undefined')
        clearInterval(intervalRAM);
}


function registerGFXcallback(func, triggerPoint, interval) {
    diagnostics.unregisterGFXcallback(); // Make sure only ever one callback is registered
    if (triggerPoint < 0 || triggerPoint > 250) {
        console.log("Error: invalid triggerPoint specified (" + triggerPoint + ")! Please specify triggerPoint in the range [0-250]");
        return;
    }
    if (interval < 0 || interval > 65535) {
        console.log("Error: invalid interval specified (" + interval + ")! Please specify interval in the range [0-65535]");
        return;
    }
    var checkOnce = false;
    if (typeof interval === 'undefined' || interval == 0) {
        interval = 1; // Check every 1s when we want to trigger only once.
        checkOnce = true;
    }
    intervalGFX = setInterval( () => {
        if (diagnostics.usedGfxPercent >= triggerPoint) {
            func(true);
            if (checkOnce)
                diagnostics.unregisterGFXcallback();
        }
    }, interval * 1000); //param interval is in seconds
}

function unregisterGFXcallback() {
    if (typeof intervalGFX !== 'undefined')
        clearInterval(intervalGFX);
}

function logMemoryAnalysis() {
    console.log("Memory analysis:\n\tImage elements memory usage:\n\t\tEstimated RAM used by the img elements: " + diagnostics.imagesRamEstimate
        + "MB\n\t\tEstimated GFX used by the img elements: " + diagnostics.imagesGfxEstimate + "MB");
}

function logElementTEST(element) {
    console.log(element);
}

function imagesAnalysis() {
    var images = document.getElementsByTagName("img");
    var totalRamEstimate = 0;
    var totalGfxEstimate = 0;
    var sortedRam = [];
    var sortedGfx = [];

    for (var i = 0; i < images.length; i++) {
        if (images[i].estimatedRam > 0 || images[i].estimatedGfx > 0) {
            totalRamEstimate += images[i].estimatedRam;
            totalGfxEstimate += images[i].estimatedGfx;
            images[i].isUsingGfx ? sortedGfx.push([images[i], images[i].estimatedGfx]) : sortedRam.push([images[i], images[i].estimatedRam]);
        }
    }
    sortedRam.sort(function(a,b) {return b[0].estimatedRam - a[0].estimatedRam;});
    sortedGfx.sort(function(a,b) {return b[0].estimatedGfx - a[0].estimatedGfx;});

    console.log("Memory Analysis for HTML Image elements:" +
    "\n\tEstimated total RAM used by images: " + totalRamEstimate + "MB" +
    "\n\tEstimated total GFX used by images: " + totalGfxEstimate + "MB");

    if (sortedRam.length) {
        console.group("\tRAM use of each Image element (high-low):" +
        "\n\tImage element:\tEstimated RAM (MB):");
        console.log(sortedRam);
        console.groupEnd();
    }
    if (sortedGfx.length) {
        console.group("\tGFX use of each Image element (high-low):" +
        "\n\tImage element:\tEstimated GFX (MB):");
        console.log(sortedGfx);
        console.groupEnd();
    }
}
