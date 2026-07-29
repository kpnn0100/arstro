# Packaging (M9)

- `sessions/*.desktop` -> `/usr/share/wayland-sessions/` (login-screen entries).
- `android-shell-plasma-session.sh` -> `/usr/bin/arstro-android-shell-plasma-session`
  (kwin_wayland without plasmashell + our shell + a gesture-suppressing kwinrc).
- `systemd/arstro-android-shell.service` -> user unit bound to graphical-session.target.
- deb/rpm built via CPack from the CMake install rules (`build-packages.sh`).

Subpackages (plan §7): base (binary+assets+fonts), `-plasma` (kwin-script + session),
`-gnome` (extension + session). v1 CPack ships one package with everything; distro-native
`debian/` + `.spec` are a later split.
