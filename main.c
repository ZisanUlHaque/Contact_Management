#define _WIN32_IE 0x0500
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <commctrl.h> 
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "resource.h"
#include "sqlite3.h" 

#pragma comment(lib, "comctl32.lib")

#define DB_FILE "contacts.db"

HINSTANCE hInst;
sqlite3 *db;
HWND hListView = NULL;
HWND hSearchEdit = NULL;
HWND hStatusBar = NULL;
HWND hMainWnd = NULL;

const char *SEARCH_PLACEHOLDER = "Enter name, phone or email to search...";

// forward declarations
ATOM MyRegisterClass(HINSTANCE);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK AddDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK EditDlgProc(HWND, UINT, WPARAM, LPARAM);

// helper: show SQLite error (CORRECTED: Now passes only 4 arguments)
void sql_error(const char *msg) {
    MessageBoxA(NULL, msg, "SQLite Error", MB_ICONERROR);
}

// helper: validation functions
BOOL IsNameValid(const char *name) {
    if (strlen(name) == 0) return FALSE;
    for (int i = 0; name[i]; i++) {
        if (!isalpha((unsigned char)name[i]) && !isspace((unsigned char)name[i])) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL IsPhoneValid(const char *phone) {
    if (strlen(phone) == 0) return TRUE;
    for (int i = 0; phone[i]; i++) {
        if (!isdigit((unsigned char)phone[i])) return FALSE;
    }
    return TRUE;
}

BOOL IsEmailValid(const char *email) {
    if (strlen(email) == 0) return TRUE;
    if (strchr(email, '@') == NULL) return FALSE;
    for (int i = 0; email[i]; i++) {
        if (isspace((unsigned char)email[i]) || email[i] == ',') return FALSE;
    }
    return TRUE;
}

// --- Database Functions ---

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

// --- UI & Control Functions ---

HWND CreateListView(HWND parent) {
    RECT rc; GetClientRect(parent, &rc);
    HWND h = CreateWindowEx(0, WC_LISTVIEW, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        10, 75, rc.right - 20, rc.bottom - 110, 
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

void LoadContactsToListView(HWND hList, const char *filter) {
    if (!hList || !db) return;
    ListView_DeleteAllItems(hList);

    sqlite3_stmt *stmt = NULL;
    int total_rows = 0;

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
    total_rows = row;
    
    // Update Status Bar
    char status[64];
    snprintf(status, sizeof(status), "Total %d contacts", total_rows);
    SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)status);

    if (total_rows == 0 && filter && strlen(filter) > 0) {
        MessageBox(hMainWnd, "No contact found matching your search.", "Search Result", MB_OK | MB_ICONINFORMATION);
    }
}

void CreateMainControls(HWND hWnd) {
    // Search Edit Control (Search Bar) - Y=8
    hSearchEdit = CreateWindowExA(0, "EDIT", SEARCH_PLACEHOLDER, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
        10, 8, 260, 24, hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);
    
    // Search Button - Y=8
    CreateWindowExA(0, "BUTTON", "Search", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        280, 8, 80, 24, hWnd, (HMENU)IDC_SEARCH_BTN, hInst, NULL);

    // Add Button - Y=40
    CreateWindowExA(0, "BUTTON", "Add Contact", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 40, 100, 30, hWnd, (HMENU)IDC_ADD_CONTACT, hInst, NULL);
    
    // Edit Button - Y=40
    CreateWindowExA(0, "BUTTON", "Edit Selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 40, 110, 30, hWnd, (HMENU)IDC_EDIT_CONTACT, hInst, NULL);
    
    // Delete Button - Y=40
    CreateWindowExA(0, "BUTTON", "Delete Selected", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        240, 40, 120, 30, hWnd, (HMENU)IDC_DELETE_CONTACT, hInst, NULL);

    // List View - Starts at Y=75
    hListView = CreateListView(hWnd);
    
    // Status Bar
    hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hWnd, (HMENU)IDC_STATUSBAR, hInst, NULL);
}

int GetSelectedContactId() {
    if (!hListView) return -1;
    int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    if (sel == -1) {
        return -1;
    }
    LVITEM it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = sel;
    ListView_GetItem(hListView, &it);
    return (int)it.lParam;
}

// --- Dialog Procedures ---

INT_PTR CALLBACK AddDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char name[100] = {0}, phone[20] = {0}, email[100] = {0};
            GetDlgItemTextA(hDlg, IDC_ADD_NAME, name, sizeof(name));
            GetDlgItemTextA(hDlg, IDC_ADD_PHONE, phone, sizeof(phone));
            GetDlgItemTextA(hDlg, IDC_ADD_EMAIL, email, sizeof(email));

            if (!IsNameValid(name) || !IsPhoneValid(phone) || !IsEmailValid(email)) {
                MessageBoxA(hDlg, "Invalid input. Check name (alphabetic), phone (numeric), or email (@ required).", "Input Error", MB_ICONERROR);
                return (INT_PTR)TRUE;
            }
            AddContact(name, phone, email);
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        } else if (LOWORD(wParam) == IDC_CLEAR) {
            SetDlgItemText(hDlg, IDC_ADD_NAME, "");
            SetDlgItemText(hDlg, IDC_ADD_PHONE, "");
            SetDlgItemText(hDlg, IDC_ADD_EMAIL, "");
            SetFocus(GetDlgItem(hDlg, IDC_ADD_NAME));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK EditDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int editId = -1;
    switch (message) {
    case WM_INITDIALOG: {
        editId = (int)lParam;
        if (editId <= 0) return (INT_PTR)TRUE;

        char sql[256];
        snprintf(sql, sizeof(sql), "SELECT name, phone, email FROM contacts WHERE id=%d;", editId);
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *name = sqlite3_column_text(stmt, 0);
            const unsigned char *phone = sqlite3_column_text(stmt, 1);
            const unsigned char *email = sqlite3_column_text(stmt, 2);

            if (name) SetDlgItemTextA(hDlg, IDC_EDIT_NAME, (const char*)name);
            if (phone) SetDlgItemTextA(hDlg, IDC_EDIT_PHONE, (const char*)phone);
            if (email) SetDlgItemTextA(hDlg, IDC_EDIT_EMAIL, (const char*)email);
        }
        sqlite3_finalize(stmt);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char name[100] = {0}, phone[20] = {0}, email[100] = {0};
            
            GetDlgItemTextA(hDlg, IDC_EDIT_NAME, name, sizeof(name));
            GetDlgItemTextA(hDlg, IDC_EDIT_PHONE, phone, sizeof(phone));
            GetDlgItemTextA(hDlg, IDC_EDIT_EMAIL, email, sizeof(email));

            if (!IsNameValid(name) || !IsPhoneValid(phone) || !IsEmailValid(email)) {
                MessageBoxA(hDlg, "Invalid input. Check name, phone, or email format.", "Input Error", MB_ICONERROR);
                return (INT_PTR)TRUE;
            }
            UpdateContact(editId, name, phone, email); 
            
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;

        } else if (LOWORD(wParam) == IDC_DELETE_ITEM) { 
            if (MessageBoxA(hDlg, "Delete this contact?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DeleteContact(editId);
                EndDialog(hDlg, IDOK);
            }
            return (INT_PTR)TRUE;

        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// --- Window Procedure ---

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateMainControls(hWnd);
        LoadContactsToListView(hListView, NULL);
        break;

    case WM_SIZE: {
        // Resize Status Bar
        SendMessage(hStatusBar, WM_SIZE, 0, 0);

        // Resize List View to fill remaining space
        RECT rc; GetClientRect(hWnd, &rc);
        int sb_height;
        RECT sb_rc;
        if (GetWindowRect(hStatusBar, &sb_rc)) {
            sb_height = sb_rc.bottom - sb_rc.top;
        } else {
            sb_height = 20; // Default height if failed
        }
        
        int listview_height = rc.bottom - 75 - sb_height - 5; 

        HDWP hdwp = BeginDeferWindowPos(1);
        DeferWindowPos(hdwp, hListView, NULL, 10, 75, rc.right - 20, listview_height, SWP_NOZORDER | SWP_NOACTIVATE);
        EndDeferWindowPos(hdwp);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_SEARCH_EDIT) {
            // Search Placeholder Logic
            if (code == EN_SETFOCUS) {
                char buf[256];
                GetWindowTextA(hSearchEdit, buf, sizeof(buf));
                if (strcmp(buf, SEARCH_PLACEHOLDER) == 0) SetWindowTextA(hSearchEdit, "");
            } else if (code == EN_KILLFOCUS) {
                char buf[256];
                GetWindowTextA(hSearchEdit, buf, sizeof(buf));
                if (strlen(buf) == 0) SetWindowTextA(hSearchEdit, SEARCH_PLACEHOLDER);
            }
            break;
        }

        switch (id) {
        case IDC_ADD_CONTACT:
        case IDM_CONTACT_ADD:
            if (DialogBox(hInst, MAKEINTRESOURCE(IDD_ADD_CONTACT), hWnd, AddDlgProc) == IDOK) {
                LoadContactsToListView(hListView, NULL);
            }
            break;

        case IDC_EDIT_CONTACT:
        case IDM_CONTACT_EDIT: {
            int idToEdit = GetSelectedContactId();
            if (idToEdit != -1) {
                if (DialogBoxParam(hInst, MAKEINTRESOURCE(IDD_EDIT_CONTACT), hWnd, EditDlgProc, (LPARAM)idToEdit) == IDOK) {
                    LoadContactsToListView(hListView, NULL);
                }
            } else {
                MessageBox(hWnd, "Please select a contact first.", "Info", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        
        case IDC_DELETE_CONTACT:
        case IDM_CONTACT_DEL: {
            int idToDelete = GetSelectedContactId();
            if (idToDelete != -1) {
                if (MessageBoxA(hWnd, "Delete selected contact?", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    DeleteContact(idToDelete);
                    LoadContactsToListView(hListView, NULL);
                }
            } else {
                MessageBox(hWnd, "Please select a contact first.", "Info", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case IDC_SEARCH_BTN:
        case IDM_CONTACT_SEARCH: {
            char search[200] = {0};
            GetWindowTextA(hSearchEdit, search, sizeof(search));
            if (strcmp(search, SEARCH_PLACEHOLDER) == 0) search[0] = '\0';
            LoadContactsToListView(hListView, search);
            break;
        }
        
        case IDM_CONTACT_VIEW: // Explicitly load all (Clear filter)
            SetWindowTextA(hSearchEdit, SEARCH_PLACEHOLDER);
            LoadContactsToListView(hListView, NULL);
            break;

        case IDM_FILE_EXIT:
            DestroyWindow(hWnd);
            break;
        } 
        break;
    } 

    case WM_NOTIFY: {
        LPNMHDR lpnmhdr = (LPNMHDR)lParam;
        if (lpnmhdr->code == NM_DBLCLK) {
            if (lpnmhdr->idFrom == IDC_LISTVIEW) {
                // Double-click to Edit
                SendMessage(hWnd, WM_COMMAND, (WPARAM)IDC_EDIT_CONTACT, 0);
                return TRUE;
            }
        }
        break;
    }

    case WM_DESTROY:
        if (db) sqlite3_close(db);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// --- Main Functions ---

ATOM MyRegisterClass(HINSTANCE hInstance) {
    WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
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
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);
    
    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCEL));

    InitDatabase();
    if (!db) {
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
    
    return (int)msg.wParam;
}