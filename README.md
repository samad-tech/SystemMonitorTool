# SystemMonitorTool# 🖥️ SysMon - System Monitor Tool (Assignment 3 - LSP)

A simple **Linux system monitoring tool** written in **C++17** using the **ncurses library** — similar to the `top` or `htop` command.

---

## 🎯 Objective
To develop a **console-based system monitor tool** that displays:
- Real-time **CPU usage**
- **Memory usage**
- **Running processes** (PID, user, %CPU, %MEM, RSS, and command)
- Supports interactive controls for sorting and killing processes

---

## ⚙️ Features
✅ Live CPU and Memory statistics  
✅ Process list with PID, USER, %CPU, %MEM, RSS, CMD  
✅ Sort processes by CPU or Memory (toggle with **`s`**)  
✅ Kill process by PID (press **`k`** then enter PID)  
✅ Refresh automatically every **2 seconds**  
✅ Quit easily with **`q`**

---

## 🧩 Requirements
- **Linux** or **WSL (Windows Subsystem for Linux)**
- `g++` compiler
- `ncurses` library

Install dependencies:
```bash
sudo apt update
sudo apt install g++ libncurses5-dev libncursesw5-dev
