"""Select a preserved engine patchset and its project-local Windows build paths.

``engine-patches/current`` may be a directory or a symlink within engine-patches.
Without it the original flat patch series remains active. An optional active
``windows-support.json`` supplies ``support`` and ``prefix`` as project-relative
POSIX paths; unrelated fetch metadata in that object is left to the fetcher.
"""
import json
from pathlib import Path, PureWindowsPath


def _inside(path, root, label):
    try:
        resolved = path.resolve()
        resolved.relative_to(root)
    except (OSError, RuntimeError, ValueError) as error:
        raise ValueError(f"{label} must resolve inside {root}: {path}") from error
    return resolved


def active_patchset(root):
    """Return the active patch directory, rejecting broken or escaping selectors."""
    root = Path(root).resolve()
    patches = _inside(root / "engine-patches", root, "engine-patches")
    if not patches.is_dir():
        raise ValueError(f"Engine patch directory is missing: {patches}")
    current = patches / "current"
    if not current.exists() and not current.is_symlink():
        return patches
    selected = _inside(current, patches, "engine-patches/current")
    if not selected.is_dir():
        raise ValueError(f"engine-patches/current must select an existing directory: {current}")
    return selected


def _build_path(root, value, label):
    if not isinstance(value, str) or not value.strip() or "\x00" in value:
        raise ValueError(f"{label} must be a nonempty project-relative path")
    # Config uses POSIX separators even when it is created or consumed on Windows.
    # Reject drive-relative C:foo and UNC/rooted Windows paths on Linux as well.
    if Path(value).is_absolute() or PureWindowsPath(value).drive or "\\" in value:
        raise ValueError(f"{label} must be a project-relative POSIX path: {value!r}")
    return _inside(root / value, root, label)


def windows_build_paths(root):
    """Return (support, prefix), resolved beneath root; preserve legacy defaults."""
    root = Path(root).resolve()
    selected = active_patchset(root)
    config_path = selected / "windows-support.json"
    config = {}
    if config_path.exists() or config_path.is_symlink():
        config_path = _inside(config_path, root, "windows-support.json")
        try:
            config = json.loads(config_path.read_text())
        except (OSError, ValueError) as error:
            raise ValueError(f"Cannot read Windows build configuration: {config_path}") from error
        if not isinstance(config, dict):
            raise ValueError(f"Windows build configuration must be a JSON object: {config_path}")
    support = _build_path(root, config.get("support", ".deps/windows-upstream/support"), "Windows support")
    prefix = _build_path(root, config.get("prefix", ".deps/windows-runtime"), "Windows prefix")
    return support, prefix
