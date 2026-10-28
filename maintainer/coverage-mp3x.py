#!/usr/bin/env python3
"""coverage-mp3x.py - drive the mp3x frame analyzer for coverage-run.sh.

mp3x is a GTK 4 program, and a coverage run that only starts it measures its
start-up. This script works it the way a person would, without one: it starts
mp3x on an input file, and over D-Bus activates every action the program
publishes, in the order of its menus. The file dialogs are driven through the
accessibility bus (AT-SPI): the save dialog for each export target, and the
open and save dialogs once each with Cancel and with Escape.

The values of the actions with a string parameter (the channel, the source,
the spectrum and the window to show) are read from the menu model the running
program exports, not written down here. A renamed value then fails the run
instead of being skipped.

    coverage-mp3x.py --probe
        Exit 0 when this system can drive mp3x; else print why not, exit 1.
    coverage-mp3x.py MP3X INPUT OUTDIR
        Drive MP3X (the libtool wrapper in the build tree, which loads the
        build's own libmp3lame) on INPUT, then on a WAV of ten seconds that
        it writes to OUTDIR, long enough for the transport actions. The
        exported images and the program's output go to OUTDIR too. Run it
        under a private session bus (dbus-run-session), so that nothing
        reaches the desktop's own session. Exit 0 when every step did what
        it should, 1 otherwise, with one line per step.

Needs a display (DISPLAY), PyGObject with the Gio and Atspi 2.0 typelibs, and
an X server for GDK_BACKEND=x11. xdotool is optional: without it the Escape
steps are reported as skipped.

Part of the LAME distribution.  No warranty; see COPYING.
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import time
import wave

APP = '/org/sourceforge/lame/mp3x'
WIN = APP + '/window/1'
SAVE_TITLES = ('Save Screenshot', 'Export Plot')
OPEN_TITLE = 'Open Audio File'
EXPORT_TARGETS = range(9)
# Opens a window the driver cannot close, or ends the program: run apart.
NOT_IN_SWEEP = ('open', 'recent', 'close', 'export', 'quit', 'about', 'docs', 'stats')
STEPS = ('step', 'step', 'back', 'adv10', 'back', 'adv100', 'step')
END_STEPS = ('last', 'back', 'step')
TIMEOUT = 20.0
# Last Frame plots every frame of the transport file on the way.
ADVANCE_TIMEOUT = 300.0
TRANSPORT_SECONDS = 10
RATE = 44100

failures = 0


def say(*a):
    print(*a, flush=True)


def fail(msg):
    global failures
    failures += 1
    say('FAIL ' + msg)


def probe():
    """None when mp3x can be driven here, else the reason it cannot."""
    if not os.environ.get('DISPLAY'):
        return 'no display (DISPLAY is not set)'
    try:
        import gi
        gi.require_version('Atspi', '2.0')
        from gi.repository import Atspi, Gio  # noqa: F401
    except (ImportError, ValueError) as e:
        return 'no PyGObject with the Gio and Atspi 2.0 typelibs (%s)' % e
    return None


if len(sys.argv) == 2 and sys.argv[1] == '--probe':
    why = probe()
    if why:
        print(why)
        sys.exit(1)
    sys.exit(0)
if len(sys.argv) != 4:
    sys.exit(__doc__)
MP3X, FIRST, OUT = sys.argv[1:4]
why = probe()
if why:
    sys.exit('cannot drive mp3x: ' + why)

import gi  # noqa: E402
gi.require_version('Atspi', '2.0')
from gi.repository import Atspi, Gio, GLib  # noqa: E402

ctx = GLib.MainContext.default()


def pump(cond, timeout=TIMEOUT):
    """Run the main loop until cond() is true; False after timeout seconds."""
    end = time.time() + timeout
    while time.time() < end:
        while ctx.pending():
            ctx.iteration(False)
        if cond():
            return True
        time.sleep(0.05)
    return False


def bus_name(conn, pid):
    def call(method, args, reply):
        return conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                              method, args, GLib.VariantType(reply), Gio.DBusCallFlags.NONE, -1, None)
    end = time.time() + TIMEOUT
    while time.time() < end:
        for n in call('ListNames', None, '(as)').unpack()[0]:
            if not n.startswith(':'):
                continue
            try:
                if call('GetConnectionUnixProcessID', GLib.Variant('(s)', (n,)), '(u)').unpack()[0] == pid:
                    return n
            except GLib.Error:
                pass
        time.sleep(0.2)
    return None


def menu_targets(model):
    """{action: [string targets]} of every item below model, submenus and sections too."""
    found = {}

    def visit(m, depth):
        if depth > 8:
            return
        pump(lambda: m.get_n_items() > 0, 5.0)
        for i in range(m.get_n_items()):
            act = m.get_item_attribute_value(i, 'action', GLib.VariantType('s'))
            tgt = m.get_item_attribute_value(i, 'target', None)
            if act is not None and tgt is not None and tgt.get_type_string() == 's':
                name = act.get_string().split('.', 1)[-1]
                found.setdefault(name, []).append(tgt.get_string())
            for link in ('submenu', 'section'):
                sub = m.get_item_link(i, link)
                if sub is not None:
                    visit(sub, depth + 1)
    visit(model, 0)
    return found


# --- AT-SPI: the dialogs ------------------------------------------------------

def atspi_app(pid):
    end = time.time() + TIMEOUT
    while time.time() < end:
        d = Atspi.get_desktop(0)
        for i in range(d.get_child_count()):
            a = d.get_child_at_index(i)
            if a is not None and a.get_process_id() == pid:
                return a
        time.sleep(0.2)
    return None


def toplevels(app):
    return [c for c in (app.get_child_at_index(i) for i in range(app.get_child_count())) if c is not None]


def walk(acc, depth=0):
    if acc is None or depth > 40:
        return
    yield acc
    for i in range(acc.get_child_count()):
        yield from walk(acc.get_child_at_index(i), depth + 1)


def window(app, titles, timeout=TIMEOUT):
    end = time.time() + timeout
    while time.time() < end:
        for t in toplevels(app):
            if t.get_name() in titles:
                return t
        time.sleep(0.2)
    return None


def gone(app, title, timeout=TIMEOUT):
    end = time.time() + timeout
    while time.time() < end:
        if all(t.get_name() != title for t in toplevels(app)):
            return True
        time.sleep(0.2)
    return False


def buttons(acc):
    return [a for a in walk(acc) if a.get_role_name() in ('push button', 'button')]


def button(acc, label):
    for b in buttons(acc):
        if (b.get_name() or '').replace('_', '') == label:
            return b
    return None


def name_entry(dlg):
    for a in walk(dlg):
        if 'EditableText' in (a.get_interfaces() or []):
            try:
                n = Atspi.Text.get_character_count(a)
                if Atspi.Text.get_text(a, 0, n).endswith('.png'):
                    return a
            except GLib.Error:
                pass
    return None


def press(b):
    Atspi.Action.do_action(b, 0)


def escape(app, pid, title):
    """Send Escape to the window titled title; False when xdotool is missing."""
    if not shutil.which('xdotool'):
        return False
    r = subprocess.run(['xdotool', 'search', '--pid', str(pid), '--onlyvisible', '--name', '^%s$' % title],
                       capture_output=True, text=True)
    for wid in r.stdout.split()[:1]:
        subprocess.run(['xdotool', 'windowactivate', '--sync', wid], capture_output=True)
        subprocess.run(['xdotool', 'windowfocus', '--sync', wid], capture_output=True)
        subprocess.run(['xdotool', 'key', '--window', wid, '--clearmodifiers', 'Escape'], capture_output=True)
        # GTK drops a key sent to a window it does not consider focused. The
        # dialog has the focus once activated, so the key then goes there.
        if not gone(app, title, 1.0):
            subprocess.run(['xdotool', 'key', '--clearmodifiers', 'Escape'], capture_output=True)
    return True


def dismiss_alert(app, before):
    """Press the one button of the alert window that is not in before."""
    end = time.time() + TIMEOUT
    while time.time() < end:
        for t in toplevels(app):
            if t not in before:
                bs = buttons(t)
                if len(bs) == 1:
                    press(bs[0])
                    return pump(lambda: t not in toplevels(app))
        time.sleep(0.2)
    return False


def write_wav(path):
    """A stereo 16 bit WAV of TRANSPORT_SECONDS: a rising tone, and noise."""
    n = RATE * TRANSPORT_SECONDS
    seed = 1
    frames = bytearray()
    for i in range(n):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        tone = math.sin(2 * math.pi * (200.0 + 2000.0 * i / n) * i / RATE)
        left = int(9000 * tone) + (seed >> 19) - 2048
        right = int(9000 * tone) - (seed >> 19) + 2048
        frames += struct.pack('<hh', left, right)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))


# --- the run --------------------------------------------------------------------

def main():
    os.makedirs(OUT, exist_ok=True)
    env = dict(os.environ, GDK_BACKEND='x11')
    log = open(os.path.join(OUT, 'mp3x.log'), 'w')
    p = subprocess.Popen([MP3X, FIRST], cwd=OUT, env=env, stdout=log, stderr=subprocess.STDOUT)
    conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    name = bus_name(conn, p.pid)
    if name is None:
        p.kill()
        sys.exit('FAIL mp3x (pid %d) has no connection on the session bus' % p.pid)
    win = Gio.DBusActionGroup.get(conn, name, WIN)
    app_actions = Gio.DBusActionGroup.get(conn, name, APP)
    if not pump(lambda: win.has_action('close') and win.get_action_enabled('close')):
        p.kill()
        sys.exit('FAIL the file did not load: win.close never became enabled')
    acc = atspi_app(p.pid)
    if acc is None:
        p.kill()
        sys.exit('FAIL mp3x is not on the accessibility bus')
    pump(lambda: len(app_actions.list_actions()) > 0)
    say('mp3x up: %d window action(s), %d application action(s)'
        % (len(win.list_actions()), len(app_actions.list_actions())))

    def act(group, a, param=None):
        group.activate_action(a, param)
        pump(lambda: False, 0.2)

    def wait_enabled(a, timeout=TIMEOUT):
        return pump(lambda: win.get_action_enabled(a), timeout)

    # 1. the parameterless actions, the toggles twice so they end as they began
    for a in sorted(win.list_actions()):
        if a in NOT_IN_SWEEP or a in STEPS + END_STEPS or a in ('play', 'pause', 'playpause') \
                or win.get_action_parameter_type(a) is not None:
            continue
        n = 2 if win.get_action_state(a) is not None else 1
        for _ in range(n):
            act(win, a)
        say('ok   %s x%d' % (a, n))

    # 2. every value of the string actions, read from the menus
    menus = Gio.DBusMenuModel.get(conn, name, APP + '/menus/menubar')
    targets = menu_targets(menus)
    for a in sorted(win.list_actions()):
        t = win.get_action_parameter_type(a)
        if t is None or t.dup_string() != 's' or a == 'recent':
            continue
        values = targets.get(a)
        if not values:
            fail('the menus name no value of win.%s' % a)
            continue
        start = win.get_action_state(a)
        for v in values + [start.get_string()]:
            act(win, a, GLib.Variant('s', v))
            if not pump(lambda: win.get_action_state(a).get_string() == v):
                fail('win.%s did not take the value %r' % (a, v))
        say('ok   %s: %s' % (a, ' '.join(values)))

    # 3. the windows of the application menu; about is an alert with one button
    for a in ('stats', 'docs'):
        act(app_actions, a)
        say('ok   app.%s' % a)
    before = toplevels(acc)
    act(app_actions, 'about')
    if dismiss_alert(acc, before):
        say('ok   app.about, closed')
    else:
        fail('app.about: no alert to close')

    # 4. every export target through the save dialog
    for t in EXPORT_TARGETS:
        act(win, 'export', GLib.Variant('i', t))
        dlg = window(acc, SAVE_TITLES)
        if dlg is None:
            fail('export %d: no save dialog' % t)
            continue
        title = dlg.get_name()
        entry = name_entry(dlg)
        save = button(dlg, 'Save')
        if entry is None or save is None:
            fail('export %d: the dialog has no file name entry or no Save button' % t)
            continue
        path = os.path.join(OUT, 'export-%d.png' % t)
        if os.path.exists(path):
            os.remove(path)
        Atspi.EditableText.set_text_contents(entry, path)
        press(save)
        if not gone(acc, title) or not pump(lambda: os.path.exists(path) and os.path.getsize(path) > 0):
            fail('export %d: no image at %s' % (t, path))
        else:
            say('ok   export %d: "%s", %d bytes' % (t, title, os.path.getsize(path)))

    # 5. the save and open dialogs left with Cancel, then with Escape
    for a, param, title in (('export', GLib.Variant('i', 1), SAVE_TITLES[1]), ('open', None, OPEN_TITLE)):
        for how in ('Cancel', 'Escape'):
            act(win, a, param)
            dlg = window(acc, (title,))
            if dlg is None:
                fail('%s: no dialog "%s"' % (a, title))
                continue
            if how == 'Cancel' or not escape(acc, p.pid, title):
                if how == 'Escape':
                    say('skip %s, Escape: no xdotool; closed with Cancel' % a)
                b = button(dlg, 'Cancel')
                if b is None:
                    fail('%s: the dialog has no Cancel button' % a)
                    continue
                press(b)
            if gone(acc, title):
                say('ok   %s, %s' % (a, how))
            else:
                fail('%s: the dialog stayed open after %s' % (a, how))

    # 6. a longer file through the recent action. With Plot While Advancing on
    #    it runs slowly enough to be paused, and then the transport steps it.
    second = os.path.join(OUT, 'transport.wav')
    write_wav(second)
    if not win.get_action_state('plotadv').get_boolean():
        act(win, 'plotadv')
    act(win, 'recent', GLib.Variant('s', Gio.File.new_for_path(second).get_uri()))
    if window(acc, ('MP3x: transport.wav',)) is None:
        fail('recent: %s did not load' % second)
    elif not wait_enabled('pause', 5.0):
        fail('recent: %s was never seen running' % second)
    else:
        act(win, 'pause')
        idle = lambda: not win.get_action_enabled('pause')  # noqa: E731
        if pump(idle):
            say('ok   recent: %s, paused' % second)
        else:
            fail('pause: the file kept running')

        def steps(names):
            before = failures
            for a in names:
                if not wait_enabled(a):
                    fail('%s never became enabled' % a)
                    continue
                act(win, a)
                if not pump(idle, ADVANCE_TIMEOUT):
                    fail('%s did not stop' % a)
            if failures == before:
                say('ok   %s' % ' '.join(names))

        steps(STEPS)
        # Play and Pause from the menu, then twice the toggle; whether the
        # file runs shows in whether Pause is enabled.
        before = failures
        for a, running in (('play', True), ('pause', False), ('playpause', True), ('playpause', False)):
            if a != 'playpause' and not wait_enabled(a):
                fail('%s never became enabled' % a)
                continue
            act(win, a)
            if not pump(lambda: win.get_action_enabled('pause') == running):
                fail('%s: the file did not %s' % (a, 'run' if running else 'stop'))
        if failures == before:
            say('ok   play, pause, playpause x2')
        # Play is disabled at the end of the file, so the last frame comes last.
        steps(END_STEPS)
    act(win, 'plotadv')

    # 7. a file that does not exist: the error is shown, and closed
    before = toplevels(acc)
    missing = os.path.join(OUT, 'no such file.wav')
    act(win, 'recent', GLib.Variant('s', Gio.File.new_for_path(missing).get_uri()))
    if dismiss_alert(acc, before):
        say('ok   recent, a missing file: the error was shown and closed')
    else:
        fail('recent, a missing file: no error to close')

    # 8. close the file, then quit
    act(win, 'close')
    if pump(lambda: not win.get_action_enabled('close')):
        say('ok   close')
    else:
        fail('close: win.close is still enabled')
    act(app_actions, 'quit')
    try:
        rc = p.wait(TIMEOUT)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        fail('quit: mp3x did not end')
        rc = None
    if rc is not None:
        if rc == 0:
            say('ok   quit, rc 0')
        else:
            fail('quit: mp3x ended with rc %d' % rc)
    say('%d step(s) failed' % failures)
    return 1 if failures else 0


sys.exit(main())
