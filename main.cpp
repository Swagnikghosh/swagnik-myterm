#include <iostream>
#include "run.cpp"
using namespace std;
int main()
{
    string home_path;


    // For Linux/macOS systems
    const char *home_env = std::getenv("HOME");
    if (home_env)
        home_path = home_env;


    if (home_path.empty())
    {
        cerr << "❌ Could not determine home directory." << std::endl;
        return 1;
    }

    // Store length of the path in an int variable
    len = static_cast<int>(home_path.length());
    run();
    return 0;
}