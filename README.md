# 📱 Contact Management System (C + Win32 + SQLite)

A lightweight, native Windows contact manager built in pure C using Win32 API and SQLite for persistent storage.

> ✅ Add, Edit, Delete, Search, View contacts  
> ✅ Modern UI with ListView, Context Menu, Status Bar  
> ✅ No external frameworks — pure Windows API + SQLite

![Screenshot](screenshot.png) **(Add your screenshot later)*

---

## 🌟 Features

- ✅ **Add Contact** — Name, Phone, Email
- ✅ **Edit Contact** — Double-click or right-click → Edit
- ✅ **Delete Contact** — Right-click → Delete or via Edit dialog
- ✅ **Search Contacts** — Real-time filter by name/phone/email
- ✅ **View All** — Clean ListView with columns (Name | Phone | Email)
- ✅ **Status Bar** — Shows total contact count
- ✅ **Keyboard Shortcuts** — Ctrl+N to Add
- ✅ **SQLite Backend** — Data saved in `contacts.db`

---

## 🛠️ Build & Run (MinGW / MSYS2)

### Prerequisites

- [MinGW-w64](https://www.mingw-w64.org/) or [MSYS2](https://www.msys2.org/)
- GCC compiler (`gcc`)
- SQLite amalgamation (`sqlite3.c` + `sqlite3.h`)

### Step-by-step
   
1. Compile resources:
   windres resource.rc -O coff -o resource.o
   
2. Compile source files:
  gcc -c sqlite3.c -o sqlite3.o -I.
  gcc -c main.c -o main.o -I.

3. Link into executable:
   gcc main.o sqlite3.o resource.o -o contact_manager.exe -lcomctl32 -luser32 -lgdi32 -lshell32 -mwindows
   
4. Run the app:
./contact_manager.exe


   
