from __future__ import annotations

import io
import json
import unittest
from contextlib import redirect_stderr, redirect_stdout

from controller_tools_v1.__main__ import main
from controller_tools_v1.repository import project_root


class CliTests(unittest.TestCase):
    def test_validate_checked_in_project(self) -> None:
        output = io.StringIO()
        path = project_root() / "mock" / "project" / "controller-project.json"
        with redirect_stdout(output):
            result = main(["validate", str(path)])
        self.assertEqual(result, 0)
        self.assertTrue(json.loads(output.getvalue())["valid"])

    def test_list_tools_returns_fixed_catalogue(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            result = main(["list-tools"])
        self.assertEqual(result, 0)
        self.assertEqual(len(json.loads(output.getvalue())), 9)

    def test_serve_refuses_public_bind(self) -> None:
        error = io.StringIO()
        with redirect_stderr(error):
            result = main(["serve", "--host", "0.0.0.0", "--port", "0"])
        self.assertEqual(result, 2)
        self.assertIn("loopback", error.getvalue())


if __name__ == "__main__":
    unittest.main()
