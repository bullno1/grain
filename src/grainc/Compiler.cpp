#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <map>
#include <glsl_optimizer.h>
#include "grainc.hpp"
#include "CompileTask.hpp"
#include "Script.hpp"
#include "Logger.hpp"

using namespace std;

namespace
{

class Ostream: public ILogStream
{
public:
	Ostream(ostream& stream)
		:mStream(stream)
	{}

	void write(const char* msg)
	{
		mStream << msg << endl;
	}
private:
	ostream& mStream;
};

Ostream gCerr(cerr);

typedef map<string, Script> ScriptCache;

string str(size_t num)
{
	stringstream ss;
	ss << num;
	return ss.str();
}

struct CompileContext
{
	CompileContext(const Script::Declarations& sysAttrs)
		:mAttributes(sysAttrs)
	{
		mDeclParamList = "(inout int _count";
		mInvokeParamList = "(_count";
		size_t numFloats = 0;

		for(Script::Declarations::const_iterator itr = sysAttrs.begin(); itr != sysAttrs.end(); ++itr)
		{
			// calculate attributes' locations
			mAttributeMap.insert(make_pair(itr->first, numFloats));
			numFloats += DataType::size(itr->second);

			// generate parameter list
			mDeclParamList += ", inout ";
			mDeclParamList += DataType::name(itr->second);
			mDeclParamList += ' ';
			mDeclParamList += itr->first;

			mInvokeParamList += ", ";
			mInvokeParamList += itr->first;
		}

		mDeclParamList += ')';
		mInvokeParamList += ')';

		// generate sampler declarations
		mNumTextures = (numFloats + 3) / 4;//a texel has 4 fields: a, r, g, b
		for(size_t i = 0; i < mNumTextures; ++i)
		{
			mSamplerDeclarations += "uniform sampler2D _tex";
			mSamplerDeclarations += str(i);
			mSamplerDeclarations += ";\n";

			mOutputDeclarations += "out vec4 out";
			mOutputDeclarations += str(i);
			mOutputDeclarations += ";\n";
		}
	}

	typedef map<string, size_t> AttibuteMap;

	AttibuteMap mAttributeMap;
	Script::Declarations mAttributes;
	string mDeclParamList;
	string mInvokeParamList;
	string mSamplerDeclarations;
	string mOutputDeclarations;
	size_t mNumTextures;
};

const char gFieldNames[] = { 'x', 'y', 'z', 'w' };

}

extern "C" const char builtins[];
extern "C" const size_t builtins_len;

struct Compiler
{
	glslopt_ctx* mGlslOptCtx;
	ILogStream* mLogStream;
};

Compiler* createCompiler(ILogStream* logStream)
{
	Compiler* compiler = new Compiler;
	compiler->mLogStream = logStream != NULL ? logStream : &gCerr;
	compiler->mGlslOptCtx = glslopt_initialize(kGlslTargetOpenGL);
	return compiler;
}

void destroyCompiler(Compiler* compiler)
{
	glslopt_cleanup(compiler->mGlslOptCtx);
	delete compiler;
}

