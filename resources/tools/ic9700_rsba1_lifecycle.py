#!/usr/bin/env python3
"""Open, hold, validate, and close one IC-9700 RS-BA1 session."""

import argparse

from ic9700_rsba1_shared import (
    Credentials,
    DEFAULT_LIFECYCLE_SECONDS,
    PowerToolError,
    install_abrupt_interrupt_handler,
    log_event,
    log_startup_summary,
    open_awake_session,
)


def parse_arguments() -> tuple[Credentials, float]:
    parser = argparse.ArgumentParser(
        description="Connect to an IC-9700, wake it if needed, hold the session, and disconnect.")
    parser.add_argument(
        "--radio-ip", required=True,
        help="IC-9700 IPv4 address")
    parser.add_argument(
        "--radio-username", required=True,
        help="user configured under Network > Network User")
    parser.add_argument(
        "--radio-password", required=True,
        help="password for the configured radio network user")
    parser.add_argument(
        "--run-time", type=float, default=DEFAULT_LIFECYCLE_SECONDS,
        metavar="SECONDS",
        help="seconds to keep the validated RS-BA1 session connected "
             "(default: 65, enough to validate three token renewals)")
    args = parser.parse_args()
    if not args.radio_username or not args.radio_password:
        parser.error("username and password must not be empty")
    if args.run_time <= 0:
        parser.error("--run-time must be greater than zero")
    return Credentials(args.radio_ip, args.radio_username, args.radio_password), args.run_time


def main() -> int:
    install_abrupt_interrupt_handler()
    credentials, run_time = parse_arguments()
    log_event(
        "ACTION",
        f"start lifecycle radio={credentials.host} run-time={run_time:g}s")
    with open_awake_session(credentials) as session:
        log_startup_summary(session)
        # This boundary means login, authentication, stream negotiation, any
        # required recovery/wake action, and directed CI-V validation are all
        # complete. Everything after it is steady-state session maintenance.
        log_event("STATE", "initial connection complete; entering maintenance phase")
        session.hold(run_time)
        if not session.directed_identity():
            raise PowerToolError("command plane did not answer after the requested run time")
        log_event("PASS", "post-run directed CI-V validation succeeded")
    log_event("PASS", "owned session disconnected")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, PowerToolError) as error:
        log_event("FAIL", str(error))
        raise SystemExit(1)
