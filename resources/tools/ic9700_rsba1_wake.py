#!/usr/bin/env python3
"""Wake one IC-9700 from standby over a direct RS-BA1 session."""

from ic9700_rsba1_shared import (
    PowerToolError,
    install_abrupt_interrupt_handler,
    log_event,
    read_credentials,
    wake_from_standby,
)


def main() -> int:
    install_abrupt_interrupt_handler()
    credentials = read_credentials(
        "ic9700_rsba1_wake.py",
        "Wake an IC-9700 from remote-control standby.")
    wake_from_standby(credentials)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, PowerToolError) as error:
        log_event("FAIL", str(error))
        raise SystemExit(1)