// Dirty hack to avoid forward decl
struct CompilerImpl
{

static bool compile(Compiler* compiler, CompileTask* task)
{
	size_t numScripts = task->mInputs.size();
	ILogStream* logStream = compiler->mLogStream;
	vector<Script*> rootScripts;
	ScriptCache emitterCache;
	ScriptCache affectorCache;
	ScriptCache vshCache;
	ScriptCache fshCache;
	string scriptName;
	ScriptType::Enum scriptType;

	//load all scripts
	string filename;
	for(size_t i = 0; i < numScripts; ++i)
	{
		filename = task->mInputs[i];

		if(!Script::parseFilename(filename, scriptName, scriptType))
		{
			Logger(logStream) << "Can't determine script type of '" << filename << '\'';
			return false;
		}

		ScriptCache* cache;
		switch(scriptType)
		{
			case ScriptType::Affector:
				cache = &affectorCache;
				break;
			case ScriptType::Emitter:
				cache = &emitterCache;
				break;
			case ScriptType::VertexShader:
				cache = &vshCache;
				break;
			case ScriptType::FragmentShader:
				cache = &fshCache;
				break;
		}

		Script& script = (*cache)[scriptName];
		if(!script.read(filename, logStream)) { return false; }

		script.mType = scriptType;
		script.mName = scriptName;
		rootScripts.push_back(&script);
	}

	if(!loadDependencies(emitterCache, ScriptType::Emitter, task->mIncludePaths, logStream))
	{
		return false;
	}

	if(!loadDependencies(affectorCache, ScriptType::Affector, task->mIncludePaths, logStream))
	{
		return false;
	}

	// Determine the common set of attributes
	Script::Declarations sysAttrs;
	if(!(collectAttributes(sysAttrs, emitterCache, logStream)
	     && collectAttributes(sysAttrs, affectorCache, logStream)))
	{
		return false;
	}
	sysAttrs.insert(make_pair("life", DataType::Float));//life is a built-in attribute

	CompileContext compileContext(sysAttrs);

	// Compile all scripts
	bool compileResult =
		   compileModifiers(compileContext, emitterCache, logStream)
		&& compileModifiers(compileContext, affectorCache, logStream)
		&& compileRenderShaders(compileContext, vshCache, logStream)
		&& compileRenderShaders(compileContext, fshCache, logStream);

	if(!compileResult) { return false; }

	ofstream outFile(task->mOutput);
	if(!outFile.good())
	{
		Logger(logStream) << "Can't open '" << task->mOutput << "' for writing";
		return false;
	}
	outFile << compileContext.mNumTextures << endl;

	// Link scripts
	string code;

	for(size_t i = 0; i < numScripts; ++i)
	{
		Script& script = *rootScripts[i];
		code.clear();

		ScriptCache* cache = NULL;
		switch(script.mType)
		{
			case ScriptType::Affector:
				cache = &affectorCache;
				break;
			case ScriptType::Emitter:
				cache = &emitterCache;
				break;
			case ScriptType::VertexShader:
				cache = &vshCache;
				break;
			case ScriptType::FragmentShader:
				cache = &fshCache;
				break;
		}

		switch(script.mType)
		{
			case ScriptType::Emitter:
			case ScriptType::Affector:
				linkModifiers(compileContext, script, *cache, code);
				break;
			case ScriptType::VertexShader:
			case ScriptType::FragmentShader:
				linkRenderShaders(compileContext, script, code);
				break;
		}

		bool isVertexShader = script.mType == ScriptType::VertexShader;
		glslopt_shader* shader = glslopt_optimize(compiler->mGlslOptCtx, isVertexShader ? kGlslOptShaderVertex : kGlslOptShaderFragment, code.c_str(), 0);
		bool status = glslopt_get_status(shader);
		if(status)
		{
			outFile << "@" << script.mName;
			switch(script.mType)
			{
				case ScriptType::Emitter:
					outFile << ".emitter";
					break;
				case ScriptType::Affector:
					outFile << ".affector";
					break;
				case ScriptType::VertexShader:
					outFile << ".vsh";
					break;
				case ScriptType::FragmentShader:
					outFile << ".fsh";
					break;
			}
			outFile << endl;
			outFile << (task->mOptimize ? glslopt_get_output(shader) : code.c_str());
		}
		else
		{
			Logger(logStream) << glslopt_get_log(shader);
			return false;
		}

		glslopt_shader_delete(shader);
	}

	return true;
}

static bool loadDependencies(
	ScriptCache& cache,
	ScriptType::Enum scriptType,
	const vector<const char*>& searchPaths,
	ILogStream* logStream
)
{
	vector<string> pendingScripts;
	for(ScriptCache::const_iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		pendingScripts.push_back(itr->first);
	}

	string scriptName;
	string filename;
	while(!pendingScripts.empty())
	{
		scriptName = pendingScripts.back();
		pendingScripts.pop_back();

		const ScriptCache::const_iterator itr = cache.find(scriptName);
		const Script* script;
		if(itr == cache.end())//script is not loaded
		{
			if(!findScript(searchPaths, scriptName, scriptType, filename))
			{
				Logger(logStream) << "Cannot find script '" << scriptName << '\'';
				return false;
			}

			Script* newScript = &cache[scriptName];
			if(!newScript->read(filename, logStream)) { return false; }

			newScript->mType = scriptType;
			newScript->mName = scriptName;
		}
		else
		{
			script = &(itr->second);
		}

		const vector<string>& deps = script->mDependencies;
		for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
		{
			if(cache.find(*itr) == cache.end())//dependency is not loaded
			{
				pendingScripts.push_back(*itr);//load it later
			}
		}
	}

	return true;
}

static bool findScript(
	const vector<const char*>& searchPaths,
	const string& scriptName,
	ScriptType::Enum scriptType,
	string& fullPath
)
{
	string filename = scriptName;
	switch(scriptType)
	{
		case ScriptType::Affector:
			filename += ".affector";
			break;
		case ScriptType::Emitter:
			filename += ".emitter";
			break;
		case ScriptType::VertexShader:
			filename += ".vsh";
			break;
		case ScriptType::FragmentShader:
			filename += ".fsh";
			break;
	}

	if(fileExists(filename))
	{
		fullPath = filename;
		return true;
	}

	for(vector<const char*>::const_iterator itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
	{
		fullPath = *itr;
		fullPath += '/';
		fullPath += filename;
		if(fileExists(fullPath))
		{
			return true;
		}
	}

	return false;
}

static bool fileExists(const string& filename)
{
	ifstream file(filename.c_str());
	return file.good();
}

//TODO: handle conflicts
static bool collectAttributes(Script::Declarations& sysAttrs, ScriptCache& cache, ILogStream* logStream)
{
	for(ScriptCache::const_iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		const Script::Declarations& attributes = itr->second.mAttributes;
		for(Script::Declarations::const_iterator itr2 = attributes.begin(); itr2 != attributes.end(); ++itr2)
		{
			sysAttrs.insert(make_pair(itr2->first, itr2->second));
		}
	}

	return true;
}

static bool compileModifiers(
	const CompileContext& ctx,
	ScriptCache& cache,
	ILogStream* logStream
)
{
	for(ScriptCache::iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		Script& script = itr->second;
		string& code = script.mGeneratedCode;

		// signature
		code = "void ";
		code += script.mName;
		code += ctx.mDeclParamList;
		code += " {\n";

		// invoke dependencies
		const vector<string>& deps = script.mDependencies;
		for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
		{
			code += *itr;
			code +=	ctx.mInvokeParamList;
			code += ";\n";
		}

		// add own code
		// TODO: #line
		code += script.mBody;

		code += "\n}\n";
	}

	return true;
}

static bool compileRenderShaders(
	const CompileContext& ctx,
	ScriptCache& cache,
	ILogStream* logStream
)
{
	for(ScriptCache::iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		Script& script = itr->second;
		string& code = script.mGeneratedCode;

		code = "void main() {\n";
		generateFetch(ctx, script, code);
		code += script.mBody;
		code += "\n}\n";
	}

	return true;
}

static void generateFetch(const CompileContext& ctx, const Script& script, string& code)
{
	bool isEmitter = script.mType == ScriptType::Emitter;
	bool isVertex = script.mType == ScriptType::VertexShader;

	// Calculate texture coordinate
	if(isVertex)
	{
		// TODO: use textureSize
		code += "ivec2 _size = ivec2(_texWidth, _texHeight);\n"
		        "ivec2 _texCoord = ivec2(gl_InstanceID % _size.x, gl_InstanceID / _size.y);\n";
	}
	else
	{
		code += "ivec2 _texCoord = ivec2(gl_FragCoord.xy);\n";
	}

	// Fetch texels
	for(size_t i = 0; i < ctx.mNumTextures; ++i)
	{
		code += "vec4 stream";
		code += str(i);
		code += " = texelFetch(_tex";
		code += str(i);
		code += ", _texCoord, 0);\n";
	}

	string prefix = isEmitter ? "previous_" : "";
	for(Script::Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
	{
		DataType::Enum attrType = itr->second;
		code += DataType::name(attrType);
		code += ' ';
		code += prefix;
		code += itr->first;
		code += " = ";
		code += DataType::name(attrType);
		code += '(';
		bool first = true;

		size_t size = DataType::size(attrType);
		for(size_t count = 0; count < size; ++count)
		{
			if(first)
			{
				first = false;
			}
			else
			{
				code += ", ";
			}

			size_t attrLoc = ctx.mAttributeMap.find(itr->first)->second;
			code += "stream";
			code += str((attrLoc + count) / 4);
			code += '.';
			code += gFieldNames[(attrLoc + count) % 4];
		}
		code += ");\n";
	}
}

static void linkModifiers(
	const CompileContext& ctx,
	const Script& script,
	const ScriptCache& cache,
	std::string& code
)
{
	// emitter is trickier with temporary storage
	code = "#version 140\n"
	       "uniform float _time;\n"
	       "uniform float _chance;\n"
           "uniform float dt;\n";
	code += ctx.mSamplerDeclarations;
	code += ctx.mOutputDeclarations;

	// gather dependencies
	vector<const Script*> deps;
	collectDependencies(script, deps, cache);

	// generate uniform declarations
	// TODO: check for conflicts
	Script::Declarations uniforms;

	for(vector<const Script*>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		const Script::Declarations& params = (*itr)->mParams;
		uniforms.insert(params.begin(), params.end());
	}

	for(Script::Declarations::const_iterator itr = uniforms.begin(); itr != uniforms.end(); ++itr)
	{
		code += "uniform ";
		code += DataType::name(itr->second);
		code += ' ';
		code += itr->first;
		code += ";\n";
	}

	// append custom declarations
	code += script.mCustomDeclarations;

	// add builtin functions
	code.append(builtins, builtins_len);

	// add dependencies
	for(vector<const Script*>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		code += (*itr)->mGeneratedCode;
		code += '\n';
	}

	// create main function
	code += "void main()  {\n"
	        "int _count = 0;\n";

	generateFetch(ctx, script, code);

	bool isEmitter = script.mType == ScriptType::Emitter;

	if(isEmitter)
	{
		// gen temporary vars
		for(Script::Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
		{
			code += DataType::name(itr->second);
			code += ' ';
			code += itr->first;
			code += ";\n";
		}
	}

	// invoke main script
	code += script.mName;
	code +=	ctx.mInvokeParamList;
	code += ";\n";

	if(isEmitter)
	{
		// randomly select
		code += "bool canEmit = rand() <= _chance;\n"
		        "bool dead = previous_life <= 0.0;\n"
		        "float selected = float(dead && canEmit);\n";
		for(Script::Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
		{
			code += itr->first;
			code += " = mix(previous_";
			code += itr->first;
			code += ", ";
			code += itr->first;
			code += ", selected);\n";
		}
	}

	// store
	for(Script::Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
	{
		size_t attrLoc = ctx.mAttributeMap.find(itr->first)->second;
		size_t size = DataType::size(itr->second);

		if(size > 1)
		{
			for(int j = 0; j < size; ++j)
			{
				code += "out";
				code += str((attrLoc + j) / 4);
				code += '.';
				code += gFieldNames[(attrLoc + j) % 4];
				code += " = ";
				code += itr->first;
				code += '.';
				code += gFieldNames[j];
				code += ";\n";
			}
		}
		else
		{
			code += "out";
			code += str(attrLoc / 4);
			code += '.';
			code +=	gFieldNames[attrLoc % 4];
			code += " = ";
			code += itr->first;
			code += ";\n";
		}
	}

	code += "}\n";
}

static void collectDependencies(
	const Script& script,
	vector<const Script*>& sortedDeps,
	const ScriptCache& cache
)
{
	// ignore if script is already added
	for(vector<const Script*>::const_iterator itr = sortedDeps.begin(); itr != sortedDeps.end(); ++itr)
	{
		if(*itr == &script) { return; }
	}

	// add dependencies recursively
	const vector<string>& deps = script.mDependencies;
	for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		collectDependencies(cache.find(*itr)->second, sortedDeps, cache);
	}

	// add the script
	sortedDeps.push_back(&script);
}

static void linkRenderShaders(
	const CompileContext& ctx,
	const Script& script,
	std::string& code
)
{
	code = "#version 140\n"
	       "uniform int _texWidth;\n"
	       "uniform int _texHeight;\n";
	code += ctx.mSamplerDeclarations;
	code += script.mCustomDeclarations;
	code += script.mGeneratedCode;
}

};

bool compile(Compiler* compiler, CompileTask* task)
{
	return CompilerImpl::compile(compiler, task);
}
