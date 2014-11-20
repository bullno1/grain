#include "grainc.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <glsl_optimizer.h>
#include "Graph.hpp"

using namespace std;

namespace ScriptType
{
	enum Enum
	{
		Affector,
		Emitter,
		VertexShader,
		FragmentShader
	};
}

namespace DataType
{
	enum Enum
	{
		Float,
		Vec2,
		Vec3,
		Vec4
	};
};

struct ChannelMapping
{
	unsigned int mTextureUnit;
	unsigned int mVectorField;

	ChannelMapping(const ChannelMapping& other)
		:mTextureUnit(other.mTextureUnit)
		,mVectorField(other.mVectorField)
	{}

	ChannelMapping(unsigned int textureUnit = 0, unsigned int vectorField = 0)
		:mTextureUnit(textureUnit)
		,mVectorField(vectorField)
	{}

	const ChannelMapping next() const
	{
		unsigned int nextVectorField = mVectorField + 1;
		bool useNextTexture = nextVectorField >= 4;//A texture is a vec4: a,r,g,b
		nextVectorField = useNextTexture ? 0 : nextVectorField;
		unsigned int nextTextureUnit = useNextTexture ? mTextureUnit + 1 : mTextureUnit;
		return ChannelMapping(nextTextureUnit, nextVectorField);
	}
};

bool parseType(const string& typeName, DataType::Enum& out, ostream& err)
{
	if(typeName == "float")
	{
		out = DataType::Float;
		return true;
	}
	else if(typeName == "vec2")
	{
		out = DataType::Vec2;
		return true;
	}
	else if(typeName == "vec3")
	{
		out = DataType::Vec3;
		return true;
	}
	else if(typeName == "vec4")
	{
		out = DataType::Vec4;
		return true;
	}
	else
	{
		err << "Unknown type '" << typeName << "'";
		return false;
	}
}

size_t typeSize(const DataType::Enum type)
{
	switch(type)
	{
		case DataType::Float:
			return 1;
		case DataType::Vec2:
			return 2;
		case DataType::Vec3:
			return 3;
		case DataType::Vec4:
			return 4;
		default:
			return 0;
	}
}

const char* const typeName(const DataType::Enum type)
{
	switch(type)
	{
		case DataType::Float:
			return "float";
		case DataType::Vec2:
			return "vec2";
		case DataType::Vec3:
			return "vec3";
		case DataType::Vec4:
			return "vec4";
		default:
			return 0;
	}
}

typedef map<string, DataType::Enum> Declarations;
typedef map<string, ChannelMapping> AttributeMap;

struct Script
{
	string mFuncName;
	string mFileName;
	string mCustomDeclarations;
	ScriptType::Enum mType;
	Declarations mParams;
	Declarations mAttributes;
	string mBody;
	vector<string> mDependencies;
	unsigned int mFirstBodyLine;
};

struct Object
{
	const Script* mSource;
	string mCode;
};

struct CompileContext
{
	CompileContext(const Declarations& attributes)
		:mAttributes(attributes)
	{
		// map attributes to channels
		size_t numFloats = 0;
		ChannelMapping mapping(0, 0);

		for(Declarations::const_iterator itr = attributes.begin(); itr != attributes.end(); ++itr)
		{
			mAttributeMap.insert(make_pair(itr->first, mapping));
			size_t attrSize = typeSize(itr->second);
			numFloats += attrSize;
			for(size_t count = 0; count < attrSize; ++count)
			{
				mapping = mapping.next();
			}
		}
		mNumTextures = (numFloats + 3) / 4;//a texture has 4 channels

		// generate parameter list
		stringstream declParamList, invokeParamList;
		declParamList << "(inout int _count";
		invokeParamList << "(_count";
		for(Declarations::const_iterator itr = attributes.begin(); itr != attributes.end(); ++itr)
		{
			declParamList << ", inout " << typeName(itr->second) << " " << itr->first;
			invokeParamList << ", " << itr->first;
		}
		declParamList << ")";
		invokeParamList << ")";
		mDeclParamList = declParamList.str();
		mInvokeParamList = invokeParamList.str();

		// generate var declarations
		stringstream outputDecls;
		stringstream samplerDecls;
		for(int i = 0; i < mNumTextures; ++i)
		{
			samplerDecls << "uniform sampler2D _tex" << i << ";" << endl;
			outputDecls << "out vec4 out" << i << ";" << endl;
		}
		mOutputDeclarations = outputDecls.str();
		mSamplerDeclarations = samplerDecls.str();
	}

