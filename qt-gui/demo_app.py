#!/usr/bin/env python3
"""
MegaCustom Qt6 GUI Demo
This is a demonstration of the Qt6 application functionality using Python/Tkinter
as a proxy since Qt6 is not installed. This shows the expected behavior.
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import os
import sys
from datetime import datetime

class LoginDialog:
    """Mock login dialog"""
    def __init__(self, parent):
        self.result = None
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Login to MegaCustom")
        self.dialog.geometry("400x450")
        self.dialog.resizable(False, False)

        # Center the dialog
        self.dialog.transient(parent)
        self.dialog.grab_set()

        # Create UI
        frame = ttk.Frame(self.dialog, padding="20")
        frame.pack(fill=tk.BOTH, expand=True)

        # Logo/Title
        ttk.Label(frame, text="MegaCustom", font=("Arial", 24, "bold")).pack(pady=10)
        ttk.Label(frame, text="Sign in to your account", font=("Arial", 12)).pack(pady=5)

        # Email field
        ttk.Label(frame, text="Email:").pack(anchor=tk.W, pady=(20, 5))
        self.email_var = tk.StringVar(value="user@example.com")
        self.email_entry = ttk.Entry(frame, textvariable=self.email_var, width=40)
        self.email_entry.pack(fill=tk.X)

        # Password field
        ttk.Label(frame, text="Password:").pack(anchor=tk.W, pady=(10, 5))
        self.password_var = tk.StringVar()
        self.password_entry = ttk.Entry(frame, textvariable=self.password_var, show="•", width=40)
        self.password_entry.pack(fill=tk.X)

        # Remember me checkbox
        self.remember_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(frame, text="Remember me", variable=self.remember_var).pack(anchor=tk.W, pady=10)

        # Buttons
        button_frame = ttk.Frame(frame)
        button_frame.pack(pady=20, fill=tk.X)

        ttk.Button(button_frame, text="Cancel", command=self.cancel).pack(side=tk.LEFT)
        ttk.Button(button_frame, text="Login", command=self.login).pack(side=tk.RIGHT)

        # Status
        self.status_label = ttk.Label(frame, text="Demo mode - any credentials will work", foreground="blue")
        self.status_label.pack(pady=10)

        self.email_entry.focus()

    def login(self):
        """Handle login"""
        email = self.email_var.get().strip()
        password = self.password_var.get()

        if not email:
            messagebox.showerror("Error", "Please enter your email address")
            return

        if not password:
            messagebox.showerror("Error", "Please enter your password")
            return

        # In demo mode, accept any credentials
        self.result = {"email": email, "remember": self.remember_var.get()}
        self.dialog.destroy()

    def cancel(self):
        """Cancel login"""
        self.dialog.destroy()

class FileExplorer(ttk.Frame):
    """Mock file explorer widget"""
    def __init__(self, parent, title="Files", explorer_type="local"):
        super().__init__(parent)
        self.explorer_type = explorer_type

        # Header
        header_frame = ttk.Frame(self)
        header_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Label(header_frame, text=title, font=("Arial", 10, "bold")).pack(side=tk.LEFT)

        # Toolbar
        toolbar_frame = ttk.Frame(self)
        toolbar_frame.pack(fill=tk.X, padx=5)

        ttk.Button(toolbar_frame, text="←", width=3, command=self.go_back).pack(side=tk.LEFT)
        ttk.Button(toolbar_frame, text="→", width=3, command=self.go_forward).pack(side=tk.LEFT)
        ttk.Button(toolbar_frame, text="↑", width=3, command=self.go_up).pack(side=tk.LEFT)
        ttk.Button(toolbar_frame, text="🏠", width=3, command=self.go_home).pack(side=tk.LEFT)
        ttk.Button(toolbar_frame, text="⟲", width=3, command=self.refresh).pack(side=tk.LEFT)

        # Path bar
        self.path_var = tk.StringVar(value="/home/user" if explorer_type == "local" else "/")
        ttk.Entry(toolbar_frame, textvariable=self.path_var).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)

        # File list
        list_frame = ttk.Frame(self)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Create treeview with columns
        self.tree = ttk.Treeview(list_frame, columns=("Size", "Modified"), show="tree headings")
        self.tree.heading("#0", text="Name")
        self.tree.heading("Size", text="Size")
        self.tree.heading("Modified", text="Modified")

        # Scrollbars
        vsb = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        hsb = ttk.Scrollbar(list_frame, orient="horizontal", command=self.tree.xview)
        self.tree.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)

        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        hsb.grid(row=1, column=0, sticky="ew")

        list_frame.grid_rowconfigure(0, weight=1)
        list_frame.grid_columnconfigure(0, weight=1)

        # Populate with demo data
        self.populate_demo_data()

        # Status bar
        status_frame = ttk.Frame(self)
        status_frame.pack(fill=tk.X, padx=5)

        self.status_label = ttk.Label(status_frame, text="Ready")
        self.status_label.pack(side=tk.LEFT)

    def populate_demo_data(self):
        """Add demo files to the tree"""
        if self.explorer_type == "local":
            # Local files
            self.tree.insert("", "end", text="📁 Documents", values=("", "2024-11-29"))
            self.tree.insert("", "end", text="📁 Downloads", values=("", "2024-11-29"))
            self.tree.insert("", "end", text="📁 Pictures", values=("", "2024-11-29"))
            self.tree.insert("", "end", text="📄 README.md", values=("4.2 KB", "2024-11-29"))
            self.tree.insert("", "end", text="📄 config.ini", values=("1.1 KB", "2024-11-28"))
        else:
            # Remote files (Mega)
            self.tree.insert("", "end", text="📁 Cloud Backup", values=("", "2024-11-29"))
            self.tree.insert("", "end", text="📁 Shared Files", values=("", "2024-11-28"))
            self.tree.insert("", "end", text="📄 backup.zip", values=("125 MB", "2024-11-27"))
            self.tree.insert("", "end", text="📄 photos.tar.gz", values=("2.3 GB", "2024-11-26"))

        self.update_status()

    def update_status(self):
        """Update status bar"""
        count = len(self.tree.get_children())
        self.status_label.config(text=f"{count} items")

    def go_back(self):
        self.status_label.config(text="Navigate back (demo)")

    def go_forward(self):
        self.status_label.config(text="Navigate forward (demo)")

    def go_up(self):
        self.status_label.config(text="Navigate up (demo)")

    def go_home(self):
        self.status_label.config(text="Navigate home (demo)")

    def refresh(self):
        self.status_label.config(text="Refreshing... (demo)")
        self.after(500, lambda: self.status_label.config(text="Ready"))

class MegaCustomGUI:
    """Main application window"""
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("MegaCustom Qt6 GUI - Demo Mode")
        self.root.geometry("1200x700")

        self.logged_in = False
        self.user_email = None

        self.create_menu()
        self.create_toolbar()
        self.create_main_content()
        self.create_statusbar()

        # Start with login
        self.root.after(100, self.show_login)

    def create_menu(self):
        """Create menu bar"""
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        # File menu
        file_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="Connect", command=self.show_login)
        file_menu.add_command(label="Disconnect", command=self.logout)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.quit)

        # Edit menu
        edit_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Edit", menu=edit_menu)
        edit_menu.add_command(label="Copy", command=lambda: self.show_message("Copy"))
        edit_menu.add_command(label="Cut", command=lambda: self.show_message("Cut"))
        edit_menu.add_command(label="Paste", command=lambda: self.show_message("Paste"))

        # View menu
        view_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="View", menu=view_menu)
        view_menu.add_command(label="Refresh", command=lambda: self.show_message("Refresh"))
        view_menu.add_command(label="Show Hidden", command=lambda: self.show_message("Toggle hidden"))

        # Tools menu
        tools_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Tools", menu=tools_menu)
        tools_menu.add_command(label="Settings", command=self.show_settings)
        tools_menu.add_command(label="Transfer Queue", command=self.show_transfers)

        # Help menu
        help_menu = tk.Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Help", menu=help_menu)
        help_menu.add_command(label="About", command=self.show_about)
        help_menu.add_command(label="Documentation", command=lambda: self.show_message("Open docs"))

    def create_toolbar(self):
        """Create toolbar"""
        toolbar = ttk.Frame(self.root)
        toolbar.pack(fill=tk.X, padx=2, pady=2)

        ttk.Button(toolbar, text="Upload", command=self.upload_file).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Download", command=self.download_file).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="New Folder", command=self.new_folder).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Delete", command=self.delete_file).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Rename", command=self.rename_file).pack(side=tk.LEFT, padx=2)

        ttk.Separator(toolbar, orient="vertical").pack(side=tk.LEFT, fill=tk.Y, padx=5)

        ttk.Button(toolbar, text="Sync", command=self.sync_files).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Share", command=self.share_file).pack(side=tk.LEFT, padx=2)

    def create_main_content(self):
        """Create main content area"""
        self.main_frame = ttk.Frame(self.root)
        self.main_frame.pack(fill=tk.BOTH, expand=True)

        # Create paned window for dual-pane layout
        self.paned = ttk.PanedWindow(self.main_frame, orient="horizontal")
        self.paned.pack(fill=tk.BOTH, expand=True)

        # Left pane - Local files
        self.local_explorer = FileExplorer(self.paned, "Local Files", "local")
        self.paned.add(self.local_explorer, weight=1)

        # Right pane - Remote files
        self.remote_explorer = FileExplorer(self.paned, "Mega Cloud", "remote")
        self.paned.add(self.remote_explorer, weight=1)

        # Initially disable remote explorer
        self.set_logged_in(False)

    def create_statusbar(self):
        """Create status bar"""
        self.statusbar = ttk.Frame(self.root)
        self.statusbar.pack(fill=tk.X)

        self.status_label = ttk.Label(self.statusbar, text="Not connected", relief=tk.SUNKEN)
        self.status_label.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.user_label = ttk.Label(self.statusbar, text="", relief=tk.SUNKEN)
        self.user_label.pack(side=tk.RIGHT, padx=5)

    def show_login(self):
        """Show login dialog"""
        if self.logged_in:
            messagebox.showinfo("Info", "Already logged in")
            return

        dialog = LoginDialog(self.root)
        self.root.wait_window(dialog.dialog)

        if dialog.result:
            self.user_email = dialog.result["email"]
            self.set_logged_in(True)
            messagebox.showinfo("Success", f"Logged in as {self.user_email}\n(Demo mode)")

    def logout(self):
        """Logout"""
        if not self.logged_in:
            messagebox.showinfo("Info", "Not logged in")
            return

        self.set_logged_in(False)
        messagebox.showinfo("Success", "Logged out successfully")

    def set_logged_in(self, logged_in):
        """Update UI based on login status"""
        self.logged_in = logged_in

        if logged_in:
            self.status_label.config(text="Connected to Mega")
            self.user_label.config(text=self.user_email)
            for child in self.remote_explorer.winfo_children():
                child.configure(state="normal")
        else:
            self.status_label.config(text="Not connected")
            self.user_label.config(text="")
            self.user_email = None
            for child in self.remote_explorer.winfo_children():
                try:
                    child.configure(state="disabled")
                except:
                    pass

    def show_message(self, message):
        """Show demo message"""
        self.status_label.config(text=f"Demo: {message}")

    def upload_file(self):
        """Demo upload"""
        if not self.logged_in:
            messagebox.showwarning("Warning", "Please login first")
            return

        files = filedialog.askopenfilenames(title="Select files to upload")
        if files:
            messagebox.showinfo("Demo", f"Would upload {len(files)} file(s) to Mega")

    def download_file(self):
        """Demo download"""
        if not self.logged_in:
            messagebox.showwarning("Warning", "Please login first")
            return

        messagebox.showinfo("Demo", "Would download selected file from Mega")

    def new_folder(self):
        """Demo new folder"""
        name = tk.simpledialog.askstring("New Folder", "Enter folder name:")
        if name:
            messagebox.showinfo("Demo", f"Would create folder: {name}")

    def delete_file(self):
        """Demo delete"""
        if messagebox.askyesno("Confirm", "Delete selected file(s)?"):
            messagebox.showinfo("Demo", "Would delete selected file(s)")

    def rename_file(self):
        """Demo rename"""
        name = tk.simpledialog.askstring("Rename", "Enter new name:")
        if name:
            messagebox.showinfo("Demo", f"Would rename to: {name}")

    def sync_files(self):
        """Demo sync"""
        if not self.logged_in:
            messagebox.showwarning("Warning", "Please login first")
            return

        messagebox.showinfo("Demo", "Would start file synchronization")

    def share_file(self):
        """Demo share"""
        if not self.logged_in:
            messagebox.showwarning("Warning", "Please login first")
            return

        messagebox.showinfo("Demo", "Would open share dialog")

    def show_settings(self):
        """Show settings dialog"""
        messagebox.showinfo("Settings", "Settings dialog would appear here\n\nOptions:\n- Dark mode\n- Auto-login\n- Sync folders\n- Proxy settings")

    def show_transfers(self):
        """Show transfer queue"""
        messagebox.showinfo("Transfers", "Transfer queue would show:\n- Active uploads\n- Active downloads\n- Completed transfers\n- Failed transfers")

    def show_about(self):
        """Show about dialog"""
        about_text = """MegaCustom Qt6 GUI
Version 1.0.0 (Demo)

A modern desktop application for Mega cloud storage
Built with Qt6 framework

This is a demonstration version showing the
expected functionality of the Qt6 application.

© 2024 MegaCustom"""
        messagebox.showinfo("About MegaCustom", about_text)

    def run(self):
        """Run the application"""
        print("Starting MegaCustom GUI Demo...")
        print("This demonstrates the expected Qt6 application behavior")
        print("-" * 50)
        self.root.mainloop()

# Import simpledialog for input dialogs
import tkinter.simpledialog as simpledialog

if __name__ == "__main__":
    app = MegaCustomGUI()
    app.run()