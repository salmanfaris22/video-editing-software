"""A real MCP client (the official Python SDK, default "auto" mode: it probes
`server/discover` first, then falls back to `initialize`) for
McpBridge.RealClientThroughTheApp:

    LECTERN_TEST_MCP_CLIENT='uv run -q --no-project --with mcp python tests/mcp/sdk_client.py' \
        build/dev/bin/lectern_ui_tests --gtest_filter='McpBridge.RealClient*'

Reads the assistant setup from $LECTERN_TEST_MCP_CONFIG (the "Copy Setup" JSON),
lists the tools, adds a marker named "real-client" and prints what it did.
Exits non-zero on any failure."""
import asyncio
import json
import os
import sys

from mcp.client.client import Client
from mcp.client.stdio import StdioServerParameters


async def main() -> int:
    setup = json.load(open(os.environ["LECTERN_TEST_MCP_CONFIG"]))["mcpServers"]["lectern"]
    server = StdioServerParameters(command=setup["command"], args=setup.get("args", []))
    async with Client(server) as client:
        print("connected:", client.server_info.name if client.server_info else "?", "protocol", client.protocol_version)
        tools = await client.list_tools()
        print("tools:", len(tools.tools))
        # Lectern asks the user first; retry while the approval is pending.
        for _ in range(50):
            result = await client.call_tool("add_marker", {"time": 1.0, "label": "real-client"})
            text = result.content[0].text if result.content else ""
            if not result.is_error or "approval" not in text:
                break
            await asyncio.sleep(0.2)
        print("add_marker:", "error: " + text if result.is_error else "ok")
        return 1 if result.is_error else 0


sys.exit(asyncio.run(main()))
