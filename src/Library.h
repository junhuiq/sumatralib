/* Copyright 2025 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct LibraryNode;

struct LibraryNode {
    // display name (file name for books, directory name for dirs)
    Str name;
    // full file path for book files, empty for directories
    Str filePath;
    // whether this node is expanded in the tree view
    bool isExpanded = true;
    // children of this node
    Vec<LibraryNode*> children;
    // parent node (nullptr for root)
    LibraryNode* parent = nullptr;

    bool IsBook() const {
        return filePath.len > 0 && filePath.s;
    }
    bool IsDir() const {
        return !IsBook();
    }
};

void CreateLibrary(MainWindow* win);
void ToggleLibraryBox(MainWindow* win);
void PopulateLibraryTree(MainWindow* win);
void SaveLibrary(MainWindow* win);
void DeleteLibraryNodeTree(LibraryNode* node);
