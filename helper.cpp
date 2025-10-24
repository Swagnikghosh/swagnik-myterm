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
const string FILENAME = "./input_log.txt";
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



int commonPrefixLength(const string &a, const string &b)
{
    int len = min(a.size(), b.size());
    for (int i = 0; i < len; ++i)
        if (a[i] != b[i])
            return i;
    return len;
}
int getLastHistoryNumber()
{
    ifstream in(FILENAME);
    if (!in)
        return 0;

    string line; int lastNum = 0;
    while (getline(in, line))
    {
        istringstream iss(line);
        int num;
        if (iss >> num) lastNum = max(lastNum, num);
    }
    in.close();
    return lastNum;
}
string searchHistory(const string &input, const string &filename = FILENAME)
{
    ifstream in(filename);
    if (!in)
        return "No match for search term in history";

    vector<string> history;
    string line;

    while (getline(in, line))
    {
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos) history.push_back(line.substr(pos));
        else history.push_back("");
    }
    in.close();

    string exactMatch = "";
    vector<string> candidates;
    int maxPrefixLen = 0;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        string cmd = *it;

        if (cmd == input)
        {
            exactMatch = cmd;
            break;
        }
        int prefixLen = commonPrefixLength(cmd, input);
        if (prefixLen > maxPrefixLen)
        {
            maxPrefixLen = prefixLen;
            candidates.clear();
            candidates.push_back(cmd);
        }
        else if (prefixLen == maxPrefixLen)
        {
            candidates.push_back(cmd);
        }
    }

    if (!exactMatch.empty())
        return exactMatch;

    if (maxPrefixLen >= 2)
        return candidates[0];

    return "No match for search term in history";
}


vector<string> loadInputs()
{
    ifstream in(FILENAME);
    vector<string> inputs;
    if (!in) return inputs;

    string line;
    while (getline(in, line))
    {
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos)
            inputs.push_back(line.substr(pos));
        else
            inputs.push_back("");
    }

    in.close();
    return inputs;
}
void storeInput(const string &input)
{
    int histNum = getLastHistoryNumber() + 1;
    ofstream out(FILENAME, ios::app);
    if (!out)
    {
        cerr << "Error opening file for writing.\n";
        return;
    }
    out << "  " << histNum << "  " << input << endl;
    out.close();
}



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
string stripQuotes(const string &s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}