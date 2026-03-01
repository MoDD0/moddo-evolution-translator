# Changelog

## 1.2.0 — first release as Moddo Evolution Translator
- **Toolbar button**: Translate action now appears directly in the mail toolbar
  with a `translate-symbolic` icon, next to Reply/Forward buttons.
- **Custom keyboard shortcut**: Shortcut for the Translate action is now
  configurable via the Translate Settings dialog. Change takes effect after
  restarting Evolution.
- **Project renamed** to Moddo Evolution Translator — forked from
  [costantinoai/evolution-mail-translate](https://github.com/costantinoai/evolution-mail-translate)
  and maintained independently. The rename was prompted by the upstream project
  independently tagging their own v1.2.0 with different content, making a
  distinct identity necessary to avoid confusion.
- **Evolution ≥ 3.56 compatibility**: Full support for the new EUIManager /
  EUIAction API that replaced GtkUIManager in Evolution 3.56. Plugin now works
  on both legacy (< 3.56) and modern Evolution builds.
- **Bug fixes**: Resolved issues #4, #5, #6 and merged upstream PR #3
  (translation status messages, stale state handling, and other stability fixes).
- **Manjaro / Arch installation guide** added to README.
- **ArgosTranslate on Python 3.14**: Fixed crash caused by spacy/pydantic
  import error; uninstalling spacy from the venv resolves the issue.

## 1.0.0
- Initial public release of Evolution Translation Extension.
- ArgosTranslate offline translation with HTML preservation.
- Per-user Python environment via `evolution-translate-setup`.
- Debian packaging and build scripts.
