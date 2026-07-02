/* Copyright 2025 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/File.h"
#include "base/Win.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/WinGui.h"

#include "Settings.h"
#include "AppSettings.h"
#include "GlobalPrefs.h"
#include "SumatraPDF.h"
#include "MainWindow.h"
#include "Commands.h"
#include "Translations.h"
#include "Menu.h"
#include "Theme.h"
#include "resource.h"
#include "Library.h"

static void LayoutLibraryContainer(MainWindow* win);

// ── LibraryTreeModel ────────────────────────────────────────────────

struct LibraryTreeModel : TreeModel {
    LibraryNode* root = nullptr;
    Vec<LibraryNode*>* allNodes = nullptr; // tracks all nodes for cleanup

    explicit LibraryTreeModel(LibraryNode* rootNode, Vec<LibraryNode*>* nodes) {
        root = rootNode;
        allNodes = nodes;
        allNodes->Append(root);
        // add all nodes to the tracking list
        for (auto* child : root->children) {
            AddRecursive(child);
        }
    }

    void AddRecursive(LibraryNode* node) {
        allNodes->Append(node);
        for (auto* child : node->children) {
            AddRecursive(child);
        }
    }

    ~LibraryTreeModel() override {
        for (auto* node : *allNodes) {
            str::Free(node->name);
            str::Free(node->filePath);
            delete node;
        }
        delete allNodes;
    }

    TreeItem Root() override { return (TreeItem)root; }

    Str Text(TreeItem ti) override {
        auto* node = (LibraryNode*)ti;
        return node->name;
    }

    TreeItem Parent(TreeItem ti) override {
        auto* node = (LibraryNode*)ti;
        if (node->parent) {
            return (TreeItem)node->parent;
        }
        return kNullItem;
    }

    int ChildCount(TreeItem ti) override {
        auto* node = (LibraryNode*)ti;
        return node->children.Size();
    }

    TreeItem ChildAt(TreeItem ti, int index) override {
        auto* node = (LibraryNode*)ti;
        if (index >= 0 && index < node->children.Size()) {
            return (TreeItem)node->children[index];
        }
        return kNullItem;
    }

    bool IsExpanded(TreeItem ti) override {
        auto* node = (LibraryNode*)ti;
        return node->isExpanded;
    }

    bool IsChecked(TreeItem) override { return false; }

    void SetHandle(TreeItem, HTREEITEM) override {
        // not needed for library tree
    }

    HTREEITEM GetHandle(TreeItem) override { return nullptr; }
};

// ── Tree building / flattening ─────────────────────────────────────

static LibraryNode* BuildLibraryTreeFromEntries(Vec<LibraryEntry*>* entries) {
    auto* root = new LibraryNode();
    root->name = str::Dup(_TRA("My Library"));
    root->isExpanded = true;

    if (!entries || entries->Size() == 0) {
        return root;
    }

    // First pass: create all nodes
    Vec<LibraryNode*> allNodes;
    for (auto& entry : *entries) {
        auto* node = new LibraryNode();
        node->name = str::Dup(entry->name);
        if (entry->path.len > 0) {
            node->filePath = str::Dup(entry->path);
        }
        node->isExpanded = entry->isExpanded;
        allNodes.Append(node);
    }

    // Second pass: link parents using parentIndex
    for (int i = 0; i < allNodes.Size(); i++) {
        auto* entry = entries->at(i);
        int parentIdx = entry->parentIndex;
        if (parentIdx < 0 || parentIdx >= allNodes.Size()) {
            root->children.Append(allNodes[i]);
            allNodes[i]->parent = root;
        } else {
            auto* parent = allNodes[parentIdx];
            parent->children.Append(allNodes[i]);
            allNodes[i]->parent = parent;
        }
    }

    return root;
}

static void FlattenLibraryNode(LibraryNode* node, Vec<LibraryEntry*>* entries, int parentIndex) {
    for (auto* child : node->children) {
        int myIndex = entries->Size();

        auto* entry = new LibraryEntry();
        entry->name = str::Dup(child->name ? child->name : StrL(""));
        entry->path = str::Dup(child->filePath ? child->filePath : StrL(""));
        entry->parentIndex = parentIndex;
        entry->isExpanded = child->isExpanded;
        entries->Append(entry);

        if (child->IsDir()) {
            FlattenLibraryNode(child, entries, myIndex);
        }
    }
}

void DeleteLibraryNodeTree(LibraryNode* root) {
    if (!root) {
        return;
    }
    for (auto* child : root->children) {
        DeleteLibraryNodeTree(child);
    }
    str::Free(root->name);
    str::Free(root->filePath);
    delete root;
}

// ── Persistence ────────────────────────────────────────────────────

void SaveLibrary(MainWindow* win) {
    if (!gGlobalPrefs->library) {
        gGlobalPrefs->library = new Vec<LibraryEntry*>();
    }
    DeleteVecMembers(*gGlobalPrefs->library);
    gGlobalPrefs->library->Reset();

    auto* model = (LibraryTreeModel*)win->libraryTreeView->treeModel;
    if (!model || !model->root) {
        return;
    }

    FlattenLibraryNode(model->root, gGlobalPrefs->library, -1);
}

void PopulateLibraryTree(MainWindow* win) {
    // delete old model (TreeView doesn't own it, we do)
    auto* oldModel = (LibraryTreeModel*)win->libraryTreeView->treeModel;
    if (oldModel) {
        // The model destructor will clean up the old tree nodes
        // SetTreeModel will set a new model
    }

    Vec<LibraryEntry*>* entries = gGlobalPrefs->library;
    LibraryNode* root = BuildLibraryTreeFromEntries(entries);

    auto* nodes = new Vec<LibraryNode*>();
    auto* model = new LibraryTreeModel(root, nodes);
    win->libraryTreeView->SetTreeModel(model);

    // Safe to delete old model after setting new one
    delete oldModel;
}

// ── Panel creation ─────────────────────────────────────────────────

static void LayoutLibraryContainer(MainWindow* win) {
    if (!win->libraryLayout) {
        return;
    }
    Rect rc = ClientRect(win->hwndLibraryBox);
    if (rc.IsEmpty()) {
        return;
    }
    win->libraryLayout->Layout(Tight(Size{rc.dx, rc.dy}));
    win->libraryLayout->SetBounds(Rect{0, 0, rc.dx, rc.dy});
}

// ── Drag & drop support ────────────────────────────────────────────

struct LibraryDragState {
    bool dragging = false;
    TreeView* treeView = nullptr;
    LibraryNode* draggedNode = nullptr;
    HIMAGELIST dragImage = nullptr;
    HTREEITEM hPrevDropTarget = nullptr;
    POINT startPt{}; // drag start point (client coords of libraryBox)
    bool pendingDrag = false;
};

static LibraryDragState gLibraryDrag;

static void LibraryEndDrag(bool cancelled) {
    if (gLibraryDrag.dragImage) {
        ImageList_Destroy(gLibraryDrag.dragImage);
        gLibraryDrag.dragImage = nullptr;
    }
    if (gLibraryDrag.treeView) {
        TreeView_SelectDropTarget(gLibraryDrag.treeView->hwnd, nullptr);
    }
    gLibraryDrag = {};
}

static void LibraryBeginDrag(MainWindow* win, HWND hwndLibraryBox, HTREEITEM hItem, POINT ptScreen) {
    HWND hTree = win->libraryTreeView->hwnd;
    TreeItem ti = win->libraryTreeView->GetTreeItemByHandle(hItem);
    LibraryNode* node = (LibraryNode*)ti;
    if (!node || !node->parent) {
        return; // can't drag root
    }

    HIMAGELIST himl = TreeView_CreateDragImage(hTree, hItem);
    if (!himl) {
        return;
    }

    ImageList_BeginDrag(himl, 0, 0, 0);
    ImageList_DragEnter(nullptr, ptScreen.x, ptScreen.y);

    gLibraryDrag.dragging = true;
    gLibraryDrag.treeView = win->libraryTreeView;
    gLibraryDrag.draggedNode = node;
    gLibraryDrag.dragImage = himl;
    gLibraryDrag.hPrevDropTarget = nullptr;

    SetCapture(hwndLibraryBox);
}

static void LibraryMoveNodeToNewParent(MainWindow* win, LibraryNode* node, LibraryNode* newParent) {
    if (!node || !newParent || node == newParent) {
        return;
    }
    // don't allow moving a node into its own descendant
    for (LibraryNode* p = newParent; p; p = p->parent) {
        if (p == node) {
            return;
        }
    }
    // remove from old parent
    if (node->parent) {
        auto& siblings = node->parent->children;
        for (int i = 0; i < siblings.Size(); i++) {
            if (siblings[i] == node) {
                siblings.RemoveAt(i);
                break;
            }
        }
    }
    // add to new parent
    node->parent = newParent;
    newParent->children.Append(node);

    SaveLibrary(win);
    PopulateLibraryTree(win);
    newParent->isExpanded = true;
}

static LRESULT CALLBACK WndProcLibraryBox(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId,
                                          DWORD_PTR data) {
    MainWindow* win = FindMainWindowByHwnd(hwnd);
    if (!win) {
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    // ── drag-and-drop mouse handling ──
    if (gLibraryDrag.dragging) {
        if (msg == WM_MOUSEMOVE) {
            HWND hTree = gLibraryDrag.treeView->hwnd;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &pt);
            ImageList_DragMove(pt.x, pt.y);
            ScreenToClient(hTree, &pt);
            TVHITTESTINFO ht{pt};
            TreeView_HitTest(hTree, &ht);
            HTREEITEM hTarget = nullptr;
            if (ht.flags & (TVHT_ONITEM | TVHT_ONITEMLABEL | TVHT_ONITEMBUTTON)) {
                hTarget = ht.hItem;
            }
            if (hTarget != gLibraryDrag.hPrevDropTarget) {
                TreeView_SelectDropTarget(hTree, hTarget);
                gLibraryDrag.hPrevDropTarget = hTarget;
            }
            return 0;
        }
        if (msg == WM_LBUTTONUP) {
            HWND hTree = gLibraryDrag.treeView->hwnd;
            ImageList_DragLeave(hTree);
            ImageList_EndDrag();
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            TVHITTESTINFO ht{pt};
            TreeView_HitTest(hTree, &ht);
            LibraryNode* newParent = nullptr;
            if (ht.flags & (TVHT_ONITEM | TVHT_ONITEMLABEL | TVHT_ONITEMBUTTON)) {
                TreeItem ti = win->libraryTreeView->GetTreeItemByHandle(ht.hItem);
                LibraryNode* target = (LibraryNode*)ti;
                if (target) {
                    newParent = target->IsBook() ? target->parent : target;
                }
            }
            if (newParent && gLibraryDrag.draggedNode) {
                LibraryMoveNodeToNewParent(win, gLibraryDrag.draggedNode, newParent);
            }
            LibraryEndDrag(false);
            ReleaseCapture();
            return 0;
        }
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            ImageList_DragLeave(gLibraryDrag.treeView->hwnd);
            ImageList_EndDrag();
            LibraryEndDrag(true);
            ReleaseCapture();
            return 0;
        }
    }

    LRESULT res = TryReflectMessages(hwnd, msg, wp, lp);
    if (res) {
        return res;
    }

    switch (msg) {
        case WM_SIZE:
            LayoutLibraryContainer(win);
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_LIBRARY_LABEL_WITH_CLOSE) {
                ToggleLibraryBox(win);
            }
            break;
        case WM_NOTIFY: {
            NMHDR* nmhdr = (NMHDR*)lp;
            if (nmhdr->code == TVN_BEGINDRAG) {
                NMTREEVIEWW* nmtv = (NMTREEVIEWW*)lp;
                POINT pt = {nmtv->ptDrag.x, nmtv->ptDrag.y};
                ClientToScreen(nmhdr->hwndFrom, &pt);
                LibraryBeginDrag(win, hwnd, nmtv->itemNew.hItem, pt);
                if (gLibraryDrag.dragging) {
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void SubclassLibrary(MainWindow* win) {
    win->libraryBoxSubclassId = NextSubclassId();
    SetWindowSubclass(win->hwndLibraryBox, WndProcLibraryBox, win->libraryBoxSubclassId, 0);
}

// ── Event handlers ─────────────────────────────────────────────────

static void LibraryAddFiles(MainWindow* win, LibraryNode* target);

// Remove a node (and its subtree) from the library.
// Only removes from the library tree, does NOT delete files from disk.
static void RemoveLibraryNode(MainWindow* win, LibraryNode* node) {
    if (!node || !node->parent) {
        return;
    }

    // confirm with user before removing
    Str msg = node->IsBook()
                  ? _TRA("Remove this book from the library?\n\nThe file will NOT be deleted from your computer.")
                  : _TRA(
                        "Remove this folder and its contents from the library?\n\nThe files will NOT be deleted from "
                        "your computer.");
    int res = MsgBox(win->hwndFrame, msg, _TRA("Remove from Library"), MB_YESNO | MB_ICONQUESTION);
    if (res != IDYES) {
        return;
    }

    // remove node from parent's children list
    auto& siblings = node->parent->children;
    for (int i = 0; i < siblings.Size(); i++) {
        if (siblings[i] == node) {
            siblings.RemoveAt(i);
            break;
        }
    }
    // Note: we do NOT call DeleteLibraryNodeTree(node) here.
    // The old tree model owns all nodes and will clean them up
    // when PopulateLibraryTree replaces it with a new model.

    SaveLibrary(win);
    PopulateLibraryTree(win);
}

static void LibraryAddFolder(MainWindow* win, LibraryNode* target) {
    BROWSEINFOW bi{};
    bi.hwndOwner = win->hwndFrame;
    bi.lpszTitle = L"Select Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return;
    }
    WCHAR pathW[MAX_PATH];
    SHGetPathFromIDListW(pidl, pathW);
    CoTaskMemFree(pidl);

    Str pathUtf8 = ToUtf8Temp(pathW);
    auto* dirNode = new LibraryNode();
    dirNode->name = str::Dup(path::GetBaseNameTemp(pathUtf8));
    dirNode->isExpanded = true;
    dirNode->parent = target;
    target->children.Append(dirNode);

    // enumerate files and subdirectories at one level
    TempWStr patternW = ToWStrTemp(str::JoinTemp(pathUtf8, "\\*"));
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(patternW, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            WStr fileNameW = fd.cFileName;
            if (wstr::Eq(fileNameW, L".") || wstr::Eq(fileNameW, L"..")) {
                continue;
            }
            Str childUtf8 = ToUtf8Temp(fileNameW);
            TempStr childFullPath = str::JoinTemp(pathUtf8, "\\", childUtf8);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                auto* subDir = new LibraryNode();
                subDir->name = str::Dup(childUtf8);
                subDir->isExpanded = true;
                subDir->parent = dirNode;
                dirNode->children.Append(subDir);

                TempStr subPattern = str::JoinTemp(childFullPath, "\\*");
                TempWStr subPatternW = ToWStrTemp(subPattern);
                WIN32_FIND_DATAW fd2;
                HANDLE hFind2 = FindFirstFileW(subPatternW, &fd2);
                if (hFind2 != INVALID_HANDLE_VALUE) {
                    do {
                        WStr subFileNameW = fd2.cFileName;
                        if (wstr::Eq(subFileNameW, L".") || wstr::Eq(subFileNameW, L"..")) {
                            continue;
                        }
                        if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            Str subFileUtf8 = ToUtf8Temp(subFileNameW);
                            auto* bookNode = new LibraryNode();
                            bookNode->name = str::Dup(subFileUtf8);
                            bookNode->filePath = str::Dup(str::JoinTemp(childFullPath, "\\", subFileUtf8));
                            bookNode->parent = subDir;
                            subDir->children.Append(bookNode);
                        }
                    } while (FindNextFileW(hFind2, &fd2));
                    FindClose(hFind2);
                }
            } else {
                auto* bookNode = new LibraryNode();
                bookNode->name = str::Dup(childUtf8);
                bookNode->filePath = str::Dup(childFullPath);
                bookNode->parent = dirNode;
                dirNode->children.Append(bookNode);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    SaveLibrary(win);
    PopulateLibraryTree(win);
}

static void LibraryAddSubdir(MainWindow* win, LibraryNode* target) {
    auto* subDir = new LibraryNode();
    subDir->name = str::Dup(_TRA("New Folder"));
    subDir->isExpanded = true;
    subDir->parent = target;
    target->children.Append(subDir);
    SaveLibrary(win);
    PopulateLibraryTree(win);
}

static void OnLibraryAddButtonClick(MainWindow* win) {
    POINT pt{};
    GetCursorPos(&pt);

    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, 1, L"Add Files...");
    AppendMenuW(popup, MF_STRING, 2, L"Add Folder...");
    AppendMenuW(popup, MF_STRING, 3, L"Add Subdirectory");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, 4, L"Remove Selected");

    int cmd = (int)TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, win->hwndFrame, nullptr);
    DestroyMenu(popup);

    // Determine target node: selected item, or root
    TreeItem sel = win->libraryTreeView->GetSelection();
    auto* model = (LibraryTreeModel*)win->libraryTreeView->treeModel;
    LibraryNode* target = (LibraryNode*)sel;
    if (!target) {
        target = model ? model->root : nullptr;
    }
    if (!target) {
        return;
    }
    // If target is a book, add to its parent
    if (target->IsBook()) {
        target = target->parent;
        if (!target) {
            return;
        }
    }

    switch (cmd) {
        case 1:
            LibraryAddFiles(win, target);
            break;
        case 2:
            LibraryAddFolder(win, target);
            break;
        case 3:
            LibraryAddSubdir(win, target);
            break;
        case 4:
            // Remove selected
            if (sel && sel != TreeModel::kNullItem) {
                RemoveLibraryNode(win, (LibraryNode*)sel);
            }
            break;
    }
}

static void LibraryAddFiles(MainWindow* win, LibraryNode* target) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = win->hwndFrame;
    // Use a broad filter for all supported documents
    ofn.lpstrFilter = L"Documents\0*.pdf;*.xps;*.epub;*.mobi;*.cbz;*.cbr;*.djvu;*.chm\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    // Allocate a large buffer for multi-select
    int bufSize = MAX_PATH * 100;
    AutoFreeWStr fileBuf = AllocArray<WCHAR>(bufSize);
    fileBuf[0] = 0;
    ofn.nMaxFile = bufSize;
    ofn.lpstrFile = fileBuf;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    // Parse the multi-select result
    // The buffer contains: directory path \0 file1 file2 ... \0\0
    const WCHAR* dirPath = fileBuf.Get();
    int dirLen = (int)wcslen(dirPath);
    const WCHAR* filePtr = dirPath + dirLen + 1;

    if (*filePtr == 0) {
        // Single file selected: the whole buffer is the full path
        Str utf8Path = ToUtf8Temp(dirPath);
        auto* node = new LibraryNode();
        node->name = str::Dup(path::GetBaseNameTemp(utf8Path));
        node->filePath = str::Dup(utf8Path);
        node->parent = target;
        target->children.Append(node);
    } else {
        // Multiple files selected: dir is the directory, filePtr lists file names
        Str dirUtf8 = ToUtf8Temp(dirPath);
        while (*filePtr) {
            Str fileNameUtf8 = ToUtf8Temp(filePtr);
            TempStr fullPath = str::JoinTemp(dirUtf8, "\\", fileNameUtf8);
            auto* node = new LibraryNode();
            node->name = str::Dup(fileNameUtf8);
            node->filePath = str::Dup(fullPath);
            node->parent = target;
            target->children.Append(node);
            filePtr += wcslen(filePtr) + 1;
        }
    }

    SaveLibrary(win);
    PopulateLibraryTree(win);
}

static void LibraryTreeItemClicked(TreeView::ClickEvent* ev) {
    if (!ev->treeItem || ev->treeItem == TreeModel::kNullItem) {
        return;
    }
    auto* node = (LibraryNode*)ev->treeItem;

    if (!node->IsBook()) {
        // only toggle on label clicks — let the tree view handle button (+/-) clicks natively
        TVHITTESTINFO ht{};
        ht.pt.x = ev->mouseWindow.x;
        ht.pt.y = ev->mouseWindow.y;
        TreeView_HitTest(ev->treeView->hwnd, &ht);
        if (ht.flags & TVHT_ONITEMBUTTON) {
            // native +/- button click — just sync our isExpanded state
            TVITEMW item{};
            item.hItem = ht.hItem;
            item.mask = TVIF_STATE;
            TreeView_GetItem(ev->treeView->hwnd, &item);
            node->isExpanded = (item.state & TVIS_EXPANDED) != 0;
            return;
        }

        // toggle expand/collapse on the hit-tested item (not the selected one)
        if (ht.hItem) {
            node->isExpanded = !node->isExpanded;
            uint flag = node->isExpanded ? TVE_EXPAND : TVE_COLLAPSE;
            PostMessage(ev->treeView->hwnd, TVM_EXPAND, flag, (LPARAM)ht.hItem);
        }
        return;
    }

    MainWindow* win = FindMainWindowByHwnd(ev->treeView->hwnd);
    if (!win) {
        return;
    }

    LoadArgs args(node->filePath, win);
    args.activateExisting = true;
    LoadDocument(&args);
}

static void LibraryTreeKeyDown(TreeView::KeyDownEvent* ev) {
    if (ev->keyCode == VK_RETURN) {
        TreeItem sel = ev->treeView->GetSelection();
        if (sel && sel != TreeModel::kNullItem) {
            auto* node = (LibraryNode*)sel;
            if (node->IsBook()) {
                MainWindow* win = FindMainWindowByHwnd(ev->treeView->hwnd);
                if (win) {
                    LoadArgs args(node->filePath, win);
                    args.activateExisting = true;
                    LoadDocument(&args);
                    ev->result = 1;
                }
            }
        }
    }
    if (ev->keyCode == VK_DELETE) {
        TreeItem sel = ev->treeView->GetSelection();
        if (sel && sel != TreeModel::kNullItem) {
            auto* node = (LibraryNode*)sel;
            MainWindow* win = FindMainWindowByHwnd(ev->treeView->hwnd);
            if (win) {
                RemoveLibraryNode(win, node);
                ev->result = 1;
            }
        }
    }
}

static void LibraryTreeContextMenu(ContextMenuEvent* ev) {
    MainWindow* win = FindMainWindowByHwnd(ev->w->hwnd);
    if (!win) {
        return;
    }

    POINT pt{};
    TreeItem ti = GetOrSelectTreeItemAtPos(ev, pt);
    if (!ti) {
        return;
    }
    LibraryNode* target = (LibraryNode*)ti;

    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, 1, L"Add Files...");
    AppendMenuW(popup, MF_STRING, 2, L"Add Folder...");
    AppendMenuW(popup, MF_STRING, 3, L"Add Subdirectory");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, 4, L"Remove");
    if (target->IsBook()) {
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_STRING, 5, L"Open");
    }

    int cmd = (int)TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, win->hwndFrame, nullptr);
    DestroyMenu(popup);

    // for add operations, if target is a book, add to its parent
    LibraryNode* addTarget = target;
    if (addTarget->IsBook()) {
        addTarget = addTarget->parent;
        if (!addTarget) {
            return;
        }
    }

    switch (cmd) {
        case 1:
            LibraryAddFiles(win, addTarget);
            break;
        case 2:
            LibraryAddFolder(win, addTarget);
            break;
        case 3:
            LibraryAddSubdir(win, addTarget);
            break;
        case 4:
            // remove selected item directly with confirmation
            RemoveLibraryNode(win, target);
            break;
        case 5:
            if (target->IsBook()) {
                LoadArgs args(target->filePath, win);
                args.activateExisting = true;
                LoadDocument(&args);
            }
            break;
    }
}

// ── Create / destroy ──────────────────────────────────────────────

void CreateLibrary(MainWindow* win) {
    HMODULE hmod = GetModuleHandle(nullptr);
    int dx = gGlobalPrefs->libraryDx;
    if (dx <= 0) {
        dx = 200;
    }
    DWORD style = WS_CHILD | WS_CLIPCHILDREN;
    HWND parent = win->hwndFrame;
    win->hwndLibraryBox = CreateWindowExW(0, WC_STATIC, L"", style, 0, 0, dx, 0, parent, nullptr, hmod, nullptr);

    // Header: [My Library label (flex=1)]  [+]  [X]
    auto* labelWnd = new Static();
    {
        Static::CreateArgs args;
        args.parent = win->hwndLibraryBox;
        args.font = GetAppSidebarLabelFont();
        args.isRtl = IsUIRtl();
        labelWnd->Create(args);
    }
    win->libraryLabel = labelWnd;
    HwndSetText(labelWnd->hwnd, _TRA("My Library"));

    // Add "+" button (flat, no border, with tooltip)
    auto* addButton = new Button();
    {
        Button::CreateArgs args;
        args.parent = win->hwndLibraryBox;
        args.font = GetAppFont();
        args.text = StrL("+");
        addButton->Create(args);
    }
    // make the button flat (no visible border until hovered)
    {
        LONG_PTR btnStyle = GetWindowLongPtr(addButton->hwnd, GWL_STYLE);
        btnStyle |= BS_FLAT;
        SetWindowLongPtr(addButton->hwnd, GWL_STYLE, btnStyle);
    }
    addButton->onClick = MkFunc0(OnLibraryAddButtonClick, win);
    win->libraryAddButton = addButton;

    // Add tooltip to the "+" button
    {
        HWND tooltipHwnd = CreateWindowExW(
            WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, addButton->hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        if (tooltipHwnd) {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = win->hwndLibraryBox;
            ti.uId = (UINT_PTR)addButton->hwnd;
            TempWStr tip = ToWStrTemp(_TRA("Add files or directories"));
            ti.lpszText = tip.s;
            SendMessageW(tooltipHwnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(tooltipHwnd, TTM_SETMAXTIPWIDTH, 0, 300);
            win->hwndLibraryAddTooltip = tooltipHwnd;
        }
    }

    // "X" close button (flat)
    auto* closeButton = new Button();
    {
        Button::CreateArgs args;
        args.parent = win->hwndLibraryBox;
        args.font = GetAppFont();
        args.text = StrL("X");
        closeButton->Create(args);
    }
    {
        LONG_PTR btnStyle = GetWindowLongPtr(closeButton->hwnd, GWL_STYLE);
        btnStyle |= BS_FLAT;
        SetWindowLongPtr(closeButton->hwnd, GWL_STYLE, btnStyle);
    }
    closeButton->onClick = MkFunc0(ToggleLibraryBox, win);

    // Header HBox: label (flex=1) + button + close
    auto* headerBox = new HBox();
    headerBox->alignMain = MainAxisAlign::MainStart;
    headerBox->alignCross = CrossAxisAlign::Stretch;
    headerBox->AddChild(labelWnd, 1);
    headerBox->AddChild(addButton);
    headerBox->AddChild(closeButton);

    // Tree view
    auto* treeView = new TreeView();
    {
        TreeView::CreateArgs args;
        args.parent = win->hwndLibraryBox;
        args.font = GetAppTreeFont();
        args.fullRowSelect = true;
        args.exStyle = 0;
        args.isRtl = IsUIRtl();
        treeView->Create(args);
    }
    treeView->onContextMenu = MkFunc1Void(LibraryTreeContextMenu);
    treeView->onClick = MkFunc1Void(LibraryTreeItemClicked);
    treeView->onKeyDown = MkFunc1Void(LibraryTreeKeyDown);
    win->libraryTreeView = treeView;

    // VBox: header HBox + TreeView (flex=1)
    auto* vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;
    vbox->AddChild(headerBox);
    vbox->AddChild(treeView, 1);
    win->libraryLayout = vbox;

    SubclassLibrary(win);

    // Populate the tree from saved settings
    PopulateLibraryTree(win);
}

void ToggleLibraryBox(MainWindow* win) {
    SetSidebarVisibility(win, win->tocVisible, gGlobalPrefs->showFavorites, !win->libraryVisible);
    if (win->libraryVisible) {
        HwndSetFocus(win->libraryTreeView->hwnd);
    }
}
