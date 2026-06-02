"""
Download all your WiGLE-uploaded scan runs and check them against flockdar detection.

Workflow:
  1. Fetch your transaction list from /api/v2/file/transactions (paginated)
  2. For each transaction, download /api/v2/file/csv/{transid} if not already cached
  3. Run detection on every cached CSV and merge results by MAC
  4. Print a summary table; optionally write hits to --out CSV or KML

Cache: ~/.cache/flockdar/runs/{transid}.csv
Credentials: same as enrich.py — env WIGLE_API_NAME/WIGLE_API_TOKEN or
             ~/.config/flockdar/config.json
"""

from __future__ import annotations

import argparse
import base64
import os
import sys
import time
from pathlib import Path
from typing import Any

import httpx

from .detect import Hit, export_csv, export_kml, freq_to_channel, run_detection
from .enrich import _cache_dir, load_config

_WIGLE_BASE = "https://api.wigle.net/api/v2"
_UA = "flockdar/1.0"
_PAGE_SIZE = 100
_DL_DELAY_S = 2.0   # seconds between CSV downloads
_DL_TIMEOUT = 120.0  # WiGLE generates files on-demand; can be slow


# ---------------------------------------------------------------------------
# Cache helpers
# ---------------------------------------------------------------------------


def _runs_dir() -> Path:
    d = _cache_dir() / "runs"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _csv_path(transid: str) -> Path:
    return _runs_dir() / f"{transid}.csv"


# ---------------------------------------------------------------------------
# WiGLE API calls (synchronous httpx)
# ---------------------------------------------------------------------------


def _auth_header(api_name: str, api_token: str) -> str:
    cred = base64.b64encode(f"{api_name}:{api_token}".encode()).decode()
    return f"Basic {cred}"


def list_transactions(client: httpx.Client) -> list[dict[str, Any]]:
    """Fetch all upload transactions, handling pagination."""
    transactions: list[dict[str, Any]] = []
    page_start = 0

    while True:
        resp = client.get(
            f"{_WIGLE_BASE}/file/transactions",
            params={"pagestart": page_start},
        )
        resp.raise_for_status()
        data = resp.json()

        if not data.get("success"):
            raise RuntimeError(f"WiGLE API error: {data}")

        batch: list[dict[str, Any]] = data.get("results") or []
        transactions.extend(batch)
        page_start += len(batch)

        # Stop when we get a partial page
        if len(batch) < _PAGE_SIZE:
            break

        time.sleep(0.5)

    return transactions


def download_csv(client: httpx.Client, transid: str, dest: Path) -> None:
    """Download the CSV for one transaction and write it to dest."""
    resp = client.get(f"{_WIGLE_BASE}/file/csv/{transid}")
    resp.raise_for_status()
    dest.write_bytes(resp.content)


# ---------------------------------------------------------------------------
# Core sync logic
# ---------------------------------------------------------------------------


