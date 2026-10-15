#!/usr/bin/env python3
"""Exercise the actual patched fprintd on a private bus and virtual-only driver.

Requires the exact fprintd source's upstream test dependencies (python3-dbusmock,
PyGObject, python3-cairo), a built candidate daemon, and a separate virtual-only
libfprint build with introspection. No system bus, USB devices, saved prints,
user credentials, or installed daemon are used. A test-only LD_PRELOAD helper
can supply nonempty synthetic metadata; it is never installed.

Example:
  python3 test-coverage-api.py --source /abs/fprintd-1.94.5 \
      --daemon-dir /abs/fprintd-1.94.5/build/src \
      --virtual-build /abs/coverage-test-libfprint -v
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET


def configure():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--daemon-dir", required=True, type=Path)
    parser.add_argument("--virtual-build", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    source = args.source.resolve()
    daemon_dir = args.daemon_dir.resolve()
    virtual_build = args.virtual_build.resolve()
    daemon = daemon_dir / "fprintd"
    if not daemon.is_file() or not os.access(daemon, os.X_OK):
        parser.error("candidate daemon must already be built")
    if daemon.resolve() in (Path("/usr/libexec/fprintd"), Path("/usr/lib/fprintd/fprintd")):
        parser.error("refusing to test the installed daemon")
    options = json.loads((virtual_build / "meson-info/intro-buildoptions.json").read_text())
    drivers = next(item["value"] for item in options if item["name"] == "drivers")
    if drivers != "virtual_image":
        parser.error("test libfprint must be built with only -Ddrivers=virtual_image")
    virtual_lib = virtual_build / "libfprint"
    if not (virtual_lib / "FPrint-2.0.typelib").is_file():
        parser.error("virtual libfprint introspection must already be built")
    # ld.so reads its library search path at process startup, before Python.
    if (os.environ.get("LD_LIBRARY_PATH") != str(virtual_lib) or
            os.environ.get("GI_TYPELIB_PATH") != str(virtual_lib)):
        child_env = os.environ.copy()
        child_env["LD_LIBRARY_PATH"] = str(virtual_lib)
        child_env["GI_TYPELIB_PATH"] = str(virtual_lib)
        os.execve(sys.executable, [sys.executable, *sys.argv], child_env)
    os.environ["FPRINT_BUILD_DIR"] = str(daemon_dir)
    os.environ["TOPSRCDIR"] = str(source)
    sys.path.insert(0, str(source / "tests"))
    spec = importlib.util.spec_from_file_location("upstream_fprintd_test", source / "tests/fprintd.py")
    upstream = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(upstream)
    sys.argv = [sys.argv[0], *remaining]
    return source, virtual_lib, upstream


SOURCE, VIRTUAL_LIB, UPSTREAM = configure()
from gi.repository import Gio, GLib


class CoverageEmptyTests(UPSTREAM.FPrintdVirtualDeviceBaseTest):
    inject_payload = False

    @classmethod
    def setUpClass(cls):
        cls.payload_dir = tempfile.TemporaryDirectory(prefix="fprintd-coverage-test-")
        cls.addClassCleanup(cls.payload_dir.cleanup)
        cls.payload_so = Path(cls.payload_dir.name) / "synthetic-payload.so"
        if cls.inject_payload:
            flags = shlex.split(subprocess.check_output(
                ["pkg-config", "--cflags", "--libs", "gobject-2.0"], text=True))
            subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(Path(__file__).with_name("coverage-api-test-payload.c")),
                            "-o", str(cls.payload_so), *flags, "-ldl"], check=True)
        super().setUpClass()

    def daemon_start(self, driver="Virtual image device for debugging"):
        # The upstream helper passes an environment copy only to the child.
        previous = os.environ.get("LD_PRELOAD")
        try:
            if self.inject_payload:
                os.environ["LD_PRELOAD"] = str(self.payload_so)
            else:
                os.environ.pop("LD_PRELOAD", None)
            super().daemon_start(driver)
        finally:
            if previous is None:
                os.environ.pop("LD_PRELOAD", None)
            else:
                os.environ["LD_PRELOAD"] = previous
        self.assertIsNotNone(self.device, "virtual device is mandatory; do not silently skip")
        maps = Path(f"/proc/{self.daemon.pid}/maps").read_text()
        self.assertIn(str((VIRTUAL_LIB / "libfprint-2.so.2.0.0").resolve()), maps)
        self.assertNotIn("/opt/goodix-libfprint/", maps)

    def coverage(self, device=None):
        result = (device or self.device).call_sync(
            "GetEnrollmentCoverage", None, Gio.DBusCallFlags.NONE, 2000, None)
        self.assertEqual(result.get_type_string(), "(a{sv})")
        return result.unpack()[0]

    def assertPayload(self, payload):
        if self.inject_payload:
            self.assertEqual(payload, {
                "test-only-sentinel": "synthetic coarse map; no fingerprint data"})
        else:
            self.assertEqual(payload, {})

    def secondary(self):
        bus, device = self.get_secondary_bus_and_device()
        self.addCleanup(bus.close_sync, None)
        self.assertNotEqual(bus.get_unique_name(), self.dbus.get_unique_name())
        return device

    def test_unclaimed_denied(self):
        with self.assertFprintError("ClaimDevice"):
            self.coverage()

    def test_owner_gets_payload_without_new_polkit_check(self):
        self.device.Claim("(s)", "coverage-test-user")
        self._polkitd_obj.SetAllowed([""])
        self._polkitd_obj.ClearCalls()
        for _ in range(3):
            self.assertPayload(self.coverage())
        self.assertEqual(list(self._polkitd_obj.GetMethodCalls("CheckAuthorization")), [])
        self.device.Release()

    def test_other_connection_denied_while_owner_succeeds(self):
        self.device.Claim("(s)", "coverage-test-user")
        stranger = self.secondary()
        self.assertPayload(self.coverage())
        with self.assertFprintError("AlreadyInUse"):
            self.coverage(stranger)
        self.assertPayload(self.coverage())
        self.device.Release()

    def test_release_revokes_access(self):
        self.device.Claim("(s)", "coverage-test-user")
        self.assertPayload(self.coverage())
        self.device.Release()
        with self.assertFprintError("ClaimDevice"):
            self.coverage()

    def test_pending_claim_authorization_does_not_grant_access(self):
        replies = []

        def done(proxy, result):
            try:
                replies.append(proxy.call_finish(result))
            except GLib.Error as error:
                replies.append(error)

        self._polkitd_obj.SimulateHang(True)
        self.device.call("Claim", GLib.Variant("(s)", ("coverage-test-user",)),
                         Gio.DBusCallFlags.NONE, 4000, None, done)
        try:
            deadline = time.monotonic() + 2
            while not self._polkitd_obj.HaveHangingCalls():
                self.assertLess(time.monotonic(), deadline, "claim authorization never started")
                while UPSTREAM.ctx.pending():
                    UPSTREAM.ctx.iteration(False)
                time.sleep(0.005)
            self.assertEqual(replies, [])
            with self.assertFprintError("ClaimDevice"):
                self.coverage()
            with self.assertFprintError("ClaimDevice"):
                self.coverage(self.secondary())
        finally:
            self._polkitd_obj.SimulateHang(False)
            self._polkitd_obj.ReleaseHangingCalls()
        deadline = time.monotonic() + 3
        while not replies:
            self.assertLess(time.monotonic(), deadline, "claim did not complete")
            while UPSTREAM.ctx.pending():
                UPSTREAM.ctx.iteration(False)
            time.sleep(0.005)
        if isinstance(replies[0], GLib.Error):
            raise replies[0]
        self.assertPayload(self.coverage())
        self.device.Release()

    def test_claim_transfer_follows_connection(self):
        self.device.Claim("(s)", "coverage-test-user")
        successor = self.secondary()
        self.device.Release()
        successor.Claim("(s)", "coverage-test-user")
        self.assertPayload(self.coverage(successor))
        with self.assertFprintError("AlreadyInUse"):
            self.coverage()
        successor.Release()

    def test_introspection_is_method_not_public_property(self):
        xml = self.dbus.call_sync(
            UPSTREAM.FPRINT_NAMESPACE, self.device.get_object_path(),
            "org.freedesktop.DBus.Introspectable", "Introspect", None,
            GLib.VariantType.new("(s)"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        interface = next(item for item in ET.fromstring(xml).findall("interface")
                         if item.attrib["name"] == UPSTREAM.FPRINT_NAMESPACE + ".Device")
        method = next(item for item in interface.findall("method")
                      if item.attrib["name"] == "GetEnrollmentCoverage")
        self.assertEqual([arg.attrib["type"] for arg in method.findall("arg")
                          if arg.attrib.get("direction") == "out"], ["a{sv}"])
        self.assertFalse(any("coverage" in item.attrib["name"].lower()
                             for item in interface.findall("property")))


class CoverageSyntheticPayloadTests(CoverageEmptyTests):
    inject_payload = True


if __name__ == "__main__":
    unittest.main()
