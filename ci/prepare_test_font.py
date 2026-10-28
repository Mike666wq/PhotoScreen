"""仅在 Actions 下载固定版本测试字体；不修改系统字体，不参与应用部署。"""

import hashlib
import json
import os
from pathlib import Path
import time
from urllib.request import urlopen


COMMIT = "a85815a42757630ce188fdad368c2dfc444d4773"
FILES = {
    "NotoSansSC%5Bwght%5D.ttf": ("NotoSansSC.ttf", "fb0637bafbcd804fe32152370a1225990745b4bc"),
    "OFL.txt": ("NotoSansSC-OFL.txt", "1c9f43281b8f216c5461fe9ac729afbade7724e4"),
}


def main():
    assets = Path("build/regression-font").resolve()
    reports = Path("build/reports").resolve()
    assets.mkdir(parents=True, exist_ok=True)
    reports.mkdir(parents=True, exist_ok=True)
    manifest = {"commit": COMMIT, "purpose": "仅供合成回归截图；不进入安装包", "files": []}
    for source, (name, expected_blob) in FILES.items():
        url = f"https://raw.githubusercontent.com/google/fonts/{COMMIT}/ofl/notosanssc/{source}"
        for attempt in range(3):
            try:
                with urlopen(url, timeout=45) as response:
                    data = response.read()
                break
            except OSError:
                if attempt == 2:
                    raise
                time.sleep(2)
        header = f"blob {len(data)}\0".encode("ascii")
        actual_blob = hashlib.sha1(header + data).hexdigest()
        if actual_blob != expected_blob:
            raise RuntimeError(f"测试字体校验失败：{name} {actual_blob}")
        (assets / name).write_bytes(data)
        if name.endswith(".txt"):
            (reports / name).write_bytes(data)
        manifest["files"].append({"name": name, "url": url, "git_blob": actual_blob,
                                  "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})
    (reports / "font-fixture.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as environment:
        environment.write(f"PHOTOSCREEN_TEST_FONT={(assets / 'NotoSansSC.ttf').as_posix()}\n")
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
