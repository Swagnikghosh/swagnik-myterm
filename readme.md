# 🖥️ MyTerm - A Custom Terminal with X11 GUI

**MyTerm** is a standalone shell application built with **X11 GUI**.  
It accepts and executes user commands line-by-line, similar to a Linux terminal, while offering advanced features like multi-tab support, command history, autocomplete, and more — all within a graphical interface.

---

## 🚀 Features Implemented

### 🪟 Graphical User Interface

- Built using **X11**.
- Handles all keyboard inputs and common key combinations.
- Supports **multiple independent tabs**, which can be switched using:
  - `Alt + Tab` / `Alt + Shift + Tab`
  - or through the on-screen tab buttons.

---

### ⚙️ Run External Commands

Execute typical Linux commands just like in the Bash shell.

**Examples:**

```bash
ls
ls -l
cd ..
```

---

### 🧵 Multiline Unicode Input

- Allows multiline input using double quotes.
- Example:
  ```bash
  echo "Hello
  World"
  ```
- **Note:** Unicode input is supported but cannot be displayed.

---

### 📥 Input Redirection

Redirect standard input from a file using `<`.

**Examples:**

```bash
./a.out < input.txt
sort < somefile.txt
```

---

### 📤 Output Redirection

Redirect standard output to a file using `>`.

**Examples:**

```bash
./a.out > output.txt
ls > files.txt
```

Supports combined input and output redirection:

```bash
./a.out < input.txt > output.txt
```

---

### 🔗 Pipe Support

Connect multiple commands using pipes `|`.

**Examples:**

```bash
ls *.txt | wc -l
cat file.c | sort | more
ls *.txt | xargs rm
```

---

### 🧩 Custom Command: `multiWatch`

Run multiple commands **in parallel** using multiple processes.

**Syntax:**

```bash
multiWatch ["cmd1", "cmd2", "cmd3", ...]
```

**Example:**

```bash
multiWatch ["echo Hello", "ls", "cat a.txt"]
```

Ends execution after receiving `Ctrl + C`.

---

### ⌨️ Line Navigation

- `Ctrl + A` → Move cursor to the **start** of the line.
- `Ctrl + E` → Move cursor to the **end** of the line.

---

### 🛑 Interrupting Commands

> **Note:** Feature not implemented.  
> (Interrupting commands using signal handling was not completed.)

---

### 🕒 Searchable Command History

- Keeps history of **10,000** executed commands.
- The command `history` shows the **most recent 1,000**.
- Press **Ctrl + R** to search command history:
  - Prompts: `"Enter search term"`
  - Displays matching results dynamically.

---

### ⚡ Autocomplete for File Names

- Autocompletes filenames from the current working directory.
- Type the first few letters of a filename and press **Tab** to auto-complete.

---

## 🎁 Bonus Functionalities

1. ⬅️➡️ **Left / Right Arrow — Move Across Input**

2. ⬆️⬇️ **Up / Down Arrows — Command History Navigation**

3. 📋 **Ctrl + V — Paste Clipboard Text**

4. 🔄 **Ctrl + Tab / Ctrl + Shift + Tab — Switch Between Tabs**

5. 🖱️ **Mouse Click — Add, Close, or Switch Tabs**

6. 💡 **Blinking Cursor — Realistic Typing Experience**

7. ⌫ **Backspace / Delete — Edit Text Inline**

---

## ⚙️ Installation & Running MyTerm

Run the following commands in your bash terminal:

```bash
# Build the project
make
# Run the terminal GUI
./main
```

---

## 🧱 Tech Stack

| Component            | Technology |
| -------------------- | ---------- |
| **Language**         | C / C++    |
| **GUI Framework**    | X11 (Xlib) |
| **Operating System** | Linux      |
| **Build Tool**       | Makefile   |

---

## 🧩 Key Shortcuts

| Action                 | Shortcut            |
| ---------------------- | ------------------- |
| Open Next Tab          | `Alt + Tab`         |
| Open Previous Tab      | `Alt + Shift + Tab` |
| Move to Line Start     | `Ctrl + A`          |
| Move to Line End       | `Ctrl + E`          |
| Search History         | `Ctrl + R`          |
| Paste                  | `Ctrl + V`          |
| Scroll Command History | `↑` / `↓`           |
| Move Cursor            | `←` / `→`           |
| Exit multiWatch        | `Ctrl + C`          |

---

---

## 👤 Author

**Swagnik**  
💻 Passionate about low-level systems programming and user interface design.  
📧 [swagnikghosh.cse25@kgpian.iitkgp.ac.in]  
🔗 [https://github.com/swagnikghosh]

---

## 🌟 Acknowledgments

- Inspired by **GNU Bash** and **xterm**.
- Built as part of a systems programming project to explore process control, I/O redirection, and GUI integration using X11.

---

---