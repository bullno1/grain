#ifndef GRAINC_COMPILER_HPP
#define GRAINC_COMPILER_HPP

struct Compiler;

struct CompileTask;

class ILogStream
{
public:
	virtual void write(const char* msg) = 0;
};

Compiler* createCompiler(ILogStream* logStream);
void destroyCompiler(Compiler* compiler);

bool compile(Compiler* compiler, CompileTask* task);

CompileTask* createCompileTask();
void destroyCompileTask(CompileTask* task);

void setOptimize(CompileTask* task, bool optimize);
void setOutput(CompileTask* task, const char* filename);
void addInput(CompileTask* task, const char* filename);
void addIncludePath(CompileTask* task, const char* path);

#endif
