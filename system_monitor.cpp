// sysmon.cpp
// System Monitor Tool (simple top-like tool) for Linux
// Compile: g++ sysmon.cpp -o sysmon -std=c++17 -lncurses
//
// Features:
// - Shows CPU usage, memory usage
// - Lists processes with PID, USER, %CPU, %MEM, RSS, CMD
// - Sort by CPU or MEM (toggle with 's')
// - Kill a process by PID (press 'k' then enter PID)
// - Refresh automatically every REFRESH_INTERVAL seconds (default 2s)
// - Quit with 'q'

#include <bits/stdc++.h>
#include <ncurses.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>

using namespace std;

struct CpuSnapshot {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    unsigned long long total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    }
    unsigned long long idleAll() const {
        return idle + iowait;
    }
};

struct ProcSnapshot {
    pid_t pid;
    string user;
    string cmd;
    unsigned long long utime;
    unsigned long long stime;
    unsigned long long total_time() const { return utime + stime; }
    unsigned long rss; // in KB (approx)
    double cpu_percent;
    double mem_percent;
};

static const int REFRESH_INTERVAL = 2; // seconds
static long Hertz = sysconf(_SC_CLK_TCK);
static unsigned long long total_mem_kb_cache = 0;

bool sort_by_cpu = true;

CpuSnapshot read_cpu_line() {
    CpuSnapshot s = {0};
    ifstream f("/proc/stat");
    string line;
    if (!f.is_open()) return s;
    getline(f, line);
    // Example: cpu  4705 150 1994 136239 234 0 45 0 0 0
    string label;
    stringstream ss(line);
    ss >> label;
    ss >> s.user >> s.nice >> s.system >> s.idle >> s.iowait >> s.irq >> s.softirq >> s.steal >> s.guest >> s.guest_nice;
    return s;
}

unsigned long long read_total_memory_kb() {
    ifstream f("/proc/meminfo");
    string line;
    unsigned long long memTotal = 0;
    while (getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            stringstream ss(line);
            string label; unsigned long long val; string unit;
            ss >> label >> val >> unit;
            memTotal = val; // kB
            break;
        }
    }
    return memTotal;
}

string uid_to_user(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (pw) return string(pw->pw_name);
    return to_string(uid);
}

bool is_number(const string &s) {
    for (char c : s) if (!isdigit(c)) return false;
    return true;
}

ProcSnapshot read_proc(pid_t pid) {
    ProcSnapshot p{};
    p.pid = pid;
    p.cpu_percent = 0.0;
    p.mem_percent = 0.0;
    string base = "/proc/" + to_string(pid);
    // cmdline
    ifstream fcmd(base + "/cmdline");
    if (fcmd.is_open()) {
        string cmd;
        getline(fcmd, cmd, '\0');
        if (cmd.empty()) {
            // fallback to comm
            ifstream fcomm(base + "/comm");
            if (fcomm.is_open()) {
                getline(fcomm, cmd);
            }
        }
        // replace '\0' with ' '
        for (char &c : cmd) if (c == '\0') c = ' ';
        p.cmd = cmd;
    } else {
        p.cmd = "";
    }

    // read stat
    ifstream fstat(base + "/stat");
    if (fstat.is_open()) {
        // fields: pid (1) comm (2) state (3) ... utime (14) stime (15) ... rss (24)
        string content;
        getline(fstat, content);
        stringstream ss(content);
        string token;
        vector<string> fields;
        while (ss >> token) fields.push_back(token);
        if (fields.size() >= 24) {
            unsigned long long utime = stoull(fields[13]);
            unsigned long long stime = stoull(fields[14]);
            long rss_pages = stol(fields[23]);
            long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
            p.utime = utime;
            p.stime = stime;
            p.rss = rss_pages * page_size_kb; // in KB
        }
    }

    // read status for uid
    ifstream fstatus(base + "/status");
    if (fstatus.is_open()) {
        string line;
        while (getline(fstatus, line)) {
            if (line.rfind("Uid:", 0) == 0) {
                stringstream ss(line);
                string label; uid_t real_uid;
                ss >> label >> real_uid;
                p.user = uid_to_user(real_uid);
            }
        }
    }

    return p;
}

vector<pid_t> list_pids() {
    vector<pid_t> pids;
    DIR *d = opendir("/proc");
    if (!d) return pids;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            string name = entry->d_name;
            if (is_number(name)) {
                pids.push_back(stoi(name));
            }
        }
    }
    closedir(d);
    return pids;
}

