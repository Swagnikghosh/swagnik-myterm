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

vector<string> getRecomm(string query, vector<string> list)
{
    vector<string> recs;
    for (auto &ele : list)
        if (ele.rfind(query, 0) == 0)
            recs.push_back(ele);
    return recs;
}
int getRecIdx(string inp)
{
    string recIdx = "";
    for (auto c : inp) if (c >= '0' && c <= '9') recIdx += c;
    return !recIdx.empty() ? stoi(recIdx) : 1;
}

string getQuery(string input)
{
    string query = "";
    for (auto c : input)
    {
        if (c == ' ') { query = ""; continue; }
        query += c;
    }
    return query;
}
