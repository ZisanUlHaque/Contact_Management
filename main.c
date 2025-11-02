#define _WIN32_IE 0x0500  

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "resource.h"
#include "sqlite3.h"

#define DB_FILE "contacts.db"

HINSTANCE hInst;
sqlite3 *db;
HWND hListView = NULL;
HWND hSearchEdit = NULL;
HWND hStatusBar = NULL;
HWND hMainWnd = NULL;

// forward
ATOM MyRegisterClass(HINSTANCE);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AddDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK EditDlgProc(HWND, UINT, WPARAM, LPARAM);

// Safe error handler (no exit!)
void sql_error(const char *msg) {
    MessageBoxA(NULL, msg, "SQLite Error", MB_ICONERROR);
}

void InitDatabase() {
    int rc = sqlite3_open(DB_FILE, &db);
    if (rc != SQLITE_OK) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Cannot open database: %s", sqlite3_errmsg(db));
        sql_error(buf);
        sqlite3_close(db);
        db = NULL;
        return;
    }
    char *errmsg = 0;
    const char *sql =
      "CREATE TABLE IF NOT EXISTS contacts("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "name TEXT NOT NULL,"
      "phone TEXT,"
      "email TEXT"
      ");";
    rc = sqlite3_exec(db, sql, 0, 0, &errmsg);
    if (rc != SQLITE_OK) {
        sql_error(errmsg);
        sqlite3_free(errmsg);
    }
}

void AddContact(const char *name, const char *phone, const char *email) {
    if (!db) return;
    const char *sql = "INSERT INTO contacts(name,phone,email) VALUES(?,?,?);";
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to prepare statement: %s", sqlite3_errmsg(db));
        sql_error(error_msg);
        return;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, phone, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, email, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed to execute: %s", sqlite3_errmsg(db));
        sql_error(error_msg);
    }

    if (stmt) sqlite3_finalize(stmt);
}

void UpdateContact(int id,const char *name,const char *phone,const char *email) {
    if (!db) return;

    const char *sql = "UPDATE contacts SET name=?, phone=?, email=? WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,phone,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3,email,-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,4,id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sql_error(sqlite3_errmsg(db));
        }
    } else {
        sql_error(sqlite3_errmsg(db));
    }
    if (stmt) sqlite3_finalize(stmt);
}

void DeleteContact(int id) {
    if (!db) return;

    const char *sql = "DELETE FROM contacts WHERE id=?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt,1,id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sql_error(sqlite3_errmsg(db));
        }
    } else {
        sql_error(sqlite3_errmsg(db));
    }
    if (stmt) sqlite3_finalize(stmt);
}

HWND CreateListView(HWND parent) {
    RECT rc; GetClientRect(parent, &rc);
    HWND h = CreateWindowEx(0, WC_LISTVIEW, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        10, 40, rc.right - 20, rc.bottom - 80,
        parent, (HMENU)IDC_LISTVIEW, hInst, NULL);

    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMN col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.cx = 240; col.pszText = "Name"; ListView_InsertColumn(h, 0, &col);
    col.cx = 140; col.pszText = "Phone"; ListView_InsertColumn(h, 1, &col);
    col.cx = 300; col.pszText = "Email"; ListView_InsertColumn(h, 2, &col);

    return h;
}

// ✅ Modified version (adds “No contact found.” message)
void LoadContactsToListView(HWND hList, const char *filter) {
    if (!hList || !db) return;

    ListView_DeleteAllItems(hList);

    sqlite3_stmt *stmt = NULL;
    if (filter && strlen(filter) > 0) {
        const char *sql = "SELECT id,name,phone,email FROM contacts WHERE name LIKE ? OR phone LIKE ? OR email LIKE ? ORDER BY name;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        char pat[512]; snprintf(pat, sizeof(pat), "%%%s%%", filter);
        sqlite3_bind_text(stmt,1,pat,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,pat,-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3,pat,-1,SQLITE_TRANSIENT);
    } else {
        const char *sql = "SELECT id,name,phone,email FROM contacts ORDER BY name;";
        sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    }

    int row = 0;
    while (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        const unsigned char *phone = sqlite3_column_text(stmt, 2);
        const unsigned char *email = sqlite3_column_text(stmt, 3);

        LVITEM item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = (LPSTR)name;
        item.lParam = (LPARAM)id;
        ListView_InsertItem(hList, &item);

        ListView_SetItemText(hList, row, 1, (LPSTR)phone);
        ListView_SetItemText(hList, row, 2, (LPSTR)email);
        row++;
    }

    if (stmt) sqlite3_finalize(stmt);

    // ✅ Show popup if search returned no results
    if (row == 0 && filter && strlen(filter) > 0) {
        MessageBox(hMainWnd, "No contact found.", "Info", MB_OK | MB_ICONINFORMATION);
    }

    char status[64];
    snprintf(status, sizeof(status), "%d contacts", row);
    SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)status);
}

void CreateMainControls(HWND hWnd) {
    hSearchEdit = CreateWindowEx(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
        10, 6, 260, 24, hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);

    CreateWindowEx(0, "BUTTON", "Search", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280, 6, 80, 24, hWnd, (HMENU)IDC_SEARCH_BTN, hInst, NULL);

    hListView = CreateListView(hWnd);

    hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_STATUSBAR, hInst, NULL);
}

