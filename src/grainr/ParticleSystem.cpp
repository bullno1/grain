#include <GL/glew.h>
#include <iostream>
#include <algorithm>
#include "ParticleSystem.hpp"
#include "SystemDefinition.hpp"
#include "Program.hpp"
#include "Context.hpp"
#include "Shader.hpp"

using namespace std;

namespace grainr
{

namespace
{

GLuint createTexture(GLsizei width, GLsizei height, void* data)
{
	GLuint handle;
	glGenTextures(1, &handle);
	glBindTexture(GL_TEXTURE_2D, handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data);
	return handle;
}

GLuint findShader(const char* name, const char* ext, const map<string, GLuint>& shaders)
{
	string fullName(name);
	fullName += '.';
	fullName += ext;
	map<string, GLuint>::const_iterator itr = shaders.find(fullName);
	return itr == shaders.end() ? 0 : itr->second;
}

}

ParticleSystem::ParticleSystem(const SystemDefinition* def, size_t width, size_t height)
	:mFlipFlag(true)
	,mTexWidth(width)
	,mTexHeight(height)
	,mDef(def)
{
	glGenFramebuffers(1, &mFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

	float* data = new float[4 * width * height];
	std::fill_n(data, 4 * width * height, -20.0f);
	for(size_t i = 0; i < def->mNumTextures; ++i)
	{
		GLuint evenTexture = createTexture(width, height, data);
		GLenum evenTarget = GL_COLOR_ATTACHMENT0 + i;
		glFramebufferTexture2D(GL_FRAMEBUFFER, evenTarget, GL_TEXTURE_2D, evenTexture, 0);
		mEvenTargets.push_back(evenTarget);
		mEvenTextures.push_back(evenTexture);

		GLuint oddTexture = createTexture(width, height, data);
		GLenum oddTarget = GL_COLOR_ATTACHMENT0 + def->mNumTextures + i;
		glFramebufferTexture2D(GL_FRAMEBUFFER, oddTarget, GL_TEXTURE_2D, oddTexture, 0);
		mOddTargets.push_back(oddTarget);
		mOddTextures.push_back(oddTexture);
	}
	delete[] data;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ParticleSystem::~ParticleSystem()
{
	glDeleteFramebuffers(1, &mFbo);
	glDeleteTextures(mDef->mNumTextures, mEvenTextures.data());
	glDeleteTextures(mDef->mNumTextures, mOddTextures.data());
}

void ParticleSystem::destroy()
{
	delete this;
}

Emitter* ParticleSystem::createEmitter(const char* name, std::ostream& err)
{
	GLuint shader = findShader(name, "emitter", mDef->mShaders);
	if(shader == 0)
	{
		err << "Cannot find emitter '" << name << "'" << endl;
		return NULL;
	}

	GLuint prog = createProgram(mDef->mContext->mQuadVsh, shader, mDef->mNumTextures, err);
	if(prog == 0) return NULL;

	Emitter* result = new Emitter;
	result->mHandle = prog;
	result->mSystem = this;
	return result;
}

Affector* ParticleSystem::createAffector(const char* name, std::ostream& err)
{
	GLuint shader = findShader(name, "affector", mDef->mShaders);
	if(shader == 0)
	{
		err << "Cannot find affector '" << name << "'" << endl;
		return NULL;
	}

	GLuint prog = createProgram(mDef->mContext->mQuadVsh, shader, mDef->mNumTextures, err);
	if(prog == 0) return NULL;

	Affector* result = new Affector;
	result->mHandle = prog;
	result->mSystem = this;
	return result;
}

Renderer* ParticleSystem::createRenderer(const char* name, std::ostream& err)
{
	GLuint vsh = findShader(name, "vsh", mDef->mShaders);
	GLuint fsh = findShader(name, "fsh", mDef->mShaders);

	if(vsh == 0)
	{
		err << "Cannot find vertex shader '" << name << "'" << endl;
		return NULL;
	}

	if(fsh == 0)
	{
		err << "Cannot find fragment shader '" << name << "'" << endl;
		return NULL;
	}

	GLuint prog = createProgram(vsh, fsh, mDef->mNumTextures, err);
	if(prog == 0) return NULL;

	Renderer* result = new Renderer;
	result->mHandle = prog;
	result->mSystem = this;
	result->prepare();
	glUniform1i(glGetUniformLocation(prog, "_gr_texWidth"), mTexWidth);
	glUniform1i(glGetUniformLocation(prog, "_gr_texHeight"), mTexHeight);
	return result;
}

void ParticleSystem::render(GLenum primType, GLsizei count)
{
	vector<GLuint>& inputTexs = mFlipFlag ? mOddTextures : mEvenTextures;
	for(size_t i = 0; i < mDef->mNumTextures; ++i)
	{
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, inputTexs[i]);
	}
	glDrawArraysInstanced(primType, 0, count, mTexWidth * mTexHeight);
}

void ParticleSystem::flip()
{
	vector<GLuint>& inputTexs = mFlipFlag ? mOddTextures : mEvenTextures;
	vector<GLuint>& renderTargets = mFlipFlag ? mEvenTargets : mOddTargets;
	mFlipFlag = !mFlipFlag;

	glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

	for(size_t i = 0; i < mDef->mNumTextures; ++i)
	{
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, inputTexs[i]);
	}
	glDrawBuffers(mDef->mNumTextures, renderTargets.data());
}

}
