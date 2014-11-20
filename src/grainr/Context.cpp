#include <GL/glew.h>
#include "Context.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "SystemDefinition.hpp"
#include "Shader.hpp"

using namespace std;

namespace grainr
{

namespace
{

inline bool endsWith(const std::string& value, const std::string& ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

const char* gQuadVshSource =
	"#version 140\n"
	"#extension GL_ARB_explicit_attrib_location: require\n"
	"layout(location = 0) in vec2 aPos;\n"
	"void main() {\n"
		"gl_Position = vec4(aPos, 0.0, 1.0);\n"
	"}\n"
	;

}

Context::Context()
{
	glGenBuffers(1, &mQuadBuff);
	glBindBuffer(GL_ARRAY_BUFFER, mQuadBuff);
	float quad[] = {
		-1.0f,  1.0f,
		 1.0f,  1.1f,
		 1.0f, -1.0,
		-1.0f, -1.0f
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	glGenVertexArrays(1, &mUpdateVAO);
	glBindVertexArray(mUpdateVAO);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, mQuadBuff);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindVertexArray(0);

	mQuadVsh = createShader(GL_VERTEX_SHADER, gQuadVshSource, cerr);

	mTime = 0.0f;
	mDt = 0.0f;
}

Context::~Context()
{
	glDeleteVertexArrays(1, &mUpdateVAO);
	glDeleteBuffers(1, &mQuadBuff);
}

void Context::update(float dt)
{
	mTime += dt;
	mDt = dt;
}

SystemDefinition* Context::load(const char* filename, std::ostream& err) const
{
	ifstream input(filename, ios::in);
	if(!input.good())
	{
		err << "Can't open '" << filename << "' for reading" << endl;
		return NULL;
	}

	SystemDefinition* def = new SystemDefinition();
	input >> def->mNumTextures;
	def->mContext = this;

	string line;
	stringstream content;
	string progName;
	while(getline(input, line))
	{
		if(line.length() > 0 && line[0] == '@')
		{
			if(content.tellp() > 0 && progName.length() > 0)
			{
				GLenum shaderType = endsWith(progName, ".vsh") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
				GLuint shader = createShader(shaderType, content.str().c_str(), err);
				if(shader == 0)
				{
					delete def;
					return NULL;
				}
				def->mShaders.insert(make_pair(progName, shader));

				content.str("");
				content.clear();
			}
			progName = line.substr(1, line.length() - 1);
		}
		else
		{
			content << line << endl;
		}
	}

	GLenum shaderType = endsWith(progName, ".vsh") ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
	GLuint shader = createShader(shaderType, content.str().c_str(), err);
	if(shader <= 0)
	{
		delete def;
		return NULL;
	}
	def->mShaders.insert(make_pair(progName, shader));

	return def;
}

}
