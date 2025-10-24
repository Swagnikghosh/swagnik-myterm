// run.cpp  — full UI loop, key handling, and multiWatch integration
// (Complete; keep together with drawscreen.cpp and execute.cpp)

#include <iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <cctype>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cstdio>
#include <err.h>
#include <string>
#include <chrono>
#include <vector>
#include <bits/stdc++.h>
#include <unistd.h>
#include <cctype>
#include <regex>
#include <sys/stat.h>
#include <limits.h>
#include <thread>
#include <atomic>

using namespace std;

// Include your drawscreen implementation (contains TabState, drawing helpers, globals)

#include "execute.cpp"

// The execute.cpp you have must provide these symbols:
// - notify_sigint_from_ui(): async-safe notification from UI to execute logic
// - multiWatchThreaded_using_pipes(...): the background watcher to run commands
// - atomic flags: mw_stop_requested, mw_finished, cmd_running
extern "C" void notify_sigint_from_ui();
extern void multiWatchThreaded_using_pipes(const std::vector<std::string> &cmds, int tab_index);
extern std::atomic<bool> mw_stop_requested;
extern std::atomic<bool> mw_finished;
extern std::atomic<bool> cmd_running;

// Ensure these externs match drawscreen.cpp
extern Display *dpy;
extern Window root;
extern int scr;
extern int active_tab;
extern std::vector<TabState> tabs;
extern std::vector<std::string> inputs;

// ------------------ add_tab (exact as you provided) ------------------
// static void add_tab(const string &initial_cwd = "/")
// {
//     TabState t;
//     t.cwd = initial_cwd;
//     string sdisp = formatPWD(t.cwd);
//     string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
//     t.screenBuffer.push_back(prompt);
//     t.inpIdx = (int)inputs.size() - 1;
//     t.title = "Tab " + to_string((int)tabs.size() + 1);
//     tabs.push_back(std::move(t));
//     active_tab = (int)tabs.size() - 1;
// }
static Window create_window(int x, int y, int w, int h, int border)
{
    XSetWindowAttributes xwa;
    Colormap cmap = DefaultColormap(dpy, scr);
    XColor bg;
    XAllocNamedColor(dpy, cmap, "#1e1e1e", &bg, &bg); // VSCode-like dark
    xwa.background_pixel = bg.pixel;
    xwa.border_pixel = WhitePixel(dpy, scr);
    xwa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask | PointerMotionMask;
    Window win = XCreateWindow(dpy, root, x, y, w, h, border, DefaultDepth(dpy, scr), InputOutput, DefaultVisual(dpy, scr), CWBackPixel | CWBorderPixel | CWEventMask, &xwa);
    return win;
}
// ------------------ helpers used in this file ------------------

// Helper to store input history (assumes file and storeInput defined in your drawscreen set)

// For selection clipboard paste handling
// (PASTE_BUFFER atom usage is present in the user's pasted code)

// ------------------ main run() ------------------

