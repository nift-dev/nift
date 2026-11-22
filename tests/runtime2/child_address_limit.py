"""Build the Darwin child command with its native address-space guard."""
from pathlib import Path
import sys


def address_limited(command, allowance):
    if sys.platform != "darwin":
        return command
    launcher = Path(__file__).resolve().parents[2] / ".build/darwin-address-limit"
    return [str(launcher), str(allowance), "--", *command]
