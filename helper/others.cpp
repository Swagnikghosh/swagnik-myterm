#include<iostream>
#include<iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include<cctype>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/poll.h>
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
using namespace std;
int len=0;

string getPWD()
{
     char cwd[512];
    string currentDir;
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
        currentDir = string(cwd);
    if (int(currentDir.size()) < len)
        return currentDir;
    return currentDir.substr(len);
}
// format per-tab CWD the same way
static string formatPWD(const string &cwd)
{
  if (int(cwd.size()) < len) return cwd;
    return cwd.substr(len);
}


string stripQuotes(const string &s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}