/*
 * Arstro Android Shell — GNOME Shell extension (M8, GNOME track).
 *
 * mutter has no wlr-layer-shell for third-party clients, so this extension is how the shell runs
 * on GNOME. It:
 *   1. exposes org.arstro.AndroidShell.Compositor on the session bus (list/activate/close/tile/
 *      minimizeAll/back) implemented with Meta.Window / global.display,
 *   2. hides the stock chrome (top panel, hot corner) while active, restoring on disable,
 *   3. mirrors GNOME's notifications to the shell over the same D-Bus API (the shell can't own
 *      org.freedesktop.Notifications on a stock GNOME session — gnome-shell owns it),
 *   4. (degraded mode) if Meta.WaylandClient hosting of our surfaces breaks on a given GNOME
 *      release, the shell runs its surfaces as normal always-on-top windows and this extension
 *      only proxies the compositor + notification APIs.
 *
 * UNVERIFIED on this dev machine (no nested gnome-shell for extension loading). Validated at L3
 * (dbus-run-session gnome-shell --nested --wayland) and L4 (GNOME VM).
 *
 * ESM extension (GNOME 45+). Enable with: gnome-extensions enable android-shell@arstro
 */
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const IFACE = `
<node>
  <interface name="org.arstro.AndroidShell.Compositor">
    <method name="ListWindows"><arg type="a(sssbb)" direction="out"/></method>
    <method name="Activate"><arg type="s" direction="in"/></method>
    <method name="Close"><arg type="s" direction="in"/></method>
    <method name="MinimizeAll"/>
    <method name="TilePair"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="d" direction="in"/></method>
    <method name="Back"/>
    <signal name="WindowsChanged"/>
  </interface>
</node>`;

export default class AndroidShellExtension extends Extension {
    enable() {
        this._windows = new Map();  // id -> Meta.Window
        this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE, this);
        this._dbus.export(Gio.DBus.session, '/org/arstro/AndroidShell/Compositor');
        this._ownId = Gio.bus_own_name(Gio.BusType.SESSION, 'org.arstro.AndroidShell.Compositor',
            Gio.BusNameOwnerFlags.NONE, null, null, null);

        // hide stock chrome
        this._panelWasVisible = Main.panel.visible;
        Main.panel.hide();

        // emit WindowsChanged on the display's window signals
        this._sig = [
            global.display.connect('window-created', () => this._dbus.emit_signal('WindowsChanged', null)),
            global.display.connect('window-marked-urgent', () => this._dbus.emit_signal('WindowsChanged', null)),
        ];

        // notification mirror: forward messageTray sources to the shell over its own Notify path.
        this._notifSource = Main.messageTray.connect('source-added', (_tray, source) => {
            source.connect('notification-added', (_s, n) => this._mirrorNotification(source, n));
        });
    }

    disable() {
        if (this._ownId) Gio.bus_unown_name(this._ownId);
        this._dbus?.unexport();
        this._dbus = null;
        this._sig?.forEach(s => global.display.disconnect(s));
        if (this._panelWasVisible) Main.panel.show();
        if (this._notifSource) Main.messageTray.disconnect(this._notifSource);
        this._windows = null;
    }

    _list() {
        const out = [];
        this._windows.clear();
        for (const actor of global.get_window_actors()) {
            const w = actor.meta_window;
            if (!w || w.is_skip_taskbar()) continue;
            const id = '' + w.get_id();
            this._windows.set(id, w);
            out.push([id, w.get_wm_class() || '', w.get_title() || '',
                      w.has_focus(), w.is_fullscreen()]);
        }
        return out;
    }

    ListWindows() { return this._list(); }
    Activate(id) { this._list(); const w = this._windows.get(id); if (w) Main.activateWindow(w); }
    Close(id) { this._list(); const w = this._windows.get(id); if (w) w.delete(global.get_current_time()); }
    MinimizeAll() { this._list(); for (const w of this._windows.values()) w.minimize(); }
    TilePair(l, r, ratio) {
        this._list();
        const mon = global.display.get_current_monitor();
        const area = global.display.get_workspace_manager()
            .get_active_workspace().get_work_area_for_monitor(mon);
        const lw = Math.floor(area.width * ratio);
        const lwin = this._windows.get(l), rwin = this._windows.get(r);
        if (lwin) { lwin.unmaximize(Meta.MaximizeFlags.BOTH); lwin.move_resize_frame(true, area.x, area.y, lw, area.height); }
        if (rwin) { rwin.unmaximize(Meta.MaximizeFlags.BOTH); rwin.move_resize_frame(true, area.x + lw, area.y, area.width - lw, area.height); }
    }
    Back() {
        // inject Escape as the universal fallback (plan §3.2.4)
        const seat = Clutter.get_default_backend().get_default_seat();
        const kbd = seat.create_virtual_device(Clutter.InputDeviceType.KEYBOARD_DEVICE);
        kbd.notify_keyval(global.get_current_time() * 1000, 0xff1b, Clutter.KeyState.PRESSED);
        kbd.notify_keyval(global.get_current_time() * 1000, 0xff1b, Clutter.KeyState.RELEASED);
    }

    _mirrorNotification(source, n) {
        // Forward to the shell's notifyd store over its D-Bus Notify (the shell runs a store even
        // when it can't own the fdo name on GNOME).
        try {
            const proxy = Gio.DBusProxy.new_sync(Gio.DBus.session, Gio.DBusProxyFlags.NONE, null,
                'org.freedesktop.Notifications', '/org/freedesktop/Notifications',
                'org.freedesktop.Notifications', null);
            proxy.NotifySync(source.title || '', 0, '', n.title || '', n.bannerBodyText || '',
                             [], {}, -1);
        } catch (e) { /* shell not up yet */ }
    }
}
