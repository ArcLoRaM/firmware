"""ArcLog host tooling: parse, capture, view, merge and analyse ArcLoRaM traces.

The line format is defined by Common/Log/arclog.h and documented in
CONTEXT.md (ArcLog).
"""

from arclog.model import Kind, Line, parse_line, read_lines

__all__ = ["Kind", "Line", "parse_line", "read_lines"]
