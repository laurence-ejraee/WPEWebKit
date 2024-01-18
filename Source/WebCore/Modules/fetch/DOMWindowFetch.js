/*
 * Copyright (C) 2016 Canon Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, provided that the following conditions
 * are required to be met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Canon Inc. nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CANON INC. AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CANON INC. AND ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

function isNetworkConnected()
{
    // console.log("isNetworkConected: state " + window.internetAvailableStateDm);
    return window.internetAvailableStateDm;
}


function addNetworkStateChangeListener(func, url, timeout) {
    // console.log("addNetworkStateChangeListener: Test");
    timeout = timeout*1000; // API doc states time provided is in seconds.
    firstcallDm = true;
    window.internetAvailableStateDm = false; // js code, variable is  available to other functions.
    setInterval(f=>{
            fetch(url, {mode: 'no-cors'}).then(r=>{
                // Call the func callback if the internet available state changes.
                if (!window.internetAvailableStateDm)
                {
                    // console.log("addNetworkStateChangeListener: State changed to Connected");
                    window.internetAvailableStateDm = true;
                    func(true);
                }
                firstcallDm = false;
              })
              .catch(e=>{
                  // Call the func callback if the internet available state changes. Added firstcall to call this func once as state is initialized to false
                  // on app side there could be an expectation for a call when system starts and is offline. All other calls to  callback are on state change.
                if (firstcallDm || window.internetAvailableStateDm)
                {
                    // console.log("addNetworkStateChangeListener: State changed to NOT Connected");
                    window.internetAvailableStateDm = false;
                    func(false);
                }
                firstcallDm = false;
                });
    }, timeout);
}