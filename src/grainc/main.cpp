#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
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

	Compiler* compiler = createCompiler(NULL);
	CompileTask* task = createCompileTask();

	setOutput(task, "a.out");
	setOptimize(task, false);

	for(int i = 1; i < argc; ++i)
	{
		if(strcmp(argv[i], "-O") == 0)
		{
			setOptimize(task, true);
		}
		else if(strcmp(argv[i], "-o") == 0 && (++i < argc))
		{
			setOutput(task, argv[i]);
		}
		else if(strcmp(argv[i], "-I") == 0 && (++i < argc))
		{
			addIncludePath(task, argv[i]);
		}
		else
		{
			addInput(task, argv[i]);
		}
	}

	bool success = compile(compiler, task);
	destroyCompileTask(task);
	destroyCompiler(compiler);

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