	Declarations mAttributes;
	size_t mNumTextures;;
	AttributeMap mAttributeMap;
	string mDeclParamList;
	string mInvokeParamList;
	string mOutputDeclarations;
	string mSamplerDeclarations;
};

const char* sHeader =
	"#version 140\n"
	"uniform float _time;\n"
	"uniform float _chance;\n"
	"uniform float dt;\n"
	;

extern "C" const char builtins[];
extern "C" const size_t builtins_len;

typedef map<string, Script> ScriptCache;
typedef map<string, string> CodeCache;

void split(string str, char delim, vector<string>& out)
{
	stringstream ss(str);
	string temp;

	while(getline(ss, temp, delim))
	{
		out.push_back(temp);
	}
}

const Script* loadScript(const string& fileName, ScriptCache& cache, ostream& err)
{
	ScriptCache::const_iterator itr = cache.find(fileName);
	if(itr != cache.end())
	{
		return &itr->second;
	}

	string::size_type pos = fileName.find_last_of('.');
	if(pos == string::npos)
	{
		err << "Can't determine script type of '" << fileName << "'";
		return NULL;
	}

	const string funcName = fileName.substr(0, pos);
	const string extension = fileName.substr(pos + 1, fileName.size() - pos);

	Script out;

	out.mFuncName = funcName;
	out.mFileName = fileName;

	if(extension == "emitter")
	{
		out.mType = ScriptType::Emitter;
	}
	else if(extension == "affector")
	{
		out.mType = ScriptType::Affector;
	}
	else if(extension == "vsh")
	{
		out.mType = ScriptType::VertexShader;
	}
	else if(extension == "fsh")
	{
		out.mType = ScriptType::FragmentShader;
	}
	else
	{
		err << "Can't determine script type of '" << fileName << "'";
		return NULL;
	}

	//TODO: search in search path
	ifstream input(fileName.c_str());
	if(!input.good())
	{
		err << "Can't open file '" << fileName << "'";
		return NULL;
	}

	string line;
	vector<string> tokens;
	stringstream parseErr;
	unsigned int lineCount = 0;
	out.mFirstBodyLine = 0;

	while(getline(input, line))
	{
		++lineCount;

		parseErr.str("");
		parseErr.clear();

		tokens.clear();
		split(line, ' ', tokens);
		string& firstTok = tokens[0];
		if(tokens.empty())
		{
			out.mBody += '\n';
			continue;
		}

		if(firstTok == "@param")
		{
			DataType::Enum paramType;
			if(parseType(tokens[1], paramType, parseErr))
			{
				//TODO: handle duplicates
				out.mParams.insert(make_pair(tokens[2], paramType));
			}
			else
			{
				err << fileName << "(" << lineCount << "): " << parseErr.str();
				return NULL;
			}
		}
		else if(firstTok == "@attribute")
		{
			DataType::Enum paramType;
			if(parseType(tokens[1], paramType, parseErr))
			{
				//TODO: handle duplicates
				out.mAttributes.insert(make_pair(tokens[2], paramType));
			}
			else
			{
				err << fileName << "(" << lineCount << "): " << parseErr.str();
				return NULL;
			}
		}
		else if(firstTok == "@require")
		{
			out.mDependencies.push_back(tokens[1]);
		}
		else if(firstTok == "@declare")
		{
			//TODO: #line
			out.mCustomDeclarations += line.substr(9, line.length() - 9);
			out.mCustomDeclarations += '\n';
		}
		else
		{
			if(out.mFirstBodyLine == 0)
			{
				out.mFirstBodyLine = lineCount;
			}

			out.mBody += line;
			out.mBody += "\n";
		}
	}

	//TODO: syntax check

	cache.insert(make_pair(fileName, out));
	return &cache[fileName];
}

