#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <map>
#include <glsl_optimizer.h>
#include "grainc.hpp"
#include "CompileTask.hpp"
#include "Script.hpp"
#include "SourceMap.hpp"
#include "builtins.h"
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

template<typename TargetType, typename SourceType>
TargetType lexical_cast(const SourceType& source)
{
	stringstream ss;
	ss << source;
	TargetType result;
	ss >> result;
	return result;
}

string str(size_t num)
{
	stringstream ss;
	ss << num;
	return ss.str();
}

struct CompileContext
{
	CompileContext(const Compiler* compiler, const CompileTask* compileTask)
		:mCompiler(*compiler)
		,mCompileTask(*compileTask)
		,mDeclHelper(mAttributes)
	{
		stringstream ss;
		ss.write(builtins, builtins_len);
		unsigned int lineCount = 0;
		string line;
		while(getline(ss, line)) { ++lineCount; };
		mBuiltInStartLine = mSourceMap.addMapping("<builtins.glsl>", 1, lineCount);
	}

	typedef map<string, size_t> AttibuteMap;

	SourceMap mSourceMap;
	const Compiler& mCompiler;
	const CompileTask& mCompileTask;
	AttibuteMap mAttributeMap;
	Declarations mAttributes;
	DeclarationHelper mDeclHelper;
	unsigned int mBuiltInStartLine;
	string mSamplerDeclarations;
	string mStructDeclaration;
	string mOutputDeclarations;
	size_t mNumTextures;
};

const char gFieldNames[] = { 'x', 'y', 'z', 'w' };

}

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
	CompileContext compileCtx(compiler, task);
	ILogStream* logStream = compiler->mLogStream;
	vector<Script*> rootScripts;
	ScriptCache emitterCache;
	ScriptCache affectorCache;
	ScriptCache vshCache;
	ScriptCache fshCache;
	string scriptName;

	//load all scripts
	string filename;
	size_t numScripts = task->mInputs.size();
	for(size_t i = 0; i < numScripts; ++i)
	{
		filename = task->mInputs[i];

		ScriptType::Enum scriptType;
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

		script.mGeneratedCodeStartLine =
			compileCtx.mSourceMap.addMapping(filename, script.mFirstBodyLine, script.mNumBodyLines);
		script.mType = scriptType;
		script.mName = scriptName;
		rootScripts.push_back(&script);
	}

	if(!loadDependencies(compileCtx, emitterCache, ScriptType::Emitter)
	|| !loadDependencies(compileCtx, affectorCache, ScriptType::Affector))
	{
		return false;
	}

	// Determine the common set of attributes
	compileCtx.mDeclHelper.declare(
		"life",
		DeclarationType::Attribute,
		DataType::Float,
		1,
		"<built-in>",
		false
	);
	if(!collectAttributes(compileCtx, emitterCache)
	|| !collectAttributes(compileCtx, affectorCache))
	{
		return false;
	}

	// Compute common code fragments
	size_t numFloats = 0;
	compileCtx.mStructDeclaration = "struct _gr_particle {\n";

	for(Declarations::const_iterator itr = compileCtx.mAttributes.begin()
	;   itr != compileCtx.mAttributes.end()
	;   ++itr)
	{
		// calculate attributes' locations
		compileCtx.mAttributeMap.insert(make_pair(itr->first, numFloats));
		numFloats += DataType::size(itr->second.mDataType);

		// define particle's state struct
		compileCtx.mStructDeclaration += "\t";
		compileCtx.mStructDeclaration += DataType::name(itr->second.mDataType);
		compileCtx.mStructDeclaration += ' ';
		compileCtx.mStructDeclaration += itr->first;
		compileCtx.mStructDeclaration += ";\n";
	}

	compileCtx.mStructDeclaration += "};\n";

	// generate sampler and output declarations
	compileCtx.mNumTextures = (numFloats + 3) / 4;//a texel has 4 fields: a, r, g, b
	compileCtx.mSamplerDeclarations += "uniform sampler2D _gr_tex[";
	compileCtx.mSamplerDeclarations += str(compileCtx.mNumTextures);
	compileCtx.mSamplerDeclarations += "];\n";

	compileCtx.mOutputDeclarations += "out vec4 _gr_out[";
	compileCtx.mOutputDeclarations += str(compileCtx.mNumTextures);
	compileCtx.mOutputDeclarations += "];\n";

	// Compile all scripts
	if(!compileModifiers(compileCtx, emitterCache)
	|| !compileModifiers(compileCtx, affectorCache)
	|| !compileRenderShaders(compileCtx, vshCache)
	|| !compileRenderShaders(compileCtx, fshCache))
	{
		return false;
	}

	// Link scripts
	string code;
	stringstream output;

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

		bool success;
		switch(script.mType)
		{
			case ScriptType::Emitter:
			case ScriptType::Affector:
				success = linkModifier(compileCtx, script, *cache, code);
				break;
			case ScriptType::VertexShader:
			case ScriptType::FragmentShader:
				success = linkRenderShader(compileCtx, script, code);
				break;
		}

		if(!success) { return false; }

		bool isVertexShader = script.mType == ScriptType::VertexShader;
		glslopt_shader* shader = glslopt_optimize(
			compiler->mGlslOptCtx,
			isVertexShader ? kGlslOptShaderVertex : kGlslOptShaderFragment,
			code.c_str(),
			0
		);
		bool status = glslopt_get_status(shader);
		if(status)
		{
			output << "@" << script.mName;
			switch(script.mType)
			{
				case ScriptType::Emitter:
					output << ".emitter";
					break;
				case ScriptType::Affector:
					output << ".affector";
					break;
				case ScriptType::VertexShader:
					output << ".vsh";
					break;
				case ScriptType::FragmentShader:
					output << ".fsh";
					break;
			}
			output << endl;
			output << (task->mOptimize ? glslopt_get_output(shader) : code.c_str());
			dumpLog(compileCtx, glslopt_get_log(shader));
		}
		else
		{
			dumpLog(compileCtx, glslopt_get_log(shader));
			return false;
		}

		glslopt_shader_delete(shader);
	}

	// Write final result
	ofstream outFile(task->mOutput);
	if(!outFile.good())
	{
		Logger(logStream) << "Can't open '" << task->mOutput << "' for writing";
		return false;
	}
	outFile << compileCtx.mNumTextures << endl
	        << output.str() << endl;

	return true;
}

