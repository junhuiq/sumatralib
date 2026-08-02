# SumatraLIB v1.0

A personal library and reader with AI assistance, built on the
[SumatraPDF](https://www.sumatrapdfreader.org/) engine.

## Download

**Portable 64-bit build (no installation required):**

<https://github.com/junhuiq/sumatralib/releases/download/v1.0/Sumatra.exe>

## Highlights

- **AI Workspace** — right-side chat panel for free-form AI conversations. Type a
  prompt, press `Shift+Enter` or click **Ask AI** to send, and the reply appears
  in the response area. Works with any OpenAI-compatible endpoint (OpenAI,
  DeepSeek, Ollama, etc.).
- **Explain selected text** — select text in a document and choose *Explain
  briefly* or *Explain in depth* from the popup menu.
- **Personal Library** — organize files and folders into categories in the
  sidebar for quick access.
- **AI Chat with document** — Claude, Grok, and Codex backends for asking
  questions about the open document.
- **Take notes** — save AI workspace content as Markdown notes next to your
  document.
- **i18n** — Chinese and English UI; AI prompts adapt to the interface language.

## Fixes

- `Ctrl+C` / `Ctrl+V` now work in the AI workspace input box and reply area.
- Opening EPUB files no longer pops up a spurious "Errors in PDF" notification
  (those are benign MuPDF warnings); the *Show Errors* menu entry still lists
  them.
- Tooltips and several hardcoded strings now follow the UI language.