typedef Graph<string> DependencyGraph;

void getFileName(ScriptType::Enum scriptType, const string& scriptName, string& out)
{
	out = scriptName;
	switch(scriptType)
	{
		case ScriptType::Affector:
			out += ".affector";
			break;
		case ScriptType::Emitter:
			out += ".emitter";
			break;
		case ScriptType::FragmentShader:
			out += ".fsh";
			break;
		case ScriptType::VertexShader:
			out += ".vsh";
			break;
	}
}

//TODO: handle conflicts
bool collectAttributes(const Script* script, ScriptCache& cache, Declarations& out, ostream& err)
{
	string depName;
	const Declarations& scriptAttrs = script->mAttributes;
	out.insert(scriptAttrs.begin(), scriptAttrs.end());

	const vector<string>& deps = script->mDependencies;
	for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		getFileName(script->mType, *itr, depName);
		const Script* depScript = loadScript(depName, cache, cerr);

		if(depScript == NULL)
		{
			return false;
		}

		if(!collectAttributes(depScript, cache, out, err))
		{
			return false;
		}
	}

	return true;
}

bool buildDepenencyGraph(const Script& script, DependencyGraph& out, ScriptCache& cache, set<string>& visitedScripts, ostream& err)
{
	string depName;
	visitedScripts.insert(script.mFileName);
	out.addNode(script.mFileName);
	//For each dependency
	for(vector<string>::const_iterator itr = script.mDependencies.begin(); itr != script.mDependencies.end(); ++itr)
	{
		getFileName(script.mType, *itr, depName);
		//If it's not visited
		if(visitedScripts.find(depName) == visitedScripts.end())
		{
			//Recursively load the dependencies of this dependency
			visitedScripts.insert(depName);//mark as visited
			out.addEdge(script.mFileName, depName);

			const Script* dependency = loadScript(depName, cache, err);
			if(dependency == NULL)
			{
				return false;
			}

			if(!buildDepenencyGraph(*dependency, out, cache, visitedScripts, err))
			{
				return false;
			}
		}
	}

	return true;
}

void findRootNodes(DependencyGraph& graph, set<string>& rootNodes)
{
}

void topoSort(DependencyGraph& graph, std::vector<string>& sortedDeps)
{
	set<string> rootNodes;
	graph.findRootNodes(rootNodes);

	while(!rootNodes.empty())
	{
		set<string>::const_iterator nodeItr = rootNodes.begin();
		rootNodes.erase(nodeItr);
		sortedDeps.push_back(*nodeItr);
		graph.removeNode(*nodeItr);
		graph.findRootNodes(rootNodes);
	}
}

bool compileModifier(const CompileContext& compileCtx, const Script* script, CodeCache& codeCache, ostream& err)
{
	CodeCache::const_iterator itr = codeCache.find(script->mFileName);
	if(itr != codeCache.end())
	{
		return &itr->second;
	}

	stringstream code;
	code << "void " << script->mFuncName << compileCtx.mDeclParamList << "{" << endl;

	// invoke dependencies
	const vector<string>& deps = script->mDependencies;
	for(vector<string>::const_iterator itr = deps.begin(); itr != deps.end(); ++itr)
	{
		code << *itr << compileCtx.mInvokeParamList << ";" << endl;
	}

	// inject script code
	// TODO: #line
	code << script->mBody << endl;

	code << "}" << endl;

	codeCache.insert(make_pair(script->mFileName, code.str()));
	return true;
}

