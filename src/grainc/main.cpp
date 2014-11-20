#include <iostream>
#include <iomanip>
#include <cstring>
#include "grainc.hpp"

using namespace std;

int main(int argc, const char* const argv[])
{
	if(argc < 2)
	{
		cout << "Usage: grainc [options] file..." << endl
		     << "Options:" << endl
		     << left << setw(20) << "-o <output>"  << "Set output file name" << endl
		     << left << setw(20) << "-O"           << "Optimize generated code" << endl;
		return 1;
	}

	CompileOptions opts;

	opts.mOutput = "a.out";
	opts.mOptimize = false;

	for(int i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "-O") == 0)
		{
			opts.mOptimize = true;
		}
		else if(strcmp(argv[i], "-o") == 0 && (++i < argc))
		{
			opts.mOutput = argv[i];
		}
		else if(strcmp(argv[i], "-I") == 0 && (++i < argc))
		{
			opts.mIncludePaths.push_back(argv[i]);
		}
		else
		{
			opts.mInputs.push_back(argv[i]);
		}
	}

	return compile(opts);
}
