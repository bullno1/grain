#include "CompileTask.hpp"

CompileTask* createCompileTask()
{
	CompileTask* task = new CompileTask;
	task->mOptimize = false;
	task->mOutput = "a.out";
	return task;
}

void destroyCompileTask(CompileTask* task)
{
	delete task;
}

void setOptimize(CompileTask* task, bool optimize)
{
	task->mOptimize = optimize;
}

void setOutput(CompileTask* task, const char* filename)
{
	task->mOutput = filename;
}

void addInput(CompileTask* task, const char* filename)
{
	task->mInputs.push_back(filename);
}

void addIncludePath(CompileTask* task, const char* path)
{
	task->mIncludePaths.push_back(path);
}

