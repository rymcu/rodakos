# RodakOS handbook publishing

The public handbook is built from the numbered Markdown files in `articles/`. HTML generated
from those sources is not committed to `main`. The homepage and chapter paths match the
previous `gh-pages` site.

Use Python 3.12 to preview locally:

```shell
python -m pip install -r tools/pages/requirements.txt
python tools/pages/build.py --output _site
python -m http.server 8000 --directory _site
```

Use a new or empty output directory. Keep article numbers consecutive, and add every chapter
to the index using a relative Markdown link. The renderer checks chapter links and converts
the supported Mermaid flow notation into readable tables with the original source preserved.

Pull requests build and validate the site without publishing. On `main`, changes under
`articles/`, `tools/pages/`, or the Pages workflow build and deploy a fresh artifact. For
a manual rebuild, run **RodakOS documentation Pages** on `main`.

Repository setup: choose **GitHub Actions** in Settings → Pages and allow `main` to deploy to
the `github-pages` environment. The previous `gh-pages` branch can be retained as rollback.
`build-info.json` identifies the published source commit.