void generateFetch(const CompileContext& compileCtx, const Script* script, ostream& code)
{
	bool isEmitter = script->mType == ScriptType::Emitter;
	bool isVertex = script->mType == ScriptType::VertexShader;
	if(isVertex)
	{
		code << "ivec2 _size = ivec2(_texWidth, _texHeight);" << endl
		     << "ivec2 _texCoord = ivec2(gl_InstanceID % _size.x, gl_InstanceID / _size.y);" << endl;
	}
	else
	{
		code << "ivec2 _texCoord = ivec2(gl_FragCoord.xy);" << endl;
	}
	//fetch
	const char* const fieldNames[] = { "x", "y", "z", "w" };
	for(size_t i = 0; i < compileCtx.mNumTextures; ++i)
	{
		code << "vec4 stream" << i << " = texelFetch(_tex" << i << ", _texCoord, 0);" << endl;
	}
	string prefix = isEmitter ? "previous_" : "";
	for(Declarations::const_iterator itr = compileCtx.mAttributes.begin(); itr != compileCtx.mAttributes.end(); ++itr)
	{
		ChannelMapping mapping = compileCtx.mAttributeMap.find(itr->first)->second;
		code << typeName(itr->second) << " " << prefix << itr->first << " = " << typeName(itr->second) << "(";
		bool first = true;

		size_t size = typeSize(itr->second);
		for(size_t count = 0; count < size; ++count)
		{
			if(first)
			{
				first = false;
			}
			else
			{
				code << ", ";
			}
			code << "stream" << mapping.mTextureUnit << "." << fieldNames[mapping.mVectorField];
			mapping = mapping.next();
		}
		code << ");" << endl;
	}
}

bool linkModifier(const CompileContext& compileCtx, const Script* script, ScriptCache& scriptCache, CodeCache& codeCache, ostream& code, ostream& err)
{
	// emitter is trickier with temporary storage
	bool isEmitter = script->mType == ScriptType::Emitter;
	// gather dependencies
	DependencyGraph depGraph;
	set<string> visitedScripts;
	if(!buildDepenencyGraph(*script, depGraph, scriptCache, visitedScripts, err))
	{
		return false;
	}
	// sort them
	vector<string> sortedDeps;
	topoSort(depGraph, sortedDeps);

	code
		<< sHeader << endl
		<< compileCtx.mSamplerDeclarations << endl
		<< compileCtx.mOutputDeclarations << endl;

	// generate uniform declarations
	Declarations uniforms;
	for(vector<string>::iterator itr = sortedDeps.begin(); itr != sortedDeps.end(); ++itr)
	{
		const Script* script = loadScript(*itr, scriptCache, err);
		const Declarations& params = script->mParams;
		uniforms.insert(params.begin(), params.end());
	}
	for(Declarations::const_iterator itr = uniforms.begin(); itr != uniforms.end(); ++itr)
	{
		code << "uniform " << typeName(itr->second) << " " << itr->first << ";" << endl;
	}

	// append custom declarations
	code << script->mCustomDeclarations << endl;

	// add builtin functions
	code.write(builtins, builtins_len);
	code << endl;

	// add dependencies
	for(vector<string>::const_reverse_iterator itr = sortedDeps.rbegin(); itr != sortedDeps.rend(); ++itr)
	{
		const Script* script = loadScript(*itr, scriptCache, err);

		if(!compileModifier(compileCtx, script, codeCache, err))
		{
			return false;
		}

		code << codeCache[script->mFileName] << endl;
	}

	// create main function
	code << "void main()  {" << endl
	     << "int _count = 0;" << endl;

	generateFetch(compileCtx, script, code);

	if(isEmitter)
	{
		// gen temporary vars
		for(Declarations::const_iterator itr = compileCtx.mAttributes.begin(); itr != compileCtx.mAttributes.end(); ++itr)
		{
			code << typeName(itr->second) << " " << itr->first << ";" << endl;
		}
	}

	// invoke main script
	code << script->mFuncName << compileCtx.mInvokeParamList << ";" << endl;

	if(isEmitter)
	{
		// randomly select
		code << "bool canEmit = rand() <= _chance;" << endl
			 << "bool dead = previous_life <= 0.0;" << endl
			 << "float selected = float(dead && canEmit);" << endl;
		for(Declarations::const_iterator itr = compileCtx.mAttributes.begin(); itr != compileCtx.mAttributes.end(); ++itr)
		{
			code << itr->first << " = mix(previous_" << itr->first << ", " << itr->first << ", selected);" << endl;
		}
	}

	// store
	const char* const fieldNames[] = { "x", "y", "z", "w" };
	for(Declarations::const_iterator itr = compileCtx.mAttributes.begin(); itr != compileCtx.mAttributes.end(); ++itr)
	{
		ChannelMapping mapping = compileCtx.mAttributeMap.find(itr->first)->second;
		size_t size = typeSize(itr->second);
		size_t count = 0;
		if(size > 1)
		{
			for(int j = 0; j < size; ++j)
			{
				code << "out" << mapping.mTextureUnit << "." << fieldNames[mapping.mVectorField]
				     << " = " << itr->first << "." << fieldNames[count] << ";" << endl;
				mapping = mapping.next();
				++count;
			}
		}
		else
		{
			code << "out" << mapping.mTextureUnit << "." << fieldNames[mapping.mVectorField]
			     << " = " << itr ->first << ";" << endl;
		}
	}

	code << "}" << endl;

	return true;
}

