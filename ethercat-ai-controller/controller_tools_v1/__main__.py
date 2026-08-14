"""CLI for offline validation and the loopback mock server."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .http_server import create_server
from .repository import MockRepository, load_json
from .semantic import ContractValidator, validation_report


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="controller-tools-v1",
        description="Mock-only EtherCAT Controller AI Gateway contract lab",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser(
        "validate", help="validate one JSON artifact without storing it"
    )
    validate.add_argument("path", type=Path)
    serve = subparsers.add_parser(
        "serve", help="serve the read-only gateway on a loopback address"
    )
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8765)
    subparsers.add_parser("list-tools", help="print the fixed MCP tool catalogue")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    repository = MockRepository()
    if arguments.command == "validate":
        try:
            document = load_json(arguments.path)
        except (OSError, json.JSONDecodeError) as error:
            print(
                json.dumps(
                    {"valid": False, "error": str(error)},
                    ensure_ascii=False,
                    sort_keys=True,
                )
            )
            return 2
        report = validation_report(ContractValidator(repository).validate(document))
        print(json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2))
        return 0 if report["valid"] else 2
    if arguments.command == "list-tools":
        from .gateway import ReadOnlyGateway

        print(
            json.dumps(
                ReadOnlyGateway(repository).tool_definitions,
                ensure_ascii=False,
                sort_keys=True,
                indent=2,
            )
        )
        return 0
    if arguments.command == "serve":
        try:
            server = create_server(arguments.host, arguments.port)
        except ValueError as error:
            print(str(error), file=sys.stderr)
            return 2
        host, port = server.server_address[:2]
        print(
            f"controller-tools-v1 mock listening on http://{host}:{port}",
            file=sys.stderr,
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
