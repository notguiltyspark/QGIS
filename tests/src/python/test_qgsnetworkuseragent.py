"""
Tests for changing User-Agent final string

Run: ctest -R PyQgsNetworkUserAgent -V

.. note:: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

"""
Test: User-Agent controls (suffix/override) in QgsNetworkAccessManager

This test spins up a local HTTP echo server that returns all request headers as JSON,
then sends requests via QgsNetworkAccessManager and validates the User-Agent behavior.

Scenarios:
  1) Default UA (no attributes)
  2) UA with AttributeUserAgentSuffix
  3) UA with AttributeUserAgentOverride
  4) Interaction with request preprocessor (should run after UA assembly)
"""

import json
import socket
import threading
import time
import unittest

from http.server import BaseHTTPRequestHandler, HTTPServer

from qgis.testing import start_app, unittest as qgis_unittest
from qgis.core import (
    QgsSettings,
    QgsNetworkAccessManager,
    QgsNetworkRequestParameters,
)

from qgis.PyQt.QtCore import QEventLoop, QTimer, QUrl
from qgis.PyQt.QtNetwork import QNetworkRequest


start_app()


# -------------------- Local Echo Server --------------------

class _EchoHandler(BaseHTTPRequestHandler):
    def _reply(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.end_headers()
        headers = {k: v for k, v in self.headers.items()}
        body = {
            "path": self.path,
            "headers": headers,
        }
        self.wfile.write(json.dumps(body, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        self._reply()

    def do_POST(self):
        self._reply()

    # silence default stderr logging to keep test output clean
    def log_message(self, format, *args):
        pass


class _EchoServer(object):
    def __init__(self):
        # Bind on ephemeral port to avoid collisions
        self._server = HTTPServer(("127.0.0.1", 0), _EchoHandler)
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.port}/echo"

    def start(self):
        self._thread.start()
        # very short settle time
        time.sleep(0.05)

    def stop(self):
        try:
            self._server.shutdown()
        finally:
            self._server.server_close()


# -------------------- The Test Case --------------------

class TestQgsNetworkUserAgent(qgis_unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._echo = _EchoServer()
        cls._echo.start()

    @classmethod
    def tearDownClass(cls):
        try:
            cls._echo.stop()
        finally:
            super().tearDownClass()

    def setUp(self):
        super().setUp()
        # Make UA prefix deterministic for all tests
        self._settings = QgsSettings()
        self._ua_key = "/qgis/networkAndProxy/userAgent"
        self._ua_prev = self._settings.value(self._ua_key, None)
        self._settings.setValue(self._ua_key, "TestPrefix/0.0")

        # No preprocessors by default
        self._pp_ids = []

    def tearDown(self):
        # remove any preprocessors we set
        for pid in self._pp_ids:
            try:
                QgsNetworkAccessManager.removeRequestPreprocessor(pid)
            except Exception:
                pass

        # restore UA prefix
        if self._ua_prev is None:
            self._settings.remove(self._ua_key)
        else:
            self._settings.setValue(self._ua_key, self._ua_prev)

        super().tearDown()

    # ---- helpers ----

    def _fetch(self, req: QNetworkRequest, timeout_ms: int = 5000) -> dict:
        """Perform the network request synchronously and return parsed JSON body."""
        loop = QEventLoop()
        timed_out = {"flag": False}

        def on_timeout():
            timed_out["flag"] = True
            loop.quit()

        timer = QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(on_timeout)
        timer.start(timeout_ms)

        reply = QgsNetworkAccessManager.instance().get(req)
        reply.finished.connect(loop.quit)
        loop.exec_()
        timer.stop()

        self.assertFalse(timed_out["flag"], "Network request timed out")

        payload = bytes(reply.readAll()).decode("utf-8")
        try:
            return json.loads(payload)
        except Exception as e:
            self.fail(f"Echo-server returned non-JSON or invalid payload: {payload!r}\nError: {e}")

    def _make_request(self) -> QNetworkRequest:
        return QNetworkRequest(QUrl(self._echo.url))

    # ---- tests ----

    def test_constants_visible_in_sip(self):
        """SIP should expose new enum values with expected numeric offsets."""
        # They must exist
        self.assertTrue(
            hasattr(QgsNetworkRequestParameters, "AttributeUserAgentSuffix"),
            "Missing AttributeUserAgentSuffix in SIP",
        )
        self.assertTrue(
            hasattr(QgsNetworkRequestParameters, "AttributeUserAgentOverride"),
            "Missing AttributeUserAgentOverride in SIP",
        )

        # And have stable offsets relative to QNetworkRequest.User
        base = int(QNetworkRequest.User)
        suffix_val = int(QgsNetworkRequestParameters.AttributeUserAgentSuffix)
        override_val = int(QgsNetworkRequestParameters.AttributeUserAgentOverride)

        self.assertEqual(suffix_val - base, 3002, "Unexpected numeric offset for AttributeUserAgentSuffix")
        self.assertEqual(override_val - base, 3003, "Unexpected numeric offset for AttributeUserAgentOverride")

    def test_default_user_agent(self):
        """Without attributes, UA should be '<prefix> QGIS/.../...'."""
        req = self._make_request()
        body = self._fetch(req)
        ua = body["headers"].get("User-Agent", "")

        self.assertTrue(ua.startswith("TestPrefix/0.0 "), f"UA must start with 'TestPrefix/0.0 ': {ua!r}")
        self.assertIn("QGIS/", ua, f"UA must contain 'QGIS/': {ua!r}")

    def test_user_agent_suffix(self):
        """Suffix should append to the standard UA with a preceding space."""
        req = self._make_request()
        req.setAttribute(
            QNetworkRequest.Attribute(QgsNetworkRequestParameters.AttributeUserAgentSuffix),
            "TestPlugin/1.2",
        )
        body = self._fetch(req)
        ua = body["headers"].get("User-Agent", "")

        self.assertTrue(ua.startswith("TestPrefix/0.0 "), f"UA must start with prefix: {ua!r}")
        self.assertIn("QGIS/", ua, f"UA must contain 'QGIS/': {ua!r}")
        self.assertTrue(ua.endswith(" TestPlugin/1.2"), f"UA must end with suffix: {ua!r}")

    def test_user_agent_override(self):
        """Override should completely replace UA."""
        req = self._make_request()
        req.setAttribute(
            QNetworkRequest.Attribute(QgsNetworkRequestParameters.AttributeUserAgentOverride),
            "CustomAgent/9.9 Test/Suffix",
        )
        body = self._fetch(req)
        ua = body["headers"].get("User-Agent", "")

        self.assertEqual(ua, "CustomAgent/9.9 Test/Suffix", f"Override must be exact: {ua!r}")

    def test_suffix_and_preprocessor_order(self):
        """Preprocessor should still be able to adjust UA after suffix is applied."""
        def pp(req):
            # append a marker to what NAM has already set
            ua = bytes(req.rawHeader(b"User-Agent")).decode("latin1")
            req.setRawHeader(b"User-Agent", (ua + " PP/0.1").encode("latin1"))

        pid = QgsNetworkAccessManager.setRequestPreprocessor(pp)
        self._pp_ids.append(pid)

        req = self._make_request()
        req.setAttribute(
            QNetworkRequest.Attribute(QgsNetworkRequestParameters.AttributeUserAgentSuffix),
            "TestPlugin/1.2",
        )
        body = self._fetch(req)
        ua = body["headers"].get("User-Agent", "")

        self.assertTrue(ua.endswith(" TestPlugin/1.2 PP/0.1"), f"Preprocessor must run after suffix: {ua!r}")


if __name__ == "__main__":
    unittest.main()
