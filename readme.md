# SumatraLIB

A personal library and reader with AI assistance.

SumatraLIB is built on the [SumatraPDF](https://www.sumatrapdfreader.org/) engine and extends it with library management and AI-powered reading features.

## Download

**Latest release: [v1.0](https://github.com/junhuiq/sumatralib/releases/tag/v1.0)**

- [Sumatra.exe](https://github.com/junhuiq/sumatralib/releases/download/v1.0/Sumatra.exe) — portable 64-bit build, no installation required. Download and run directly.

## Features

### Personal Library
- **Organize your books**: Add files and folders to your personal library for quick access
- **Hierarchical organization**: Group books into categories and subcategories
- **Fast navigation**: Browse and open books directly from the library sidebar

### AI Assistance
- **Highlight to explain**: Select any text in a document, release the mouse, and choose *Explain briefly* or *Explain in depth* — the AI will analyze and respond
- **AI workspace panel**: A dedicated right-side panel for free-form AI chat. Type your prompt and press **Shift+Enter** (or click *Ask AI*) to send
- **Multi-model support**: Configure any OpenAI-compatible API endpoint (OpenAI, DeepSeek, local models via Ollama, etc.)
- **AI-aware UI**: The interface language switches seamlessly — switch to Chinese and prompts, menus, and AI replies adapt automatically

### Reader
- **All formats**: PDF, EPUB, MOBI, CHM, XPS, DjVu, CBZ/CBR, and images
- **Fast and lightweight**: Built on the battle-tested SumatraPDF rendering engine
- **Tabs and sessions**: Multi-tab reading with session restore
- **Customizable**: Themes, display modes, zoom levels, and keyboard shortcuts

### Privacy
- **Runs locally**: Your documents stay on your computer
- **Portable mode**: Run from a USB drive — no installation required, settings stored alongside the executable

## Getting Started

### Configuration
1. Launch SumatraLIB
2. Go to **Settings → AI Model Settings...**
3. Enter your API endpoint URL, API key, and model name
4. Click **Test** to verify the connection

### Using the AI Workspace
1. Open the AI workspace from **View → AI Workspace**
2. Type a prompt and press **Shift+Enter** or click **Ask AI**
3. The AI reply appears in the response area below

### Explain Selected Text
1. Open any document
2. Select text with your mouse
3. Choose *Explain briefly* or *Explain in depth* from the popup menu

## License

GPLv3 — see the [AUTHORS](AUTHORS) file.
