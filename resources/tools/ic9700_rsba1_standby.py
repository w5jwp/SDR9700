#!/usr/bin/env python3
"""Put one IC-9700 into remote-control standby over a direct RS-BA1 session."""

from ic9700_rsba1_shared import (
    PowerToolError,
    enter_standby,
    install_abrupt_interrupt_handler,
    log_event,
    read_credentials,
)


def main() -> int:
    install_abrupt_interrupt_handler()
    credentials = read_credentials(
        "ic9700_rsba1_standby.py",
        "Put an IC-9700 into remote-control standby.")
    enter_standby(credentials)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, PowerToolError) as error:
        log_event("FAIL", str(error))
        raise SystemExit(1)