bool linkRenderShader(const CompileContext& compileCtx, const Script* script, ScriptCache& scriptCache, CodeCache& codeCache, ostream& code, ostream& err)
{
	code << "#version 140" << endl
	     << "uniform int _texWidth;" << endl
	     << "uniform int _texHeight;" << endl
	     << compileCtx.mSamplerDeclarations << endl
	     << script->mCustomDeclarations << endl;

	code << "void main() {" << endl;
	generateFetch(compileCtx, script, code);
	code << script->mBody << endl;
	code << "}" << endl;

	return true;
}

bool compileRenderShader(const CompileContext& compileCtx, const Script* script, CodeCache& codeCache, ostream& err)
{
	return true;
}

int compile(const CompileOptions& opts)
{
	const vector<string>& inputs = opts.mInputs;
	vector<const Script*> scripts;

	// load all scripts
	ScriptCache scriptCache;
	for(vector<string>::const_iterator itr = inputs.begin(); itr != inputs.end(); ++itr)
	{
		const Script* script = loadScript(*itr, scriptCache, cerr);
		if(script == NULL)
		{
			cerr << endl;
			return 1;
		}
		scripts.push_back(script);
	}

	// determine the common set of attributes
	Declarations sysAttrs;
	for(vector<const Script*>::const_iterator itr = scripts.begin(); itr != scripts.end(); ++itr)
	{
		if(!collectAttributes(*itr, scriptCache, sysAttrs, cerr))
		{
			cerr << endl;
			return 1;
		}
	}
	sysAttrs.insert(make_pair("life", DataType::Float));//life is a built-in attribute

	// link scripts
	CompileContext compileCtx(sysAttrs);
	CodeCache codeCache;
	stringstream output;

	output << compileCtx.mNumTextures << endl;

	glslopt_ctx* optCtx = glslopt_initialize(kGlslTargetOpenGL);
	for(vector<const Script*>::const_iterator itr = scripts.begin(); itr != scripts.end(); ++itr)
	{
		const Script* script = *itr;
		bool success = false;
		stringstream code;

		switch(script->mType)
		{
			case ScriptType::Emitter:
			case ScriptType::Affector:
				success = linkModifier(compileCtx, *itr, scriptCache, codeCache, code, cerr);
				break;
			case ScriptType::VertexShader:
			case ScriptType::FragmentShader:
				success = linkRenderShader(compileCtx, *itr, scriptCache, codeCache, code, cerr);
				break;
		}

		if(!success)
		{
			cerr << endl;
			glslopt_cleanup(optCtx);
			return 1;
		}

		bool isVertex = script->mType == ScriptType::VertexShader;
		glslopt_shader* shader = glslopt_optimize(optCtx, isVertex ? kGlslOptShaderVertex : kGlslOptShaderFragment, code.str().c_str(), 0);
		bool status = glslopt_get_status(shader);
		if(status)
		{
			output << "@" << script->mFileName << endl
			       << (opts.mOptimize ? glslopt_get_output(shader) : code.str().c_str());
		}
		else
		{
			cerr << glslopt_get_log(shader) << endl;
		}
		glslopt_shader_delete(shader);

		if(!status)
		{
			glslopt_cleanup(optCtx);
			return 1;
		}
	}

	glslopt_cleanup(optCtx);

	ofstream outFile(opts.mOutput.c_str(), ios::out);
	if(!outFile.good())
	{
		cerr << "Can't open '" << opts.mOutput << "' for writing" << endl;
		return 1;
	}
	outFile << output.str();

	return 0;
}