int main() {
    // initialize
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE); // non-blocking getch
    keypad(stdscr, TRUE);
    curs_set(0);

    Hertz = sysconf(_SC_CLK_TCK);
    total_mem_kb_cache = read_total_memory_kb();

    CpuSnapshot prev_cpu = read_cpu_line();
    unordered_map<pid_t, ProcSnapshot> prev_procs;

    while (true) {
        // handle input
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        else if (ch == 's' || ch == 'S') sort_by_cpu = !sort_by_cpu;
        else if (ch == 'r' || ch == 'R') {
            // force refresh, nothing needed since we refresh each loop
        } else if (ch == 'k' || ch == 'K') {
            // ask user for PID (blocking read)
            echo();
            curs_set(1);
            nodelay(stdscr, FALSE);
            mvprintw(LINES - 2, 0, "Enter PID to kill: ");
            clrtoeol();
            char buf[32];
            getnstr(buf, 31);
            int pid = atoi(buf);
            if (pid > 0) {
                int res = kill(pid, SIGTERM);
                if (res == 0) {
                    mvprintw(LINES - 2, 0, "Sent SIGTERM to %d. Press any key to continue...", pid);
                } else {
                    mvprintw(LINES - 2, 0, "Failed to kill %d (errno %d). Press any key to continue...", pid, errno);
                }
            } else {
                mvprintw(LINES - 2, 0, "Invalid PID. Press any key to continue...");
            }
            getch();
            nodelay(stdscr, TRUE);
            noecho();
            curs_set(0);
        }

        // read current CPU snapshot
        CpuSnapshot cur_cpu = read_cpu_line();
        unsigned long long prev_tot = prev_cpu.total();
        unsigned long long cur_tot = cur_cpu.total();
        unsigned long long tot_diff = cur_tot - prev_tot;
        unsigned long long idle_diff = cur_cpu.idleAll() - prev_cpu.idleAll();
        double cpu_usage = 0.0; // percent
        if (tot_diff > 0) cpu_usage = 100.0 * (double)(tot_diff - idle_diff) / (double)tot_diff;

        // memory
        unsigned long long mem_total = total_mem_kb_cache;
        unsigned long long mem_free = 0, mem_available = 0;
        ifstream fmem("/proc/meminfo");
        string line;
        while (getline(fmem, line)) {
            if (line.rfind("MemAvailable:", 0) == 0) {
                stringstream ss(line);
                string label; unsigned long long val; string unit;
                ss >> label >> val >> unit;
                mem_available = val;
            } else if (line.rfind("MemFree:", 0) == 0) {
                stringstream ss(line);
                string label; unsigned long long val; string unit;
                ss >> label >> val >> unit;
                mem_free = val;
            }
        }
        unsigned long long mem_used = 0;
        if (mem_total > mem_available) mem_used = mem_total - mem_available;
        else mem_used = mem_total - mem_free;

        // read processes
        vector<pid_t> pids = list_pids();
        vector<ProcSnapshot> procs;
        procs.reserve(pids.size());
        for (pid_t pid : pids) {
            ProcSnapshot cur = read_proc(pid);
            // compute cpu percent relative to previous snapshot
            double cpu_pct = 0.0;
            if (prev_procs.count(pid)) {
                unsigned long long prev_total_time = prev_procs[pid].total_time();
                unsigned long long cur_total_time = cur.total_time();
                unsigned long long proc_time_diff = 0;
                if (cur_total_time >= prev_total_time) proc_time_diff = cur_total_time - prev_total_time;
                if (tot_diff > 0) {
                    // cpu % = (proc_time_diff / Hertz) / (tot_diff / Hertz) * 100
                    // simplified: proc_time_diff / tot_diff * 100
                    cpu_pct = 100.0 * (double)proc_time_diff / (double)tot_diff;
                }
            }
            cur.cpu_percent = cpu_pct;
            // mem %
            if (mem_total > 0) {
                cur.mem_percent = 100.0 * (double)cur.rss / (double)mem_total;
            } else cur.mem_percent = 0.0;
            procs.push_back(cur);
        }

        // update previous proc map
        prev_procs.clear();
        for (auto &p : procs) prev_procs[p.pid] = p;
        prev_cpu = cur_cpu;

        // sort
        if (sort_by_cpu) {
            sort(procs.begin(), procs.end(), [](const ProcSnapshot &a, const ProcSnapshot &b){
                if (a.cpu_percent == b.cpu_percent) return a.mem_percent > b.mem_percent;
                return a.cpu_percent > b.cpu_percent;
            });
        } else {
            sort(procs.begin(), procs.end(), [](const ProcSnapshot &a, const ProcSnapshot &b){
                if (a.mem_percent == b.mem_percent) return a.cpu_percent > b.cpu_percent;
                return a.mem_percent > b.mem_percent;
            });
        }

        // draw UI
        erase();
        attron(A_BOLD);
        mvprintw(0, 0, "SysMon - simple system monitor (press q to quit)   Refresh: %ds   Sort: %s",
                 REFRESH_INTERVAL, sort_by_cpu ? "CPU" : "MEM");
        attroff(A_BOLD);
        mvprintw(1, 0, "CPU Usage: %.2f%%   Mem: %llu kB total   Used: %llu kB (approx)",
                 cpu_usage, mem_total, mem_used);
        mvprintw(2, 0, "PID     USER       %%CPU   %%MEM   RSS(kB)   CMD");
        int row = 3;
        int max_rows = LINES - 5;
        for (size_t i = 0; i < procs.size() && i < (size_t)max_rows; ++i) {
            const auto &p = procs[i];
            mvprintw(row + i, 0, "%-7d %-10.10s %6.2f %7.2f %10llu  %.40s",
                     p.pid, p.user.c_str(), p.cpu_percent, p.mem_percent, p.rss, p.cmd.c_str());
        }
        mvprintw(LINES - 3, 0, "Commands: (s) toggle sort  (k) kill PID  (r) refresh  (q) quit");
        refresh();

        // sleep for interval but still allow user input to be responsive
        for (int i = 0; i < REFRESH_INTERVAL * 10; ++i) {
            napms(100);
            int c = getch();
            if (c == 'q' || c == 'Q') goto cleanup;
            else if (c == 's' || c == 'S') {
                sort_by_cpu = !sort_by_cpu;
                break;
            } else if (c == 'k' || c == 'K') {
                // we will handle on top of loop (force user input)
                ungetch('k');
                break;
            }
        }
    }

cleanup:
    endwin();
    return 0;
}