def sync_and_detect(
    api_name: str,
    api_token: str,
    *,
    dry_run: bool = False,
    yes: bool = False,
    out: str | None = None,
    min_confidence: int = 1,
) -> None:
    auth = _auth_header(api_name, api_token)
    headers = {"Authorization": auth, "User-Agent": _UA}

    print("Fetching transaction list from WiGLE…")
    with httpx.Client(headers=headers, timeout=30) as client:
        try:
            transactions = list_transactions(client)
        except httpx.HTTPStatusError as exc:
            sys.exit(f"WiGLE API error {exc.response.status_code}: {exc.response.text[:200]}")

    total = len(transactions)
    cached = [t for t in transactions if _csv_path(t["transid"]).exists()]
    to_download = [t for t in transactions if not _csv_path(t["transid"]).exists()]

    print(
        f"  {total} run(s) found  •  {len(cached)} cached  •  {len(to_download)} to download"
    )

    if dry_run:
        print("\nTransactions:")
        for t in transactions:
            marker = "✓" if _csv_path(t["transid"]).exists() else "↓"
            obs = t.get("total", t.get("totalGps", "?"))
            fname = t.get("fileName", t["transid"])
            print(f"  [{marker}] {t['transid']}  obs={obs}  {fname}")
        return

    if to_download:
        cost_note = f"{len(to_download)} API call(s)"
        if not yes:
            ans = input(f"\nDownload {len(to_download)} new CSV(s)? ({cost_note})  [y/N] ").strip()
            if ans.lower() not in ("y", "yes"):
                print("Aborted.")
                return

        with httpx.Client(
            headers=headers, timeout=httpx.Timeout(30, read=_DL_TIMEOUT)
        ) as client:
            for i, t in enumerate(to_download, 1):
                transid = t["transid"]
                obs = t.get("total", t.get("totalGps", "?"))
                print(
                    f"  [{i}/{len(to_download)}] Downloading {transid} (obs={obs})…",
                    end=" ",
                    flush=True,
                )
                dest = _csv_path(transid)
                try:
                    download_csv(client, transid, dest)
                    print(f"saved ({dest.stat().st_size // 1024} KB)")
                except httpx.HTTPStatusError as exc:
                    print(f"FAILED ({exc.response.status_code})")
                    dest.unlink(missing_ok=True)
                except httpx.ReadTimeout:
                    print("TIMEOUT — skipping")
                    dest.unlink(missing_ok=True)
                if i < len(to_download):
                    time.sleep(_DL_DELAY_S)

    # Run detection across all cached CSVs, tracking per-MAC run counts in one pass
    print("\nRunning detection across all cached runs…")
    seen: dict[str, Hit] = {}
    mac_runs: dict[str, int] = {}
    total_records = 0
    files_scanned = 0

    for t in transactions:
        csv_file = _csv_path(t["transid"])
        if not csv_file.exists():
            continue
        hits, n = run_detection(csv_file, min_confidence=min_confidence)
        total_records += n
        files_scanned += 1
        for h in hits:
            mac = h.mac.lower()
            mac_runs[mac] = mac_runs.get(mac, 0) + 1
            if mac not in seen:
                seen[mac] = h
            else:
                for s in h.signals:
                    seen[mac].add_signal(*s)

    all_hits = sorted(seen.values(), key=lambda h: (-h.confidence, h.mac))

    conf_labels = {3: "HIGH", 2: "MEDIUM", 1: "LOW"}
    conf_counts = {3: 0, 2: 0, 1: 0}
    for h in all_hits:
        conf_counts[h.confidence] += 1

    print(
        f"\n  {files_scanned} CSV(s) scanned  •  {total_records:,} records  •  "
        f"{len(all_hits)} unique Flock hit(s)  "
        f"(HIGH={conf_counts[3]} MED={conf_counts[2]} LOW={conf_counts[1]})"
    )

    if not all_hits:
        print("No Flock devices detected.")
        return

    # Print hit table
    print()
    col_w = (6, 19, 30, 5, 5, 4, 10, 10, 4)
    header = ("Conf", "MAC", "Name", "Type", "RSSI", "Ch", "Lat", "Lon", "Runs")
    fmt = "  ".join(f"{{:<{w}}}" for w in col_w)
    print(fmt.format(*header))
    print("  ".join("-" * w for w in col_w))

    for h in all_hits:
        ch = freq_to_channel(h.frequency)
        runs = mac_runs.get(h.mac.lower(), 1)
        print(
            fmt.format(
                conf_labels[h.confidence],
                h.mac,
                (h.ssid or "(hidden)")[:30],
                h.device_type,
                str(h.rssi),
                str(ch) if ch else "—",
                f"{h.lat:.4f}" if (h.lat or h.lon) else "—",
                f"{h.lon:.4f}" if (h.lat or h.lon) else "—",
                str(runs),
            )
        )

    # Optional export
    if out:
        out_path = Path(out)
        suf = out_path.suffix.lower()
        if suf == ".csv":
            export_csv(all_hits, out)
            print(f"\nExported {len(all_hits)} hit(s) → {out}")
        elif suf == ".kml":
            export_kml(all_hits, out)
            print(f"\nExported {len(all_hits)} placemark(s) → {out}")
        else:
            print(f"\nUnknown output format '{suf}' — use .csv or .kml")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="flockdar-runs",
        description="Download all WiGLE-uploaded runs and scan for Flock Safety devices.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="List transactions without downloading or scanning",
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="Skip download confirmation prompt",
    )
    parser.add_argument(
        "--out",
        metavar="FILE",
        help="Write hits to FILE (.csv or .kml)",
    )
    parser.add_argument(
        "--min-confidence",
        type=int,
        default=1,
        metavar="N",
        choices=(1, 2, 3),
        help="Minimum confidence (1=LOW, 2=MEDIUM, 3=HIGH) — default 1",
    )
    parser.add_argument(
        "--api-name",
        metavar="NAME",
        help="WiGLE API name (overrides config/env)",
    )
    parser.add_argument(
        "--api-token",
        metavar="TOKEN",
        help="WiGLE API token (overrides config/env)",
    )
    args = parser.parse_args(argv)

    name = args.api_name or os.environ.get("WIGLE_API_NAME", "")
    token = args.api_token or os.environ.get("WIGLE_API_TOKEN", "")
    if not name or not token:
        try:
            cfg = load_config()
            name = name or cfg.get("wigle_api_name", "")
            token = token or cfg.get("wigle_api_token", "")
        except Exception:
            pass

    if not name or not token:
        sys.exit(
            "WiGLE credentials not found.\n"
            "Set WIGLE_API_NAME + WIGLE_API_TOKEN, use --api-name/--api-token,\n"
            "or run `flockdar` and press [w] to save credentials."
        )

    sync_and_detect(
        name,
        token,
        dry_run=args.dry_run,
        yes=args.yes,
        out=args.out,
        min_confidence=args.min_confidence,
    )


if __name__ == "__main__":
    main()
