# weston 16 with its test plugin, which upstream builds but does not install.
# The plugin gives a seat ("test-seat") and the weston_test global
# (send_key, move_pointer, send_button, send_axis, activate_surface), so a
# test can type into a client under a headless weston.  Loaded on its own,
# without weston's test runner, it would dereference the runner's missing
# data, so the patch gives it an empty one whose type runs nothing.
# egl-vendor.json lets its GL renderer find Mesa (llvmpipe) off NixOS.
# Pinned: the patch below must apply to the weston it builds.
let pkgs = import (builtins.fetchTarball "https://releases.nixos.org/nixpkgs/nixpkgs-26.11pre1121398.1c872b8c508b/nixexprs.tar.xz") {};
in pkgs.weston.overrideAttrs (o: {
  postPatch = (o.postPatch or "") + ''
    substituteInPlace tests/harness/weston-test.c --replace-fail \
      '	loop = wl_display_get_event_loop(ec->wl_display);' \
      '	if (!weston_compositor_get_test_data(ec)) { static struct wet_testsuite_data none; none.type = (enum test_type)99; wl_list_init(&none.breakpoints.list); ec->test_data.test_private_data = &none; }
	loop = wl_display_get_event_loop(ec->wl_display);'
  '';
  postInstall = (o.postInstall or "") + ''
    cp tests/harness/test-plugin.so $out/lib/weston/
    cp ../protocol/weston-test.xml ../protocol/weston-output-capture.xml $out/share/
    ln -s ${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json $out/share/egl-vendor.json
  '';
})
