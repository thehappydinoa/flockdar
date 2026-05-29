"""flockdar — passive RF detection of Flock Safety ALPR cameras.

Public API:
    from flockdar import run_detection, analyze, Hit, Cluster
"""

from __future__ import annotations

from .detect import Cluster, Hit, analyze, run_detection

__version__ = "0.2.0"

__all__ = ["Cluster", "Hit", "analyze", "run_detection", "__version__"]
