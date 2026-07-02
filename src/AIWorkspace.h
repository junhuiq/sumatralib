/* Copyright 2025 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

void CreateAIWorkspacePanel(MainWindow* win);
void ToggleAIWorkspace(MainWindow* win);
void RelayoutAIWorkspace(MainWindow* win);
void AICallSendPrompt(MainWindow* win);
void ExplainSelectedText(MainWindow* win, Str selectedText, bool inDepth);