int GetSelectedContactId() {
    if (!hListView) return -1;
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBox(hMainWnd, "Please select a contact first.", "Info", MB_OK | MB_ICONINFORMATION);
        return -1;
    }
    LVITEM it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = sel;
    ListView_GetItem(hListView, &it);
    return (int)it.lParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance) {
    WNDCLASSEX wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
    wcex.lpszClassName = "ContactMgrClass";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    hInst = hInstance;
    hMainWnd = CreateWindow("ContactMgrClass", "Contact Management System",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 800, 480, NULL, NULL, hInstance, NULL);
    if (!hMainWnd) return FALSE;
    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);
    return TRUE;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCEL));

    InitDatabase();
    if (!db) {
        MessageBox(NULL, "Database initialization failed.", "Error", MB_ICONERROR);
        return 1;
    }

    MyRegisterClass(hInstance);
    if (!InitInstance(hInstance, nCmdShow)) return FALSE;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hMainWnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (db) sqlite3_close(db);
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateMainControls(hWnd);
        LoadContactsToListView(hListView, NULL);
        break;

    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        MoveWindow(hSearchEdit, 10, 6, 260, 24, TRUE);
        MoveWindow(GetDlgItem(hWnd, IDC_SEARCH_BTN), 280, 6, 80, 24, TRUE);
        MoveWindow(hListView, 10, 40, rc.right - 20, rc.bottom - 80, TRUE);
        SendMessage(hStatusBar, WM_SIZE, 0, 0);
    } break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_EXIT:
            PostQuitMessage(0);
            break;
        case IDM_CONTACT_ADD:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ADD_CONTACT), hWnd, AddDlgProc);
            LoadContactsToListView(hListView, NULL);
            break;
        case IDC_SEARCH_BTN: {
            char buf[256] = {0};
            GetWindowTextA(hSearchEdit, buf, sizeof(buf));
            LoadContactsToListView(hListView, buf);
        } break;
        case IDM_CONTACT_EDIT: {
            int id = GetSelectedContactId();
            if (id > 0) {
                DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_EDIT_CONTACT), hWnd, EditDlgProc, (LPARAM)id);
                LoadContactsToListView(hListView, NULL);
            }
        } break;
        case IDM_CONTACT_DEL: {
            int id = GetSelectedContactId();
            if (id > 0) {
                if (MessageBox(hWnd, "Delete this contact?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DeleteContact(id);
                    LoadContactsToListView(hListView, NULL);
                }
            }
        } break;
        }
        break;

    case WM_NOTIFY: {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (pnm->idFrom == IDC_LISTVIEW) {
            if (pnm->code == NM_DBLCLK) {
                int id = GetSelectedContactId();
                if (id > 0) {
                    DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_EDIT_CONTACT), hWnd, EditDlgProc, (LPARAM)id);
                    LoadContactsToListView(hListView, NULL);
                }
            } else if (pnm->code == NM_RCLICK) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenu(hMenu, MF_STRING, IDM_CONTACT_EDIT, "Edit");
                AppendMenu(hMenu, MF_STRING, IDM_CONTACT_DEL, "Delete");
                TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
        }
    } break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK AddDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_ADD_NAME));
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char name[256]={0}, phone[128]={0}, email[256]={0};
            GetDlgItemTextA(hDlg, IDC_ADD_NAME, name, sizeof(name));
            GetDlgItemTextA(hDlg, IDC_ADD_PHONE, phone, sizeof(phone));
            GetDlgItemTextA(hDlg, IDC_ADD_EMAIL, email, sizeof(email));
            if (strlen(name) == 0) {
                MessageBox(hDlg, "Name is required.", "Input Error", MB_ICONERROR);
                SetFocus(GetDlgItem(hDlg, IDC_ADD_NAME));
                return (INT_PTR)TRUE;
            }
            AddContact(name, phone, email);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK EditDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int contactId = -1;
    switch (message) {
    case WM_INITDIALOG: {
        contactId = (int)lParam;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)contactId);
        const char *sql = "SELECT name,phone,email FROM contacts WHERE id=?;";
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, contactId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char *name = sqlite3_column_text(stmt,0);
                const unsigned char *phone = sqlite3_column_text(stmt,1);
                const unsigned char *email = sqlite3_column_text(stmt,2);
                SetDlgItemTextA(hDlg, IDC_EDIT_NAME, (const char*)name ? (const char*)name : "");
                SetDlgItemTextA(hDlg, IDC_EDIT_PHONE, (const char*)phone ? (const char*)phone : "");
                SetDlgItemTextA(hDlg, IDC_EDIT_EMAIL, (const char*)email ? (const char*)email : "");
            }
            sqlite3_finalize(stmt);
        }
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            int id = (int)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            char name[256]={0}, phone[128]={0}, email[256]={0};
            GetDlgItemTextA(hDlg, IDC_EDIT_NAME, name, sizeof(name));
            GetDlgItemTextA(hDlg, IDC_EDIT_PHONE, phone, sizeof(phone));
            GetDlgItemTextA(hDlg, IDC_EDIT_EMAIL, email, sizeof(email));
            if (id > 0) UpdateContact(id, name, phone, email);
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDM_CONTACT_DEL) {
            int id = (int)GetWindowLongPtr(hDlg, GWLP_USERDATA);
            if (id > 0 && MessageBox(hDlg, "Delete this contact?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DeleteContact(id);
                EndDialog(hDlg, IDOK);
            }
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