static bool loadDependencies(
	CompileContext& ctx,
	ScriptCache& cache,
	ScriptType::Enum scriptType
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
			if(!findScript(ctx.mCompileTask.mIncludePaths, scriptName, scriptType, filename))
			{
				Logger(ctx.mCompiler.mLogStream) << "Cannot find script: '" << scriptName << '\'';
				return false;
			}

			Script* newScript = &cache[scriptName];
			if(!newScript->read(filename, ctx.mCompiler.mLogStream)) { return false; }

			newScript->mGeneratedCodeStartLine =
				ctx.mSourceMap.addMapping(
					filename,
					newScript->mFirstBodyLine,
					newScript->mNumBodyLines
				);
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

static bool collectAttributes(CompileContext& ctx, ScriptCache& cache)
{
	for(ScriptCache::const_iterator scriptItr = cache.begin()
	;   scriptItr != cache.end()
	;   ++scriptItr)
	{
		const Declarations& declarations = scriptItr->second.mDeclarations;
		for(Declarations::const_iterator declItr = declarations.begin()
		;   declItr != declarations.end()
		;   ++declItr)
		{
			//Ignore params
			if(declItr->second.mDeclType != DeclarationType::Attribute)
			{
				continue;
			}

			const string* conflictedFile;
			const Declaration* conflictedDecl;
			const Declaration& declaration = declItr->second;
			if(!ctx.mDeclHelper.declare(
				declItr->first,
				declaration,
				scriptItr->second.mFilename,
				true,
				&conflictedFile,
				&conflictedDecl
			))
			{
				Logger(ctx.mCompiler.mLogStream)
					<< scriptItr->second.mFilename
					<< ':' << declaration.mLine
					<< ": This declaration of '" << declItr->first << '\''
					<< " conflicts with a previous one"
					<< " (found at " << *conflictedFile <<
					':' << conflictedDecl->mLine << ')';
				return false;
			}
		}
	}

	return true;
}

static bool compileModifiers(
	const CompileContext& ctx,
	ScriptCache& cache
)
{
	for(ScriptCache::iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		Script& script = itr->second;
		string& code = script.mGeneratedCode;

		// signature
		code = "void ";
		code += script.mName;
		code += "(inout float _gr_seed, inout _gr_particle particle) {\n";

		// invoke dependencies
		const vector<string>& deps = script.mDependencies;
		for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
		{
			code += *itr;
			code += "(_gr_seed, particle);\n";
		}

		// add own code
		code += "#line ";
		code += str(script.mGeneratedCodeStartLine);
		code += '\n';
		code += script.mBody;

		code += "\n}\n";
	}

	return true;
}

static bool compileRenderShaders(
	const CompileContext& ctx,
	ScriptCache& cache
)
{
	for(ScriptCache::iterator itr = cache.begin(); itr != cache.end(); ++itr)
	{
		Script& script = itr->second;
		string& code = script.mGeneratedCode;

		code = "void main() {\n";
		generateFetch(ctx, script, code);
		code += "#line ";
		code += str(script.mGeneratedCodeStartLine);
		code += '\n';
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
		code += "ivec2 _gr_size = ivec2(_gr_texWidth, _gr_texHeight);\n"
		        "ivec2 _gr_texCoord = ivec2(gl_InstanceID % _gr_size.x, gl_InstanceID / _gr_size.y);\n";
	}
	else
	{
		code += "ivec2 _gr_texCoord = ivec2(gl_FragCoord.xy);\n";
	}

	// Define struct to hold state
	code += "_gr_particle particle;";
	if(isEmitter)
	{
		code += "_gr_particle _gr_previous;";
	}

	// Fetch texels
	for(size_t i = 0; i < ctx.mNumTextures; ++i)
	{
		code += "vec4 _gr_stream";
		code += str(i);
		code += " = texelFetch(_gr_tex[";
		code += str(i);
		code += "], _gr_texCoord, 0);\n";
	}

	string prefix = isEmitter ? "_gr_previous." : "particle.";
	for(Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
	{
		DataType::Enum attrType = itr->second.mDataType;
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
			code += "_gr_stream";
			code += str((attrLoc + count) / 4);
			code += '.';
			code += gFieldNames[(attrLoc + count) % 4];
		}
		code += ");\n";
	}
}

static bool linkModifier(
	const CompileContext& ctx,
	const Script& script,
	const ScriptCache& cache,
	std::string& code
)
{
	// emitter is trickier with temporary storage
	code = "#version 140\n"
	       "uniform float _gr_time;\n"
	       "uniform float _gr_chance;\n"
	       "uniform float dt;\n";
	code += ctx.mSamplerDeclarations;
	code += ctx.mOutputDeclarations;
	code += ctx.mStructDeclaration;

	// sort dependencies
	vector<const Script*> deps;
	collectDependencies(script, deps, cache);

	// generate uniform declarations
	Declarations uniforms;
	DeclarationHelper declHelper(uniforms);
	declHelper.copy(ctx.mDeclHelper);

	for(vector<const Script*>::const_iterator scriptItr = deps.begin()
	;   scriptItr != deps.end()
	;   ++scriptItr)
	{
		const Declarations& declarations = (*scriptItr)->mDeclarations;
		for(Declarations::const_iterator declItr = declarations.begin()
		;   declItr != declarations.end()
		;   ++declItr)
		{
			if(declItr->second.mDeclType != DeclarationType::Param) { continue; }

			const string* conflictedFile;
			const Declaration* conflictedDecl;
			if(!declHelper.declare(
				declItr->first,
				declItr->second,
				(*scriptItr)->mFilename,
				false,
				&conflictedFile,
				&conflictedDecl
			))
			{
				Logger(ctx.mCompiler.mLogStream)
					<< (*scriptItr)->mFilename
					<< ':' << declItr->second.mLine
					<< ": This declaration of '" << declItr->first << '\''
					<< " conflicts with a previous one"
					<< " (found at " << *conflictedFile <<
					':' << conflictedDecl->mLine << ')';
				return false;
			}
		}
	}

	for(Declarations::const_iterator itr = uniforms.begin()
	;   itr != uniforms.end()
	;   ++itr)
	{
		if(itr->second.mDeclType != DeclarationType::Param) { continue; }

		code += "uniform ";
		code += DataType::name(itr->second.mDataType);
		code += ' ';
		code += itr->first;
		code += ";\n";
	}

	// append custom declarations
	code += script.mCustomDeclarations;

	// add builtin functions
	code += "#line ";
	code += str(ctx.mBuiltInStartLine);
	code += '\n';
	code.append(builtins, builtins_len);

	// add dependencies
	for(vector<const Script*>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		code += (*itr)->mGeneratedCode;
		code += '\n';

		// Check for syntax error after every dependencies to prevent errors
		// from overflowing to next script
		if(!syntaxCheck(
			ctx,
			code,
			kGlslOptShaderFragment,
			kGlslOptionNotFullShader,
			*itr))
		{
			return false;
		}
	}

	// create main function
	code += "void main()  {\n"
	        "float _gr_seed = _gr_init_seed();\n";

	generateFetch(ctx, script, code);

	bool isEmitter = script.mType == ScriptType::Emitter;

	if(isEmitter)
	{
		// gen temporary vars
		for(Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
		{
			code += DataType::name(itr->second.mDataType);
			code += ' ';
			code += itr->first;
			code += ";\n";
		}
	}

	// invoke main script
	code += script.mName;
	code += "(_gr_seed, particle);\n";

	if(isEmitter)
	{
		// randomly select
		code += "bool _gr_canEmit = rand() <= _gr_chance;\n"
		        "bool _gr_dead = _gr_previous.life <= 0.0;\n"
		        "float _gr_selected = float(_gr_dead && _gr_canEmit);\n";
		for(Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
		{
			code += "particle.";
			code += itr->first;
			code += " = mix(_gr_previous.";
			code += itr->first;
			code += ", particle.";
			code += itr->first;
			code += ", _gr_selected);\n";
		}
	}

	// store
	for(Declarations::const_iterator itr = ctx.mAttributes.begin(); itr != ctx.mAttributes.end(); ++itr)
	{
		size_t attrLoc = ctx.mAttributeMap.find(itr->first)->second;
		size_t size = DataType::size(itr->second.mDataType);

		if(size > 1)
		{
			for(int j = 0; j < size; ++j)
			{
				code += "_gr_out[";
				code += str((attrLoc + j) / 4);
				code += "].";
				code += gFieldNames[(attrLoc + j) % 4];
				code += " = particle.";
				code += itr->first;
				code += '.';
				code += gFieldNames[j];
				code += ";\n";
			}
		}
		else
		{
			code += "_gr_out[";
			code += str(attrLoc / 4);
			code += "].";
			code +=	gFieldNames[attrLoc % 4];
			code += " = particle.";
			code += itr->first;
			code += ";\n";
		}
	}

	code += "}\n";

	return true;
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

static bool linkRenderShader(
	const CompileContext& ctx,
	const Script& script,
	std::string& code
)
{
	code = "#version 140\n";
	code += script.mCustomDeclarations;
	code += "uniform int _gr_texWidth;\n"
	        "uniform int _gr_texHeight;\n";
	code += ctx.mSamplerDeclarations;
	code += ctx.mStructDeclaration;
	code += script.mGeneratedCode;

	return true;
}

static void dumpLog(const CompileContext& ctx, const char* log, const Script* bottomScript = NULL)
{
	stringstream originalLog(log);
	string line;
	string filename;
	while(getline(originalLog, line))
	{
		string::size_type leftParenPos = line.find_first_of('(');
		string::size_type commaPos = line.find_first_of(',');
		string::size_type rightParenPos = line.find_first_of(')');
		string::size_type colonPos = line.find_first_of(':');

		if(leftParenPos < commaPos
			&& commaPos < rightParenPos
			&& rightParenPos < colonPos)
		{
			// remap lines
			unsigned int lineNumber =
				lexical_cast<unsigned int>(
					line.substr(leftParenPos + 1, commaPos - leftParenPos - 1)
				);

			// prevent line from flowing over bottom script
			if(bottomScript != NULL)
			{
				unsigned int maxLine =
					bottomScript->mGeneratedCodeStartLine +
					bottomScript->mNumBodyLines;

				lineNumber = lineNumber > maxLine ? maxLine : lineNumber;
			}

			unsigned int columnNumber =
				lexical_cast<unsigned int>(
					line.substr(commaPos + 1, rightParenPos - commaPos - 1)
				);

			string message = line.substr(colonPos + 1, line.length() - colonPos);

			unsigned int originalLine = 0;
			ctx.mSourceMap.lookup(lineNumber, filename, originalLine);

			Logger(ctx.mCompiler.mLogStream)
				<< filename
				<< ':' << originalLine
				<< ':' << columnNumber
				<< ':' << message;
		}
		else
		{
			Logger(ctx.mCompiler.mLogStream) << line;
		}
	}
}

static bool syntaxCheck(
	const CompileContext& ctx,
	const string& code,
	glslopt_shader_type shaderType,
	unsigned int options,
	const Script* bottomScript
)
{
	glslopt_shader* shader =
		glslopt_optimize(
			ctx.mCompiler.mGlslOptCtx,
			shaderType,
			code.c_str(),
			options
		);
	bool status = glslopt_get_status(shader);
	if(!status)
	{
		dumpLog(ctx, glslopt_get_log(shader), bottomScript);
	}
	glslopt_shader_delete(shader);

	return status;
}

};

bool compile(Compiler* compiler, CompileTask* task)
{
	return CompilerImpl::compile(compiler, task);
}