void run()
{
    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        errx(1, "Cant open display");
    }
    inputs = loadInputs();

    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);

    Window win = create_window(POSX, POSY, WIDTH, HEIGHT, BORDER);
    XStoreName(dpy, win, "swagnik@myterm");

    // font + gc
    XFontStruct *font = XLoadQueryFont(dpy, "-misc-fixed-bold-r-normal--20-200-75-75-c-100-iso8859-1");
    if (!font)
        font = XLoadQueryFont(dpy, "fixed");
    GC gc = XCreateGC(dpy, win, 0, nullptr);
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, WhitePixel(dpy, scr));

    // XIM/XIC setup (best-effort)
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim)
        std::cerr << "XOpenIM failed — continuing without input method\n";
    XIC xic = nullptr;
    if (xim)
    {
        xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win, XNFocusWindow, win, nullptr);
        if (!xic)
            std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XMapWindow(dpy, win);

    // initial tab
    add_tab("/");

    // main event loop
    while (true)
    {
        // Process all pending X events
        while (XPending(dpy) > 0)
        {
            XEvent event;
            XNextEvent(dpy, &event);

            // Prepare wide-char translation variables
            wchar_t wbuf[32];
            KeySym keysym = 0;
            Status status = 0;
            int len = 0;
            if (event.type == KeyPress)
            {
                if (xic)
                    len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);
                else
                {
                    len = 0;
                    keysym = event.xkey.keycode;
                }
            }

            switch (event.type)
            {
            case Expose:
            {
                XWindowAttributes wa;
                XGetWindowAttributes(dpy, win, &wa);
                draw_navbar(win, gc, wa.width);
                draw_tabs(win, gc, font);
                if (active_tab >= 0 && active_tab < (int)tabs.size())
                    drawScreen(win, gc, font, tabs[active_tab]);
                break;
            }

            case ConfigureNotify:
            {
                XWindowAttributes wa;
                XGetWindowAttributes(dpy, win, &wa);
                draw_navbar(win, gc, wa.width);
                draw_tabs(win, gc, font);
                if (active_tab >= 0 && active_tab < (int)tabs.size())
                    drawScreen(win, gc, font, tabs[active_tab]);
                break;
            }

            case ButtonPress:
            {
                XWindowAttributes wa;
                XGetWindowAttributes(dpy, win, &wa);
                auto tpos = draw_tabs(win, gc, font);

                int tab_index = -1;
                int hit = navbar_hit_test(event.xbutton.x, event.xbutton.y, tpos, &tab_index);

                switch (hit)
                {
                case -2: // "+" clicked
                {
                    add_tab("/");
                    draw_navbar(win, gc, wa.width);
                    draw_tabs(win, gc, font);
                    drawScreen(win, gc, font, tabs[active_tab]);
                    break;
                }
                case -3: // "×" close clicked
                {
                    if (tab_index >= 0 && tab_index < (int)tabs.size())
                    {
                        tabs.erase(tabs.begin() + tab_index);
                        if (active_tab >= (int)tabs.size())
                            active_tab = (int)tabs.size() - 1;
                        draw_navbar(win, gc, wa.width);
                        draw_tabs(win, gc, font);
                        if (!tabs.empty())
                            drawScreen(win, gc, font, tabs[active_tab]);
                    }
                    break;
                }
                default:
                {
                    if (hit >= 0 && hit < (int)tabs.size())
                    {
                        active_tab = hit;
                        draw_navbar(win, gc, wa.width);
                        draw_tabs(win, gc, font);
                        drawScreen(win, gc, font, tabs[active_tab]);
                        break;
                    }

                    // Click in content area: treat as scroll wheel if that's the button.
                    if (active_tab >= 0 && active_tab < (int)tabs.size())
                    {
                        TabState &T = tabs[active_tab];

                        int lineHeight = font->ascent + font->descent;
                        int visibleRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                        if (event.xbutton.button == Button4)
                        { // wheel up
                            T.scrollOffset = max(0, T.scrollOffset - SCROLL_STEP);
                            T.userScrolled = true;
                            drawScreen(win, gc, font, T);
                        }
                        else if (event.xbutton.button == Button5)
                        { // wheel down
                            int totalDisplayLines = drawScreen(win, gc, font, T);
                            T.scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                                 T.scrollOffset + SCROLL_STEP);
                            if (T.scrollOffset >= max(0, totalDisplayLines - visibleRows))
                                T.userScrolled = false;
                            drawScreen(win, gc, font, T);
                        }
                    }
                    break;
                }
                } // switch hit

                break;
            } // ButtonPress

            case MotionNotify:
            {
                // update hovered close / plus states
                hovered_close_tab = -1;
                hovered_plus = false;

                int mx = event.xmotion.x;
                auto tpos = draw_tabs(win, gc, font);
                hovered_tab_index = -1;
                for (size_t i = 0; i < tpos.size(); ++i)
                {
                    if (!tpos[i].is_plus &&
                        event.xmotion.x >= tpos[i].x &&
                        event.xmotion.x <= tpos[i].x + tpos[i].w)
                    {
                        hovered_tab_index = (int)i;
                        break;
                    }
                }

                for (size_t i = 0; i < tpos.size(); ++i)
                {
                    if (tpos[i].is_plus)
                    {
                        if (mx >= tpos[i].x && mx <= tpos[i].x + tpos[i].w)
                            hovered_plus = true;
                    }
                    else
                    {
                        if (mx >= tpos[i].close_x && mx <= tpos[i].close_x + tpos[i].close_w)
                            hovered_close_tab = i;
                    }
                }

                draw_tabs(win, gc, font); // redraw with hover state
                break;
            }

            case KeyPress:
            {
                if (!(active_tab >= 0 && active_tab < (int)tabs.size()))
                    break;

                TabState &T = tabs[active_tab];

                XWindowAttributes wa;
                XGetWindowAttributes(dpy, win, &wa);
                int lineHeight = font->ascent + font->descent;
                int visibleRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                bool isCtrl = (event.xkey.state & ControlMask);
                bool isShift = (event.xkey.state & ShiftMask);

                auto rebuildScreenBuffer = [&]()
                {
                    while (!T.screenBuffer.empty() && T.screenBuffer.back().rfind("swagnik@myterm:", 0) != 0)
                        T.screenBuffer.pop_back();
                    if (!T.screenBuffer.empty())
                        T.screenBuffer.pop_back();

                    string sdisp = formatPWD(T.cwd);
                    string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");

                    vector<string> parts;
                    size_t last = 0, p;
                    while ((p = T.input.find('\n', last)) != string::npos)
                    {
                        parts.push_back(T.input.substr(last, p - last));
                        last = p + 1;
                    }
                    parts.push_back(T.input.substr(last));

                    if (parts.empty())
                    {
                        T.screenBuffer.push_back(prompt);
                        return;
                    }

                    for (size_t i = 0; i < parts.size(); ++i)
                    {
                        if (i == 0)
                            T.screenBuffer.push_back(prompt + parts[i]);
                        else
                            T.screenBuffer.push_back(parts[i]);
                    }
                };

                // === SEARCH INPUT OVERRIDE ===
                // If in search mode, handle typing manually (bypass XIM status)
                if (T.isSearching && !(isCtrl || isShift))
                {
                    if (keysym >= XK_space && keysym <= XK_asciitilde)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)keysym);
                        T.currCursorPos++;
                        if (!T.screenBuffer.empty())
                            T.screenBuffer.back() += (char)keysym;
                        drawScreen(win, gc, font, T);
                        break;
                    }
                    if (keysym == XK_BackSpace)
                    {
                        if (T.currCursorPos > 0 && !T.input.empty())
                        {
                            T.input.erase(T.input.begin() + T.currCursorPos - 1);
                            T.currCursorPos--;
                            if (!T.screenBuffer.empty())
                                T.screenBuffer.back().pop_back();
                            drawScreen(win, gc, font, T);
                        }
                        break;
                    }
                    if (keysym == XK_Return || keysym == XK_KP_Enter)
                    {
                        string search_res = searchHistory(T.input);
                        T.input.clear();
                        string sdisp = formatPWD(T.cwd);
                        string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
                        if (search_res != "No match for search term in history")
                        {
                            T.input = search_res;
                            T.isMultLine = false;
                            for (char c : T.input)
                                if (c == '"')
                                    T.isMultLine = !T.isMultLine;
                            T.screenBuffer.push_back(prompt + T.input);
                            T.currCursorPos = (int)T.input.size();
                        }
                        else
                        {
                            T.screenBuffer.push_back(search_res);
                            T.screenBuffer.push_back(prompt + T.input);
                            T.currCursorPos = 0;
                        }
                        T.isSearching = false;
                        drawScreen(win, gc, font, T);
                        break;
                    }
                }

                // switch on keysym for keyboard handling
                switch (keysym)
                {
                case XK_Escape:
                {
                    // soft-exit: close current tab if >1, else exit
                    if (tabs.size() > 1)
                    {
                        tabs.erase(tabs.begin() + active_tab);
                        if (active_tab >= (int)tabs.size())
                            active_tab = (int)tabs.size() - 1;
                        draw_navbar(win, gc, wa.width);
                        auto tpos = draw_tabs(win, gc, font);
                        if (!tabs.empty())
                            drawScreen(win, gc, font, tabs[active_tab]);
                    }
                    else
                    {
                        // exit run()
                        if (xic)
                            XDestroyIC(xic);
                        if (xim)
                            XCloseIM(xim);
                        XUnmapWindow(dpy, win);
                        XDestroyWindow(dpy, win);
                        return;
                    }
                    break;
                }
                case XK_Up:
                {
                    if (T.isSearching || T.inRec)
                    {
                        // ignore
                    }
                    else if (!inputs.empty())
                    {
                        T.isMultLine = false;
                        if (T.inpIdx > 0)
                            T.inpIdx--;
                        else
                            T.inpIdx = 0;

                        T.input = inputs[T.inpIdx];
                        for (char c : T.input)
                            if (c == '"')
                                T.isMultLine = !T.isMultLine;
                        T.currCursorPos = (int)T.input.size();
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_Down:
                {
                    if (T.isSearching || T.inRec)
                    {
                        // ignore
                    }
                    else if (!inputs.empty())
                    {
                        T.isMultLine = false;
                        if (T.inpIdx < (int)inputs.size() - 1)
                        {
                            T.inpIdx++;
                            T.input = inputs[T.inpIdx];
                        }
                        else
                        {
                            T.inpIdx = (int)inputs.size();
                            T.input.clear();
                        }
                        for (char c : T.input)
                            if (c == '"')
                                T.isMultLine = !T.isMultLine;
                        T.currCursorPos = (int)T.input.size();
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_Left:
                {
                    if (T.currCursorPos > 0)
                        T.currCursorPos--;
                    drawScreen(win, gc, font, T);
                    break;
                }

                case XK_Right:
                {
                    if (T.currCursorPos < (int)T.input.size())
                        T.currCursorPos++;
                    drawScreen(win, gc, font, T);
                    break;
                }
                case XK_Page_Up:
                {
                    T.scrollOffset = max(0, T.scrollOffset - SCROLL_STEP * 5);
                    T.userScrolled = true;
                    drawScreen(win, gc, font, T);
                    break;
                }

                case XK_Page_Down:
                {
                    int totalDisplayLines = drawScreen(win, gc, font, T);
                    T.scrollOffset = min(max(0, totalDisplayLines - visibleRows), T.scrollOffset + SCROLL_STEP * 5);
                    if (T.scrollOffset >= max(0, totalDisplayLines - visibleRows))
                        T.userScrolled = false;
                    drawScreen(win, gc, font, T);
                    break;
                }

                case XK_Home:
                {
                    if (event.xkey.state & ControlMask)
                    {
                        T.scrollOffset = 0;
                        T.userScrolled = true;
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_End:
                {
                    if (event.xkey.state & ControlMask)
                    {
                        int totalDisplayLines = drawScreen(win, gc, font, T);
                        T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                        T.userScrolled = false;
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_v:
                case XK_V:
                {
                    if ((event.xkey.state & ControlMask))
                    {
                        XConvertSelection(dpy, XInternAtom(dpy, "CLIPBOARD", False),
                                          XInternAtom(dpy, "UTF8_STRING", False),
                                          XInternAtom(dpy, "PASTE_BUFFER", False),
                                          win, CurrentTime);
                        break;
                    }
                    [[fallthrough]];
                }

                case XK_a:
                case XK_A:
                {
                    if (T.inRec)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_A) ? 'A' : 'a'));
                        T.currCursorPos++;
                        T.screenBuffer.back().push_back((char)((keysym == XK_A) ? 'A' : 'a'));
                        drawScreen(win, gc, font, T);
                    }
                    else if (event.xkey.state & ControlMask)
                    {
                        if (T.isSearching)
                            T.currCursorPos = 0;
                        else
                            T.currCursorPos = T.count;
                        drawScreen(win, gc, font, T);
                    }
                    else
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_A) ? 'A' : 'a'));
                        T.currCursorPos++;
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_e:
                case XK_E:
                {
                    if (T.inRec)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_E) ? 'E' : 'e'));
                        T.currCursorPos++;
                        T.screenBuffer.back().push_back((char)((keysym == XK_E) ? 'E' : 'e'));
                        drawScreen(win, gc, font, T);
                    }
                    else if (event.xkey.state & ControlMask)
                    {
                        T.currCursorPos = (int)T.input.size();
                        drawScreen(win, gc, font, T);
                    }
                    else
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_E) ? 'E' : 'e'));
                        T.currCursorPos++;
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_r:
                case XK_R:
                {
                    if (T.inRec)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_R) ? 'R' : 'r'));
                        T.currCursorPos++;
                        T.screenBuffer.back().push_back((char)((keysym == XK_R) ? 'R' : 'r'));
                        drawScreen(win, gc, font, T);
                    }
                    else if (event.xkey.state & ControlMask)
                    {
                        T.screenBuffer.push_back("Enter search term:");
                        T.input.clear();
                        T.currCursorPos = 0;
                        T.isSearching = true;
                        drawScreen(win, gc, font, T);
                    }
                    else
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, (char)((keysym == XK_R) ? 'R' : 'r'));
                        T.currCursorPos++;
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    break;
                }

                case XK_Tab:
                {
                    if ((event.xkey.state & ControlMask) && !(event.xkey.state & ShiftMask))
                    {
                        if (!tabs.empty())
                        {
                            active_tab = (active_tab + 1) % tabs.size();
                            draw_navbar(win, gc, wa.width);
                            draw_tabs(win, gc, font);
                            drawScreen(win, gc, font, tabs[active_tab]);
                        }
                        break;
                    }

                    if (!(event.xkey.state & ControlMask))
                    {
                        if (!T.input.empty())
                        {
                            T.inRec = true;
                            T.query = getQuery(T.input);
                            if (T.query == T.input && T.query.rfind("./", 0) == 0)
                                T.query = T.query.substr(2);
                            T.forRec = T.input;

                            auto outputs = execCommandInDir("ls", T.cwd);
                            vector<string> candidates;
                            for (auto &l : outputs)
                                if (!l.empty() && l.rfind("ERROR:", 0) != 0)
                                    candidates.push_back(l);

                            T.recs = getRecomm(T.query, candidates);
                            if (T.recs.empty())
                            {
                                T.inRec = false;
                            }
                            else if (T.recs.size() == 1)
                            {
                                T.input += T.recs[0].substr(T.query.size());
                                if (!T.screenBuffer.empty())
                                    T.screenBuffer.back() += T.recs[0].substr(T.query.size());
                                T.inRec = false;
                                T.currCursorPos = (int)T.input.size();
                            }
                            else
                            {
                                for (size_t i = 0; i < T.recs.size(); i++)
                                    T.showRec += to_string(i + 1) + ". " + T.recs[i] + "  ";
                                T.screenBuffer.push_back(T.showRec);
                                T.screenBuffer.push_back("Choose from above options:");
                                T.input.clear();
                                T.currCursorPos = 0;
                                T.showRec.clear();
                            }
                            drawScreen(win, gc, font, T);
                        }
                        break;
                    }
                    break;
                }

                case XK_ISO_Left_Tab:
                {
                    if ((event.xkey.state & ControlMask) && (event.xkey.state & ShiftMask))
                    {
                        if (!tabs.empty())
                        {
                            active_tab = (active_tab - 1 + tabs.size()) % tabs.size();
                            draw_navbar(win, gc, wa.width);
                            draw_tabs(win, gc, font);
                            drawScreen(win, gc, font, tabs[active_tab]);
                        }
                    }
                    break;
                }

                case XK_c:
                case XK_C:
                {
                    if (event.xkey.state & ControlMask)
                    {
                        // Request stop from execute.cpp (async-safe)
                        notify_sigint_from_ui();

                        // Give watcher thread a short timeout to finish its cleanup & restore buffer.
                        // We poll mw_finished (set by execute.cpp when multiWatch ends).
                        for (int i = 0; i < 10; ++i)
                        {
                            if (mw_finished.load())
                                break;
                            this_thread::sleep_for(chrono::milliseconds(50));
                        }

                        // Reset stop flags so the next command can run normally.
                        mw_stop_requested.store(false);
                        mw_finished.store(false);
                        cmd_running.store(false);

                        // Append ^C and prompt to screenBuffer and redraw
                        TabState &T2 = tabs[active_tab];
                        T2.screenBuffer.push_back("^C");

                        string sdisp = formatPWD(T2.cwd);
                        string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
                        T2.screenBuffer.push_back(prompt);
                        T2.input.clear();
                        T2.currCursorPos = 0;

                        drawScreen(win, gc, font, T2);
                        break;
                    }
                    [[fallthrough]];
                }

                default:
                {
                    // Printable handling: only if XwcLookupString succeeded
                    if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                    {
                        // ENTER
                        if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                        {
                            // Recommendation selection
                            if (T.inRec)
                            {
                                int recIdx = min(getRecIdx(T.input), (int)T.recs.size()) - 1;
                                if (recIdx < 0)
                                    recIdx = 0;
                                string rec = T.recs[recIdx];
                                T.input = T.forRec + rec.substr(T.query.size());

                                string sdisp = formatPWD(T.cwd);
                                string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");

                                // remove the three pushed lines (list + "Choose..." + current line)
                                if (!T.screenBuffer.empty())
                                    T.screenBuffer.pop_back();
                                if (!T.screenBuffer.empty())
                                    T.screenBuffer.pop_back();
                                if (!T.screenBuffer.empty())
                                    T.screenBuffer.pop_back();

                                T.screenBuffer.push_back(prompt + T.input);
                                T.currCursorPos = (int)T.input.size();
                                T.inRec = false;
                                break;
                            }

                            // Search mode: use history search
                            if (T.isSearching)
                            {
                                string search_res = searchHistory(T.input);
                                T.input.clear();
                                string sdisp = formatPWD(T.cwd);
                                string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
                                if (search_res != "No match for search term in history")
                                {
                                    T.input = search_res;
                                    T.isMultLine = false;
                                    for (char c : T.input)
                                        if (c == '"')
                                            T.isMultLine = !T.isMultLine;
                                    T.screenBuffer.push_back(prompt + T.input);
                                    T.currCursorPos = (int)T.input.size();
                                }
                                else
                                {
                                    T.screenBuffer.push_back(search_res);
                                    T.screenBuffer.push_back(prompt + T.input);
                                    T.currCursorPos = 0;
                                }
                                T.isSearching = false;
                                drawScreen(win, gc, font, T);
                                break;
                            }

                            // Multiline handling (inside quotes)
                            if (T.isMultLine)
                            {
                                T.input.insert(T.input.begin() + T.currCursorPos, '\n');
                                T.currCursorPos++;
                                T.count = (int)T.input.size();
                                rebuildScreenBuffer();
                                drawScreen(win, gc, font, T);
                                break;
                            }
                            else
                            {
                                // Normal command execution flow
                                if (!T.input.empty())
                                {
                                    if (inputs.empty() || inputs.back() != T.input)
                                    {
                                        storeInput(T.input);
                                        inputs.push_back(T.input);
                                    }
                                }
                                T.count = 0;
                                T.inpIdx = (int)inputs.size();

                                auto trimLocal = [](const string &s) -> string
                                {
                                    size_t a = 0, b = s.size();
                                    while (a < b && isspace((unsigned char)s[a]))
                                        ++a;
                                    while (b > a && isspace((unsigned char)s[b - 1]))
                                        --b;
                                    return s.substr(a, b - a);
                                };
                                string trimmed = trimLocal(T.input);

                                // Built-in history command displays stored history file.
                                if (trimmed == "history")
                                {
                                    ifstream in(FILENAME);
                                    if (!in)
                                    {
                                        cerr << "Error: cannot open file\n";
                                    }
                                    else
                                    {
                                        string line;
                                        while (getline(in, line))
                                            T.screenBuffer.push_back(line);
                                        in.close();
                                    }
                                }

                                // Built-in clear command
                                if (trimmed == "clear")
                                {
                                    T.screenBuffer.clear();
                                    string sdisp = formatPWD(T.cwd);
                                    string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
                                    T.screenBuffer.push_back(prompt);
                                    T.input.clear();
                                    T.currCursorPos = 0;
                                    T.isMultLine = false;
                                    int totalDisplayLines = drawScreen(win, gc, font, T);
                                    T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                                    T.userScrolled = false;
                                    drawScreen(win, gc, font, T);
                                    break;
                                }

                                // multiWatch: parse commands inside [ ... ] and spawn thread
                                if (trimmed.rfind("multiWatch", 0) == 0)
                                {
                                    size_t start = T.input.find('[');
                                    size_t end = T.input.find(']');
                                    if (start != string::npos && end != string::npos && end > start)
                                    {
                                        string inside = T.input.substr(start + 1, end - start - 1);
                                        vector<string> cmds;
                                        regex r("\"([^\"]+)\"");
                                        smatch m;
                                        string::const_iterator s(inside.cbegin());
                                        while (regex_search(s, inside.cend(), m, r))
                                        {
                                            cmds.push_back(m[1]);
                                            s = m.suffix().first;
                                        }

                                        if (cmds.empty())
                                        {
                                            T.screenBuffer.push_back("multiWatch: No valid commands found.");
                                        }
                                        else
                                        {
                                            // ✅ Copy old screen buffer safely
                                            vector<string> oldBuffer;
                                            oldBuffer.insert(oldBuffer.end(), T.screenBuffer.begin(), T.screenBuffer.end());

                                            // Clear screen for watch mode
                                            T.screenBuffer.clear();
                                            T.screenBuffer.push_back("multiWatch — starting...");

                                            // Mark multiwatch active
                                            mw_stop_requested.store(false);
                                            mw_finished.store(false);

                                            // ✅ Pass oldBuffer to thread so it can restore later
                                            thread([cmds, tab_index = active_tab, oldBuffer]()
                                                   { multiWatchThreaded_using_pipes(cmds, tab_index, oldBuffer); })
                                                .detach();
                                        }
                                    }
                                    else
                                    {
                                        T.screenBuffer.push_back("Usage: multiWatch [\"cmd1\", \"cmd2\", ...]");
                                    }

                                    // Clear input for next command
                                    T.input.clear();
                                    T.currCursorPos = 0;
                                    break;
                                }

                                // execute in tab cwd
                                vector<string> outputs = execCommandInDir(T.input, T.cwd);
                                T.input.clear();
                                T.currCursorPos = 0;

                                bool isMultiWatch = (T.input.find("multiWatch") != string::npos);

                                // push command output lines
                                for (const auto &line : outputs)
                                    T.screenBuffer.push_back(line);

                                // show prompt if not multiWatch
                                if (!isMultiWatch)
                                {
                                    string sdisp = formatPWD(T.cwd);
                                    string prompt = (sdisp == "/") ? ("swagnik@myterm:" + sdisp + "$ ") : ("swagnik@myterm:~" + sdisp + "$ ");
                                    T.screenBuffer.push_back(prompt);
                                }

                                int totalDisplayLines = drawScreen(win, gc, font, T);
                                if (!T.userScrolled)
                                {
                                    T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                                    drawScreen(win, gc, font, T);
                                }
                                break;
                            } // end else not multiline
                        } // end ENTER handling

                        // BACKSPACE and printable handling below...
                        char ch = (char)wbuf[0];
                        if (T.inRec)
                        {
                            if (T.screenBuffer.empty() ||
                                (T.screenBuffer.back().rfind("swagnik@myterm:", 0) != 0 &&
                                 T.screenBuffer.back().find("Choose from") == string::npos))
                            {
                                string sdisp = formatPWD(T.cwd);
                                string prompt = (sdisp == "/")
                                                    ? ("swagnik@myterm:" + sdisp + "$ ")
                                                    : ("swagnik@myterm:~" + sdisp + "$ ");
                                T.screenBuffer.push_back(prompt);
                            }

                            T.input.insert(T.input.begin() + T.currCursorPos, ch);
                            T.currCursorPos++;
                            if (!T.screenBuffer.empty())
                                T.screenBuffer.back() += ch;

                            drawScreen(win, gc, font, T);
                            break;
                        }

                        if (T.isSearching)
                        {
                            T.input.insert(T.input.begin() + T.currCursorPos, ch);
                            T.currCursorPos++;
                            if (!T.screenBuffer.empty())
                                T.screenBuffer.back() += ch;
                            drawScreen(win, gc, font, T);
                            break;
                        }
                        if (isprint((unsigned char)ch) || ch == '\t')
                        {
                            if (ch == '"')
                                T.isMultLine = !T.isMultLine;
                            T.input.insert(T.input.begin() + T.currCursorPos, ch);
                            T.currCursorPos++;
                            rebuildScreenBuffer();
                            drawScreen(win, gc, font, T);
                            break;
                        }
                        if (wbuf[0] == 8 || wbuf[0] == 127)
                        {
                            if ((T.isSearching || T.inRec) && !T.input.empty())
                            {
                                T.input.erase(T.input.begin() + T.currCursorPos - 1);
                                T.currCursorPos--;
                                if (!T.screenBuffer.empty() && !T.screenBuffer.back().empty())
                                    T.screenBuffer.back().pop_back();
                                drawScreen(win, gc, font, T);
                                break;
                            }

                            if (T.currCursorPos > 0)
                            {
                                if (T.input[T.currCursorPos - 1] == '"')
                                    T.isMultLine = !T.isMultLine;
                                T.input.erase(T.input.begin() + T.currCursorPos - 1);
                                T.currCursorPos--;
                                rebuildScreenBuffer();
                                drawScreen(win, gc, font, T);
                            }
                            break;
                        }
                    } // end if lookupchars
                    break;
                } // end default printable

                } // end switch(keysym)

                break;
            } // KeyPress

            case SelectionNotify:
            {
                if (!(active_tab >= 0 && active_tab < (int)tabs.size()))
                    break;
                TabState &T = tabs[active_tab];

                if (event.xselection.selection == XInternAtom(dpy, "CLIPBOARD", False))
                {
                    Atom type;
                    int format;
                    unsigned long nitems, bytes_after;
                    unsigned char *data = nullptr;

                    XGetWindowProperty(dpy, win,
                                       XInternAtom(dpy, "PASTE_BUFFER", False),
                                       0, (~0L), True, AnyPropertyType,
                                       &type, &format, &nitems, &bytes_after, &data);

                    if (data)
                    {
                        std::string clipText((char *)data, nitems);
                        XFree(data);

                        std::istringstream ss(clipText);
                        std::string line;
                        bool first = true;

                        while (std::getline(ss, line))
                        {
                            if (first)
                            {
                                if (!T.screenBuffer.empty())
                                    T.screenBuffer.back() += line;
                                T.input += line;
                                first = false;
                            }
                            else
                            {
                                T.screenBuffer.push_back(line);
                                T.input += '\n';
                                T.input += line;
                            }
                        }
                        T.currCursorPos = (int)T.input.size();
                        drawScreen(win, gc, font, T);
                    }
                }
                break;
            }

            default:
            {
                // Unhandled event types are ignored (no-op).
                break;
            }
            } // end switch(event.type)
        } // end XPending loop

        // ===== multiWatch finished handling =====
        if (mw_finished.load())
        {
            // execute.cpp is expected to have restored the tab's screenBuffer.
            if (active_tab >= 0 && active_tab < (int)tabs.size())
            {
                drawScreen(win, gc, font, tabs[active_tab]);
            }
            // reset signals so next multiWatch can be started normally
            mw_finished.store(false);
            mw_stop_requested.store(false);
            cmd_running.store(false);
        }

        // blink active tab cursor only
        if (active_tab >= 0 && active_tab < (int)tabs.size())
        {
            TabState &T = tabs[active_tab];
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::milliseconds>(now - T.lastBlink).count() > 500)
            {
                T.showCursor = !T.showCursor;
                T.lastBlink = now;
                drawScreen(win, gc, font, T);
            }
        }

        // tiny sleep
        this_thread::sleep_for(chrono::milliseconds(20));
    } // end main while

    // cleanup (not typically reached)
    if (xic)
        XDestroyIC(xic);
    if (xim)
        XCloseIM(xim);

    XUnmapWindow(dpy, win);
    XDestroyWindow(dpy, win);
    return;
}
