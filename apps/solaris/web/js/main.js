/*
 * Solaris — main.js: boot one page = one view of the session (R-SVC-7, NTWB-11 MVVM).
 *
 *   NTWB bridge (/ntwb/ntwb.js) -> SolarisSession (model/session.js: the model's proxy)
 *                               -> this page's view-model (vm/app.js)
 *                               -> the shell (shell.js: App.cpp — the root, the screens, keys, menus, the toast)
 *                                    -> Home (home/home.js) | the song (project/screen.js -> its slots)
 *
 * js/core/*, js/ui/* and css/{panels,dialogs,ui-kit}.css are COSMO's, borrowed by `solaris-cc ntwb install` (web/BORROW)
 * — they exist only in the installed tree. window.solaris = {vm, session, bridge, shell} for the console and the tests.
 */
import { SolarisSession } from "./model/session.js";
import { createViewModel } from "./vm/app.js";
import { mount } from "./shell.js";
import { applyNativeView } from "./vm/fixture.js";

const bridge = NTWB.connect();
const session = new SolarisSession(bridge);
const vm = createViewModel(session);
const shell = mount(document.getElementById("app"), vm);
window.solaris = { vm, session, bridge, shell };
bridge.onError((e) => vm.toast(String(e), true));

// a native shot's fixture (tools/fixture_host.mjs's fake NTWB): its view state, into this page's view-model
const fixture = () => applyNativeView(vm, window.__ntwbFixture && window.__ntwbFixture.view, shell);
if (window.__ntwbFixture) fixture(); else window.addEventListener("ntwb-fixture", fixture, { once: true });
